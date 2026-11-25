"""Trace the original Windows demo startup and catalogue its host API calls."""
import argparse
import hashlib
from fnmatch import fnmatchcase
import json
import math
import ntpath
import re
import struct
import time
from collections import Counter, deque
from pathlib import Path

import pefile
from d3d_probe import D3DProbe
from demo_archive import open_level_script
from guest_heap import GuestHeap
from guest_save import PendingSave
from crt_format import format_cdecl, scan_cdecl
from audio_probe import AudioProbe, pop_float, push_float
from input_probe import InputProbe
from video_probe import VideoProbe
from thread_probe import CRT_DOSERRNO, THREAD_RETURN, ThreadProbe
from window_probe import WindowProbe, WINDOW_RETURN
from unicorn import UC_ARCH_X86, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_MODE_32, Uc, UcError
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESP

DEMO_HASH = "69830e276dceeb1017be49a6193c6cfaa0905c6397b8aa9c2907e8707f4adce9"
IMAGE_BASE = 0x400000
LOW_MEMORY = 0x08000000
API_BASE = 0x100000
API_LIMIT = IMAGE_BASE
GATE = 0x0FF000
STACK = 0x00BE0000
BSS = 0x00A00000
GAME_ENTRY = 0x67DE70
DATA_IMPORTS = {"_acmdln": BSS, "_adjust_fdiv": BSS + 4}
COMMAND = BSS + 0x100
ARGV = BSS + 0x180
HEAP_BASE = 0x01000000


class Boot:
    def __init__(self, executable, cpp_runtime, c_runtime, stop_at_winmain=True, assets=None, probe_renderer=False, probe_d3d=False, probe_frame=False, probe_audio=False, present_limit=1, archive_read_limit=0, compressed_upload_limit=0, buffer_upload_limit=0, instruction_limit=20_000_000, frame_after_draw=False, drawn_frame_limit=1, draw_after_worker=False, save_root=None, guest_memory=LOW_MEMORY, trace_blocks=True, trace_history=None):
        if not LOW_MEMORY <= guest_memory <= 0x40000000 or guest_memory % 4096:
            raise ValueError("guest memory must be page-aligned and between 128 MiB and 1 GiB")
        self.stop_at_winmain = stop_at_winmain
        self.assets = assets
        self.save_root = Path(save_root).resolve() if save_root else None
        self.probe_frame = probe_frame or probe_audio
        self.probe_d3d = probe_d3d or self.probe_frame
        self.probe_renderer = probe_renderer or self.probe_d3d
        self.audio = AudioProbe(self) if probe_audio else None
        self.input = InputProbe(self) if probe_audio else None
        self.present_limit = present_limit
        self.frame_after_draw = frame_after_draw
        self.drawn_frame_limit = drawn_frame_limit
        self.draw_after_worker = draw_after_worker
        self.archive_read_limit = archive_read_limit
        self.compressed_upload_limit = compressed_upload_limit
        self.buffer_upload_limit = buffer_upload_limit
        self.instruction_limit = instruction_limit
        self.message_loop = WindowProbe(self) if present_limit > 1 else None
        self.window_handles = {}
        self.window_proc = 0
        self.d3d = D3DProbe(self) if self.probe_d3d else None
        self.files = {}
        self.pending_saves = {}
        self.write_failed = set()
        self.file_pushback = {}
        self.file_names = {}
        self.file_reads = {}
        self.sort_events = []
        self.searches = {}
        self.crt_searches = {}
        self.virtual_directories = set()
        self.cursor_count = 0
        self.random_state = 1
        self.file_events = []
        self.debug_messages = []
        self.video = VideoProbe(self) if self.input else None
        self.window_events = []
        self.file_bytes = 0
        self.pe = pefile.PE(data=executable)
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0, guest_memory)
        self.cpu.mem_write(IMAGE_BASE, self.pe.get_memory_mapped_image())
        self.cpu.mem_write(COMMAND, b"FreakOut.exe\0")
        self.put(0x0, 0xFFFFFFFF)
        self.put(BSS, COMMAND)
        self.put(ARGV, COMMAND)
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.apis = {}
        self.heap = GuestHeap(HEAP_BASE, guest_memory)
        self.handles = {}
        self.next_semaphore_handle = 0x100
        self.events = {}
        self.thread = ThreadProbe(self)
        self.next_event_handle = 0x10000
        self.ctors = []
        self.api_calls = Counter()
        self.api_sequence = [] if trace_history is None else deque(maxlen=trace_history)
        self.api_frames = [] if trace_history is None else deque(maxlen=trace_history)
        self.blocks = Counter()
        self.block_sizes = {}
        self.status = "instruction limit"
        self.winmain = None

        self.crt = pefile.PE(data=c_runtime)
        self.cpu.mem_map(self.crt.OPTIONAL_HEADER.ImageBase, self.crt.OPTIONAL_HEADER.SizeOfImage)
        self.cpu.mem_write(self.crt.OPTIONAL_HEADER.ImageBase, self.crt.get_memory_mapped_image())
        self.crt_guest = {
            symbol.name.decode("ascii"): self.crt.OPTIONAL_HEADER.ImageBase + symbol.address
            for symbol in self.crt.DIRECTORY_ENTRY_EXPORT.symbols
            if symbol.name in (b"qsort", b"_setjmp3")
        }
        self.cpu.mem_write(BSS + 0x402,
                           self.crt.get_data(0x0003A4D8, 512))
        self.cpp = pefile.PE(data=cpp_runtime)
        cpp_base = self.cpp.OPTIONAL_HEADER.ImageBase
        cpp_size = self.cpp.OPTIONAL_HEADER.SizeOfImage
        self.cpu.mem_map(cpp_base, (cpp_size + 0xFFF) & ~0xFFF)
        self.cpu.mem_write(cpp_base, self.cpp.get_memory_mapped_image())
        self.cpp_exports = {
            symbol.name.decode("ascii"): cpp_base + symbol.address
            for symbol in self.cpp.DIRECTORY_ENTRY_EXPORT.symbols
            if symbol.name and not symbol.forwarder
        }
        self.game_image_end = IMAGE_BASE + self.pe.OPTIONAL_HEADER.SizeOfImage
        self.runtime_code_start = self.crt.OPTIONAL_HEADER.ImageBase
        self.runtime_code_end = cpp_base + cpp_size
        self.bind(self.pe)
        self.bind(self.cpp)
        self.cpu.hook_add(UC_HOOK_CODE, self.host_call, begin=GATE, end=API_LIMIT - 1)
        self.cpu.hook_add(UC_HOOK_CODE, self.game_entry, begin=GAME_ENTRY, end=GAME_ENTRY)
        if trace_blocks:
            self.cpu.hook_add(UC_HOOK_BLOCK, self.block)

    def add_host_api(self, dll, name):
        address = API_BASE + 16 * len(self.apis)
        if address >= API_LIMIT:
            raise ValueError("guest import thunk table exhausted")
        self.apis[address] = (dll, name)
        return address

    def bind(self, module):
        for dll in module.DIRECTORY_ENTRY_IMPORT:
            name = dll.dll.decode("ascii")
            for imported in dll.imports:
                symbol = imported.name.decode("ascii") if imported.name else f"#{imported.ordinal}"
                address = self.cpp_exports.get(symbol) if name.lower() == "msvcp71.dll" else None
                if address is None:
                    address = DATA_IMPORTS.get(symbol)
                if address is None:
                    address = self.add_host_api(name, symbol)
                self.put(imported.address, address)

    def get(self, address):
        return struct.unpack("<I", self.cpu.mem_read(address, 4))[0]

    def put(self, address, value):
        self.cpu.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def read_file(self, handle, size):
        if not size:
            return b""
        prefix = self.file_pushback.pop(handle, b"")
        data = prefix + self.files[handle].read(size - len(prefix))
        if len(data) < size:
            self.put(handle + 12, self.get(handle + 12) | 0x10)
        return data

    def asset_path(self, name):
        if self.assets is None:
            return None
        translated = Path(name.replace("\\", "/"))
        if translated.is_absolute() or ":" in name:
            return None
        path = (self.assets / translated).resolve()
        if not path.is_relative_to(self.assets):
            return None
        if path.exists():
            return path
        # The original Windows paths vary in case, including the race sound banks.
        parent = self.assets
        for component in path.relative_to(self.assets).parts:
            candidate = parent / component
            if parent.is_dir() and not candidate.exists():
                matches = [item for item in parent.iterdir() if item.name.casefold() == component.casefold()]
                if len(matches) > 1:
                    return None
                if matches:
                    candidate = matches[0]
            parent = candidate.resolve()
            if not parent.is_relative_to(self.assets):
                return None
        return parent

    def save_path(self, name):
        if self.save_root is None or ':' in name:
            return None
        relative = Path(name.replace('\\', '/').lower())
        parts = relative.parts
        if relative.is_absolute() or '..' in parts:
            return None
        if parts[:2] != ('data', 'user') and parts != ('language.cfg',):
            return None
        path = (self.save_root / relative).resolve()
        return path if path.is_relative_to(self.save_root) else None

    def write_find_data(self, address, path):
        data = bytearray(320)
        size = path.stat().st_size
        struct.pack_into("<I", data, 0, 0x10 if path.is_dir() else 0x20)
        struct.pack_into("<II", data, 28, size >> 32, size & 0xFFFFFFFF)
        name = path.name.encode("ascii", "replace")[:259]
        data[44:44 + len(name)] = name
        self.cpu.mem_write(address, bytes(data))

    def write_crt_find_data(self, address, path):
        info = path.stat()
        created, accessed, modified = (int(value) & 0xFFFFFFFF
                                       for value in (info.st_ctime, info.st_atime, info.st_mtime))
        name = path.name.encode("ascii", "replace")[:259]
        data = struct.pack("<5I260s", 0x10 if path.is_dir() else 0x20,
                           created, accessed, modified, info.st_size & 0xFFFFFFFF, name)
        self.cpu.mem_write(address, data)

    def c_string(self, address, max_length=None):
        value = bytearray()
        limit = 0x10000 if max_length is None else max_length
        while len(value) < limit:
            start = address + len(value)
            size = min(64, limit - len(value), 4096 - (start & 4095))
            chunk = self.cpu.mem_read(start, size)
            terminator = chunk.find(0)
            if terminator >= 0:
                value.extend(chunk[:terminator])
                return bytes(value)
            value.extend(chunk)
        if max_length is not None:
            return bytes(value)
        raise ValueError("unterminated string")

    def wide_string(self, address):
        value = bytearray()
        for offset in range(0, 0x20000, 2):
            word = self.cpu.mem_read(address + offset, 2)
            if word == b"\0\0":
                return bytes(value)
            value.extend(word)
        raise ValueError("unterminated wide string")

    @property
    def heap_top(self):
        return self.heap.top

    def allocate(self, size):
        return self.heap.allocate(size)

    def block(self, cpu, address, size, _):
        if (IMAGE_BASE <= address < self.game_image_end or
                self.runtime_code_start <= address < self.runtime_code_end):
            self.blocks[address] += 1
            self.block_sizes[address] = max(size, self.block_sizes.get(address, 0))

    def game_entry(self, cpu, _address, _size, _):
        stack = cpu.reg_read(UC_X86_REG_ESP)
        self.winmain = [self.get(stack + 4 * n) for n in range(1, 5)]
        if self.stop_at_winmain:
            self.status = "WinMain"
            cpu.emu_stop()

    def ctor_next(self):
        return_address, original_stack, remaining = self.ctors[-1]
        if remaining:
            target = remaining.pop(0)
            self.cpu.reg_write(UC_X86_REG_ESP, original_stack)
            self.put(original_stack, GATE)
        else:
            self.ctors.pop()
            target = return_address
            self.cpu.reg_write(UC_X86_REG_ESP, original_stack + 4)
        self.cpu.reg_write(UC_X86_REG_EIP, target)

    def host_call(self, cpu, address, _size, _):
        previous = self.thread.current
        self.dispatch_host_call(cpu, address)
        if address in self.apis and self.status == "instruction limit":
            self.thread.after_import(previous)

    def dispatch_host_call(self, cpu, address):
        if address == THREAD_RETURN:
            self.thread.leave(cpu.reg_read(UC_X86_REG_EAX))
            return
        if address == WINDOW_RETURN and self.message_loop:
            self.message_loop.restore()
            return
        if address == GATE:
            self.ctor_next()
            return
        if address not in self.apis:
            self.status = f"unknown host address {address:#x}"
            cpu.emu_stop()
            return

        dll, name = self.apis[address]
        self.api_calls[f"{dll}!{name}"] += 1
        self.api_sequence.append((dll, name))
        stack = cpu.reg_read(UC_X86_REG_ESP)
        return_address = self.get(stack)
        self.api_frames.append((return_address, stack, cpu.reg_read(UC_X86_REG_EBP)))

        def arg(index):
            return self.get(stack + 4 + 4 * index)

        def finish(eax=0, pop=0):
            cpu.reg_write(UC_X86_REG_EAX, eax)
            cpu.reg_write(UC_X86_REG_ESP, stack + 4 + pop)
            cpu.reg_write(UC_X86_REG_EIP, return_address)

        if self.audio and dll.lower() == "fmod.dll" and self.audio.handle(name, arg, finish):
            return
        if self.input and self.input.handle(dll, name, arg, finish):
            return
        if self.message_loop and dll.lower() == "user32.dll" and self.message_loop.handle(name, arg, finish):
            return
        if self.video and self.video.handle(dll, name, arg, finish):
            return
        if self.d3d and self.d3d.handle(dll, name, arg, finish):
            return
        if self.probe_renderer and name == "_FSOUND_SetPriority@8":
            finish(0, 8)
        elif self.probe_renderer and name == "ShowCursor":
            self.cursor_count += 1 if arg(0) else -1
            finish(self.cursor_count, 4)
        elif self.probe_renderer and name in ("LoadIconA", "LoadCursorA", "GetStockObject"):
            finish(0x101 if name == "LoadIconA" else 0x102, 4 if name == "GetStockObject" else 8)
        elif self.probe_renderer and name == "RegisterClassA":
            self.window_proc = self.get(arg(0) + 4)
            class_name = self.c_string(self.get(arg(0) + 36)).decode("ascii", "replace")
            self.window_events.append(("window class", class_name))
            finish(1, 4)
        elif self.probe_renderer and name == "AdjustWindowRect":
            finish(1, 12)
        elif self.probe_renderer and name == "CreateWindowExA":
            title = self.c_string(arg(2)).decode("ascii", "replace")
            width, height = arg(6), arg(7)
            if not self.probe_d3d:
                self.status = f"window boundary: CreateWindowExA({title!r}, {width}x{height})"
                cpu.emu_stop()
            else:
                handle = 0x200 + len(self.window_handles)
                self.window_handles[handle] = (width, height)
                self.window_events.append(("create", title, width, height, arg(8)))
                finish(handle, 48)
        elif self.probe_d3d and name == "ShowWindow" and arg(0) in self.window_handles:
            self.window_events.append(("show", arg(0), arg(1)))
            finish(0, 8)
        elif self.probe_d3d and name in ("UpdateWindow", "SetCapture") and arg(0) in self.window_handles:
            finish(1 if name == "UpdateWindow" else 0, 4)
        elif self.probe_d3d and name in ("GetClientRect", "GetWindowRect") and arg(0) in self.window_handles:
            width, height = self.window_handles[arg(0)]
            cpu.mem_write(arg(1), struct.pack("<4I", 0, 0, width, height))
            finish(1, 8)
        elif self.probe_renderer and name == "LoadLibraryA" and self.c_string(arg(0)).lower() == b"d3d9.dll":
            self.status = "Direct3D boundary: LoadLibraryA(d3d9.dll)"
            cpu.emu_stop()
        elif name in ("__p__fmode", "__p__commode"):
            finish(BSS + (8 if name == "__p__fmode" else 12))
        elif name in ("__set_app_type", "__setusermatherr", "_controlfp", "_set_new_mode"):
            finish()
        elif name == "_initterm":
            start, end = arg(0), arg(1)
            constructors = [self.get(p) for p in range(start, end, 4)]
            self.ctors.append((return_address, stack, [p for p in constructors if p]))
            self.ctor_next()
        elif name == "__getmainargs":
            self.put(arg(0), 1)
            self.put(arg(1), ARGV)
            self.put(arg(2), ARGV + 4)
            finish()
        elif name == "GetModuleHandleA" and arg(0) == 0:
            finish(IMAGE_BASE, 4)
        elif name in ("GetSystemTime", "GetLocalTime"):
            cpu.mem_write(arg(0), struct.pack("<8H", 2007, 1, 4, 11, 12, 0, 0, 0))
            finish(pop=4)
        elif self.input and name == "OutputDebugStringA":
            self.debug_messages.append(self.c_string(arg(0)).decode("ascii", "replace"))
            finish(pop=4)
        elif self.input and name == "MultiByteToWideChar" and arg(0) in (0, 1252, 65001):
            count = arg(3)
            if count != 0xFFFFFFFF and count > 0x10000:
                raise ValueError("oversized MultiByteToWideChar input")
            value = (self.c_string(arg(2)) + b"\0" if count == 0xFFFFFFFF
                     else bytes(cpu.mem_read(arg(2), count)) if count else b"")
            encoded = value.decode("utf-8" if arg(0) == 65001 else "cp1252", "replace").encode("utf-16-le")
            size = len(encoded) // 2
            if arg(5) == 0:
                finish(size, 24)
            elif arg(4) and size <= arg(5):
                if encoded:
                    cpu.mem_write(arg(4), encoded)
                finish(size, 24)
            else:
                finish(0, 24)
        elif self.input and name == "GetKeyboardState":
            cpu.mem_write(arg(0), bytes(256))
            finish(1, 4)
        elif name == "GetVersionExA":
            size = self.get(arg(0))
            if size not in (148, 156):
                finish(0, 4)
            else:
                version = struct.pack("<5I", size, 5, 1, 2600, 2)
                cpu.mem_write(arg(0), version + bytes(size - len(version)))
                finish(1, 4)
        elif name == "GetStartupInfoA":
            cpu.mem_write(arg(0), bytes(68))
            self.put(arg(0), 68)
            finish(pop=4)
        elif name in ("InitializeCriticalSection", "EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection"):
            self.thread.critical(name, arg(0), finish)
        elif name == "InterlockedExchange":
            previous = self.get(arg(0))
            self.put(arg(0), arg(1))
            finish(previous, 8)
        elif name in ("InterlockedIncrement", "InterlockedDecrement"):
            delta = 1 if name == "InterlockedIncrement" else -1
            value = (self.get(arg(0)) + delta) & 0xFFFFFFFF
            self.put(arg(0), value)
            finish(value, 4)
        elif name == "___setlc_active_func":
            finish(self.get(BSS + 0x210))
        elif name == "___unguarded_readlc_active_add_func":
            finish(BSS + 0x214)
        elif name == "___lc_handle_func":
            finish(BSS + 0x220)
        elif name == "__pctype_func":
            finish(BSS + 0x402)
        elif name == "___lc_codepage_func":
            finish(0)
        elif name == "GetLastError":
            finish(self.thread.threads[self.thread.current].last_error)
        elif name == "SetLastError":
            self.thread.threads[self.thread.current].last_error = arg(0)
            finish(pop=4)
        elif name == "_ismbblead":
            finish()
        elif name == "_errno":
            finish(self.thread.errno_address())
        elif name == "__doserrno":
            finish(self.thread.crt_address(CRT_DOSERRNO))
        elif name == "__uncaught_exception":
            finish(int(self.thread.uncaught_exception()))
        elif name == "localeconv":
            table = BSS + 0x800
            decimal = table + 64
            cpu.mem_write(decimal, b".\0")
            fields = struct.pack("<10I", decimal, *([decimal + 1] * 9))
            cpu.mem_write(table, fields + bytes([127] * 8))
            finish(table)
        elif name == "GetCurrentThreadId":
            finish(self.thread.current)
        elif name in ("GetCurrentProcessId", "GetTickCount"):
            finish(1)
        elif name == "_beginthreadex" and arg(0) == 0:
            finish(self.thread.create(*(arg(i) for i in range(1, 6))))
        elif name in ("_endthreadex", "ExitThread"):
            self.thread.leave(arg(0))
        elif name == "Sleep":
            duration = arg(0)
            finish(pop=4)
            if len(self.thread.threads) > 1:
                self.thread.sleep(duration)
        elif name == "SetThreadPriority" and arg(0) in self.thread.handles:
            finish(1, 8)
        elif name in ("ResumeThread", "SuspendThread") and arg(0) in self.thread.handles:
            thread = self.thread.threads[self.thread.handles[arg(0)]]
            previous = thread.suspend
            thread.suspend = max(previous - 1, 0) if name == "ResumeThread" else previous + 1
            finish(previous, 4)
        elif name == "QueryPerformanceFrequency":
            self.put(arg(0), 1000000)
            self.put(arg(0) + 4, 0)
            finish(1, 4)
        elif name in ("QueryPerformanceCounter", "GetSystemTimeAsFileTime"):
            self.put(arg(0), 1)
            self.put(arg(0) + 4, 0)
            finish(1, 4)
        elif name == "CreateSemaphoreA":
            handle = self.next_semaphore_handle
            self.next_semaphore_handle += 1
            self.handles[handle] = [arg(1), arg(2)]
            finish(handle, 16)
        elif name == "CreateEventA" and not arg(0) and not arg(3):
            handle = self.next_event_handle
            self.next_event_handle += 1
            self.events[handle] = (bool(arg(1)), bool(arg(2)))
            finish(handle, 16)
        elif name in ("SetEvent", "ResetEvent") and arg(0) in self.events:
            handle = arg(0)
            manual, _ = self.events[handle]
            self.events[handle] = (manual, name == "SetEvent")
            finish(1, 4)
            if name == "SetEvent" and len(self.thread.threads) > 1:
                self.thread.schedule()
        elif name == "CloseHandle":
            finish(int(self.thread.close_handle(arg(0))), 4)
        elif name == "WaitForSingleObject":
            self.thread.wait(arg(0), arg(1), finish)
        elif name == "WaitForMultipleObjects":
            count = arg(0)
            handles = struct.unpack(f"<{count}I", cpu.mem_read(arg(1), count * 4)) if 1 <= count <= 64 else ()
            self.thread.wait_group(handles, bool(arg(2)), arg(3), 16, finish)
        elif name == "ReleaseSemaphore" and arg(0) in self.handles:
            semaphore = self.handles[arg(0)]
            previous = semaphore[0]
            if arg(1) and previous + arg(1) <= semaphore[1]:
                semaphore[0] += arg(1)
                if arg(2):
                    self.put(arg(2), previous)
                finish(1, 12)
            else:
                finish(0, 12)
        elif name in ("malloc", "??2@YAPAXI@Z", "calloc", "realloc"):
            try:
                if name == "realloc":
                    pointer = self.heap.resize(arg(0), arg(1), cpu.mem_read, cpu.mem_write)
                elif name == "calloc":
                    size = arg(0) * arg(1)
                    pointer = self.allocate(size)
                    if size:
                        cpu.mem_write(pointer, bytes(size))
                else:
                    pointer = self.allocate(arg(0))
            except MemoryError:
                self.put(self.thread.errno_address(), 12)
                pointer = 0
            finish(pointer)
        elif name in ("free", "??3@YAXPAX@Z", "??_V@YAXPAX@Z"):
            self.heap.free(arg(0))
            finish()
        elif name in ("_onexit", "atexit"):
            finish(arg(0) if name == "_onexit" else 0)
        elif name == "setlocale":
            cpu.mem_write(BSS + 0x200, b"C\0")
            finish(BSS + 0x200)
        elif name in ("tolower", "toupper"):
            value = arg(0)
            offset = 32 if name == "tolower" and 65 <= value <= 90 else -32 if name == "toupper" and 97 <= value <= 122 else 0
            finish(value + offset)
        elif name in ("isspace", "isprint", "isdigit", "isalpha", "isalnum", "iscntrl",
                      "ispunct", "isxdigit", "isupper", "islower", "isgraph"):
            code = arg(0)
            byte = bytes([code]) if code <= 255 else b""
            predicates = {
                "isspace": byte.isspace(), "isprint": 32 <= code <= 126,
                "isdigit": byte.isdigit(), "isalpha": byte.isalpha(),
                "isalnum": byte.isalnum(), "iscntrl": code < 32 or code == 127,
                "ispunct": 33 <= code <= 126 and not byte.isalnum(),
                "isxdigit": bool(byte) and byte in b"0123456789abcdefABCDEF",
                "isupper": byte.isupper(), "islower": byte.islower(),
                "isgraph": 33 <= code <= 126,
            }
            finish(int(predicates[name]))
        elif self.message_loop and name == "_mkdir":
            name = self.c_string(arg(0)).decode("ascii", "replace")
            path = self.save_path(name)
            if path is not None:
                try:
                    path.mkdir(parents=True)
                except OSError:
                    finish(0xFFFFFFFF)
                else:
                    finish()
                return
            path = self.asset_path(name)
            self.file_events.append(("mkdir", name, path is not None))
            if path and path not in self.virtual_directories and not path.exists():
                self.virtual_directories.add(path)
                finish()
            else:
                finish(0xFFFFFFFF)
        elif self.input and name == "GetFileAttributesA":
            filename = self.c_string(arg(0)).decode("ascii", "replace")
            path = self.save_path(filename) or self.asset_path(filename)
            if path and (path.is_dir() or path in self.virtual_directories):
                finish(0x10, 4)
            else:
                finish(0x20 if path and path.is_file() else 0xFFFFFFFF, 4)
        elif self.input and name == "FindFirstFileA":
            pattern = self.c_string(arg(0)).decode("ascii", "replace")
            asset = self.asset_path(pattern)
            matches = []
            if asset and asset.parent.is_dir():
                matches = sorted(path for path in asset.parent.iterdir()
                                 if fnmatchcase(path.name.lower(), asset.name.lower())
                                 and path.resolve().is_relative_to(self.assets))
            self.file_events.append(("find", pattern, tuple(path.name for path in matches)))
            if not matches:
                finish(0xFFFFFFFF, 8)
            else:
                self.write_find_data(arg(1), matches.pop(0))
                handle = self.allocate(4)
                self.searches[handle] = matches
                finish(handle, 8)
        elif self.input and name == "FindNextFileA" and arg(0) in self.searches:
            matches = self.searches[arg(0)]
            if matches:
                self.write_find_data(arg(1), matches.pop(0))
                finish(1, 8)
            else:
                finish(0, 8)
        elif self.input and name == "FindClose":
            finish(int(self.searches.pop(arg(0), None) is not None), 4)
        elif name in ("_findfirst", "_findnext", "_findclose"):
            if name == "_findfirst":
                pattern = self.c_string(arg(0)).decode("ascii", "replace")
                path = self.save_path(pattern) or self.asset_path(pattern)
                matches = []
                if path and path.parent.is_dir():
                    wildcard = "*" if path.name == "*.*" else path.name.lower()
                    matches = sorted(candidate for candidate in path.parent.iterdir()
                                     if fnmatchcase(candidate.name.lower(), wildcard)
                                     and candidate.resolve().is_relative_to(path.parent))
                self.file_events.append(("find crt", pattern, tuple(item.name for item in matches)))
                if matches:
                    handle = self.allocate(4)
                    self.crt_searches[handle] = iter(matches)
                    self.write_crt_find_data(arg(1), next(self.crt_searches[handle]))
                    finish(handle)
                else:
                    self.put(self.thread.errno_address(), 2)
                    finish(0xFFFFFFFF)
            elif name == "_findnext":
                match = next(self.crt_searches.get(arg(0), iter(())), None)
                if match is None:
                    self.put(self.thread.errno_address(), 2)
                    finish(0xFFFFFFFF)
                else:
                    self.write_crt_find_data(arg(1), match)
                    finish()
            else:
                handle = arg(0)
                if self.crt_searches.pop(handle, None) is None:
                    self.put(self.thread.errno_address(), 22)
                    finish(0xFFFFFFFF)
                else:
                    self.heap.free(handle)
                    finish()
        elif name == "fopen":
            path = self.c_string(arg(0)).decode("ascii", "replace")
            mode = self.c_string(arg(1)).decode("ascii", "replace")
            candidate = self.save_path(path)
            file = None
            pending = None
            read_only = mode in ("r", "rb")
            if read_only and (candidate is None or not candidate.is_file()):
                candidate = self.asset_path(path)
            if candidate is not None and mode in ("r", "rb", "w", "wb", "a", "ab"):
                try:
                    if read_only:
                        file = candidate.open("rb")
                    else:
                        pending = PendingSave(candidate, append=mode[0] == "a")
                        file = pending.stream
                except OSError:
                    pass
            if file is None and read_only:
                file = open_level_script(self.assets, path, mode)
            self.file_events.append(("open", path, mode, file is not None))
            if file is None:
                finish()
            else:
                # The guest CRT's feof/ferror macros read _iobuf._flag directly.
                handle = self.allocate(32)
                cpu.mem_write(handle, bytes(32))
                self.put(handle + 12, 1 if read_only else 2)
                self.files[handle] = file
                self.file_names[handle] = path
                if pending:
                    self.pending_saves[handle] = pending
                finish(handle)
        elif name == "fclose" and arg(0) in self.files:
            handle = arg(0)
            file = self.files.pop(handle)
            path = self.file_names.pop(handle)
            self.file_pushback.pop(handle, None)
            pending = self.pending_saves.pop(handle, None)
            failed = handle in self.write_failed
            self.write_failed.discard(handle)
            try:
                if not pending:
                    file.close()
                elif failed:
                    pending.abort()
                else:
                    pending.commit()
            except OSError:
                failed = True
            if pending:
                self.file_events.append(("save committed", path, not failed))
            self.heap.free(handle)
            finish(0xFFFFFFFF if failed else 0)
        elif name == "fwrite":
            width, count, handle = arg(1), arg(2), arg(3)
            if width * count > LOW_MEMORY:
                raise ValueError("write exceeds guest allocation budget")
            file = self.files.get(handle)
            written = 0
            if width and count and file and file.writable():
                try:
                    written = file.write(cpu.mem_read(arg(0), width * count)) // width
                except OSError:
                    pass
                if written != count:
                    self.write_failed.add(handle)
                    self.put(handle + 12, self.get(handle + 12) | 0x20)
            finish(written)
        elif name == "fprintf":
            file = self.files.get(arg(0))
            if file is None or not file.writable():
                finish(0xFFFFFFFF)
            else:
                content = format_cdecl(self.c_string(arg(1)), stack + 12, cpu.mem_read, self.c_string)
                try:
                    written = file.write(content)
                except OSError:
                    written = -1
                if written != len(content):
                    self.write_failed.add(arg(0))
                    self.put(arg(0) + 12, self.get(arg(0) + 12) | 0x20)
                    finish(0xFFFFFFFF)
                else:
                    finish(written)
        elif name in ("getc", "fgetc") and arg(0) in self.files:
            byte = self.read_file(arg(0), 1)
            finish(byte[0] if byte else 0xFFFFFFFF)
        elif name == "ungetc" and arg(1) in self.files:
            code, handle = arg(0), arg(1)
            if code == 0xFFFFFFFF or handle in self.file_pushback:
                finish(0xFFFFFFFF)
            else:
                self.file_pushback[handle] = bytes([code & 255])
                self.put(handle + 12, self.get(handle + 12) & ~0x10)
                finish(code & 255)
        elif name == "fread" and arg(3) in self.files:
            file = self.files[arg(3)]
            width, count = arg(1), arg(2)
            if width * count > LOW_MEMORY:
                raise ValueError("read exceeds guest allocation budget")
            offset = file.tell()
            content = self.read_file(arg(3), width * count)
            if content:
                cpu.mem_write(arg(0), content)
                self.file_bytes += len(content)
                path = self.file_names[arg(3)]
                if path.lower().endswith(".bnd"):
                    reads = self.file_reads.setdefault(path, [])
                    reads.append((offset, len(content), hashlib.sha256(content).hexdigest()))
                    if self.archive_read_limit and len(reads) >= self.archive_read_limit:
                        self.status = f"archive checkpoint: {path}, {len(reads)} reads"
                        cpu.emu_stop()
                        return
            finish(len(content) // width if width else 0)
        elif name == "fseek" and arg(0) in self.files:
            offset = struct.unpack("<i", struct.pack("<I", arg(1)))[0]
            relative = offset - len(self.file_pushback.get(arg(0), b"")) if arg(2) == 1 else offset
            try:
                self.files[arg(0)].seek(relative, arg(2))
                self.file_pushback.pop(arg(0), None)
                self.put(arg(0) + 12, self.get(arg(0) + 12) & ~0x10)
                finish()
            except (OSError, ValueError):
                finish(0xFFFFFFFF)
        elif name == "ftell" and arg(0) in self.files:
            finish(self.files[arg(0)].tell() - len(self.file_pushback.get(arg(0), b"")))
        elif name == "time":
            seconds = int(time.time())
            if not 0 <= seconds <= 0x7FFFFFFF:
                seconds = 0xFFFFFFFF
            if arg(0):
                self.put(arg(0), seconds)
            finish(seconds)
        elif name == "srand":
            self.random_state = arg(0)
            finish()
        elif name == "rand":
            self.random_state = (self.random_state * 214013 + 2531011) & 0xFFFFFFFF
            finish((self.random_state >> 16) & 0x7FFF)
        elif name == "sscanf":
            finish(scan_cdecl(self.c_string(arg(0)), self.c_string(arg(1)),
                              stack + 12, cpu.mem_read, cpu.mem_write))
        elif name in ("printf", "sprintf", "vsprintf"):
            to_stdout = name == "printf"
            pattern = self.c_string(arg(0 if to_stdout else 1))
            arguments = arg(2) if name == "vsprintf" else stack + (8 if to_stdout else 12)
            try:
                value = format_cdecl(pattern, arguments, cpu.mem_read, self.c_string)
            except ValueError as error:
                self.status = str(error)
                cpu.emu_stop()
                return
            if to_stdout:
                self.debug_messages.append(value.decode("ascii", "replace"))
            else:
                cpu.mem_write(arg(0), value + b"\0")
            finish(len(value))
        elif self.message_loop and name == "_makepath":
            drive, directory, name_part, extension = (self.c_string(arg(i)) if arg(i) else b""
                                                      for i in range(1, 5))
            if drive and not drive.endswith(b":"):
                drive = drive[:1] + b":"
            if directory and not directory.endswith((b"\\", b"/")):
                directory += b"\\"
            if extension and not extension.startswith(b"."):
                extension = b"." + extension
            cpu.mem_write(arg(0), drive + directory + name_part + extension + b"\0")
            finish()
        elif name == "_splitpath":
            path = self.c_string(arg(0))
            drive, rest = ntpath.splitdrive(path)
            separator = max(rest.rfind(b"/"), rest.rfind(b"\\"))
            directory, leaf = rest[:separator + 1], rest[separator + 1:]
            dot = leaf.rfind(b".")
            base, extension = (leaf[:dot], leaf[dot:]) if dot >= 0 else (leaf, b"")
            for index, value in enumerate((drive, directory, base, extension), 1):
                if arg(index):
                    cpu.mem_write(arg(index), value + b"\0")
            finish()
        elif name == "qsort" and dll.lower() == "msvcr71.dll":
            count, width, compare = arg(1), arg(2), arg(3)
            if count * width > LOW_MEMORY:
                raise ValueError("sort exceeds diagnostic limit")
            self.sort_events.append(("crt qsort", count, width, compare))
            # The native guest routine consumes the original cdecl stack and
            # calls the comparator in guest code, including its own imports.
            cpu.reg_write(UC_X86_REG_EIP, self.crt_guest[name])
        elif name == "_setjmp3" and dll.lower() == "msvcr71.dll":
            cpu.reg_write(UC_X86_REG_EIP, self.crt_guest[name])
        elif self.message_loop and name == "wcslen":
            finish(len(self.wide_string(arg(0))) // 2)
        elif name == "wcscpy":
            cpu.mem_write(arg(0), self.wide_string(arg(1)) + b"\0\0")
            finish(arg(0))
        elif name == "wcsncpy":
            width = arg(2)
            if width > LOW_MEMORY // 2:
                raise ValueError("wide string copy exceeds guest allocation budget")
            data = bytearray()
            for i in range(width):
                unit = cpu.mem_read(arg(1) + i * 2, 2)
                if unit == b"\0\0":
                    break
                data.extend(unit)
            if width:
                cpu.mem_write(arg(0), bytes(data).ljust(width * 2, b"\0"))
            finish(arg(0))
        elif self.message_loop and name == "wcstombs":
            try:
                value = self.wide_string(arg(1)).decode("utf-16-le").encode("ascii")
            except UnicodeError:
                finish(0xFFFFFFFF)
            else:
                capacity = arg(2) if arg(0) else len(value)
                if arg(0) and capacity:
                    cpu.mem_write(arg(0), (value + b"\0")[:capacity])
                finish(min(len(value), capacity))
        elif self.message_loop and name == "mbstowcs":
            source = self.c_string(arg(1))
            if not source.isascii():
                finish(0xFFFFFFFF)
            elif not arg(0):
                finish(len(source))
            else:
                converted = source[:arg(2)].decode("ascii").encode("utf-16-le")
                if len(source) < arg(2):
                    converted += b"\0\0"
                if converted:
                    cpu.mem_write(arg(0), converted)
                finish(min(len(source), arg(2)))
        elif self.message_loop and name in ("ceil", "floor"):
            value = struct.unpack("<d", cpu.mem_read(stack + 4, 8))[0]
            integral = (math.ceil if name == "ceil" else math.floor)(value)
            push_float(cpu, float(integral) if integral else math.copysign(0.0, value))
            finish()
        elif self.message_loop and name == "_CIfmod":
            divisor = pop_float(cpu)
            dividend = pop_float(cpu)
            push_float(cpu, math.fmod(dividend, divisor))
            finish()
        elif self.message_loop and name in ("_CIasin", "_CIacos"):
            value = pop_float(cpu)
            operation = math.asin if name == "_CIasin" else math.acos
            push_float(cpu, operation(value))
            finish()
        elif self.message_loop and name in ("atof", "strtod"):
            value = self.c_string(arg(0))
            match = re.match(rb"\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)", value)
            result = float(match[1]) if match else 0.0
            if name == "strtod" and arg(1):
                self.put(arg(1), arg(0) + (match.end() if match else 0))
            if math.isinf(result):
                self.put(self.thread.errno_address(), 34)
            push_float(cpu, result)
            finish()
        elif self.message_loop and name in ("strtol", "strtoul"):
            value = self.c_string(arg(0))
            text = value.lstrip()
            offset = len(value) - len(text)
            negative = text.startswith(b"-")
            if text.startswith((b"-", b"+")):
                offset += 1
                text = text[1:]
            base = arg(2)
            if base != 0 and not 2 <= base <= 36:
                self.put(self.thread.errno_address(), 22)
                finish()
                return
            if base in (0, 16) and re.match(rb"0[xX][0-9a-fA-F]", text):
                base = 16
                offset += 2
                text = text[2:]
            base = base or (8 if text.startswith(b"0") else 10)
            digits = b"0123456789abcdefghijklmnopqrstuvwxyz"[:base]
            maximum = 0xFFFFFFFF if name == "strtoul" else 0x7FFFFFFF + negative
            end = 0
            number = 0
            for digit in text.lower():
                index = digits.find(digit)
                if index < 0:
                    break
                number = min(maximum + 1, number * base + index)
                end += 1
            if arg(1):
                self.put(arg(1), arg(0) + (offset + end if end else 0))
            if number > maximum:
                self.put(self.thread.errno_address(), 34)
                result = -maximum if negative and name == "strtol" else maximum
            else:
                result = -number if negative else number
            finish(result)
        elif name == "atoi":
            match = re.match(rb"\s*([+-]?[0-9]+)", self.c_string(arg(0)))
            finish(int(match[1]) if match else 0)
        elif name == "strlen":
            finish(len(self.c_string(arg(0))))
        elif name in ("strchr", "strrchr"):
            text = self.c_string(arg(0)) + b"\0"
            char = bytes([arg(1) & 0xFF])
            index = text.find(char) if name == "strchr" else text.rfind(char)
            finish(arg(0) + index if index >= 0 else 0)
        elif name == "strcpy":
            cpu.mem_write(arg(0), self.c_string(arg(1)) + b"\0")
            finish(arg(0))
        elif name in ("strncpy", "strncat"):
            destination, source, count = arg(0), arg(1), arg(2)
            if count > self.heap.limit:
                raise ValueError("bounded string operation exceeds guest address space")
            if count:
                data = self.c_string(source, count)
                if name == "strncat":
                    end = destination + len(self.c_string(destination))
                    cpu.mem_write(end, data + b"\0")
                else:
                    cpu.mem_write(destination, data.ljust(count, b"\0"))
            finish(destination)
        elif name == "strstr":
            index = self.c_string(arg(0)).find(self.c_string(arg(1)))
            finish(arg(0) + index if index >= 0 else 0)
        elif name in ("strcmp", "_stricmp", "strncmp"):
            limit = arg(2) if name == "strncmp" else None
            left, right = self.c_string(arg(0), limit), self.c_string(arg(1), limit)
            if name == "_stricmp":
                left, right = left.lower(), right.lower()
            finish((left > right) - (left < right))
        elif self.message_loop and name == "memchr":
            count = arg(2)
            if count > LOW_MEMORY:
                raise ValueError("memory search exceeds diagnostic limit")
            data = bytes(cpu.mem_read(arg(0), count)) if count else b""
            index = data.find(bytes([arg(1) & 0xFF]))
            finish(arg(0) + index if index >= 0 else 0)
        elif name in ("memcpy", "memmove"):
            cpu.mem_write(arg(0), bytes(cpu.mem_read(arg(1), arg(2))))
            finish(arg(0))
        elif name == "memset":
            cpu.mem_write(arg(0), bytes([arg(1) & 0xFF]) * arg(2))
            finish(arg(0))
        else:
            self.status = f"unhandled import {dll}!{name} from {return_address:#x}"
            self.unhandled_arguments = [arg(i) for i in range(6)]
            cpu.emu_stop()

    def close_files(self):
        for handle, file in self.files.items():
            pending = self.pending_saves.get(handle)
            if pending:
                pending.abort()
            else:
                file.close()
        self.files.clear()
        self.file_names.clear()
        self.file_pushback.clear()
        self.pending_saves.clear()
        self.write_failed.clear()

    def run(self):
        start = IMAGE_BASE + self.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        try:
            self.cpu.emu_start(start, 0, count=self.instruction_limit)
        except (UcError, ValueError, MemoryError) as error:
            self.status = f"{self.cpu.reg_read(UC_X86_REG_EIP):#x}: {error}"
        finally:
            self.close_files()
        return {
            "files": self.file_events,
            "file_bytes": self.file_bytes,
            "archive_reads": self.file_reads,
            "sorts": self.sort_events,
            "debug": self.debug_messages,
            "video": self.video.events if self.video else [],
            "windows": self.window_events,
            "threads": self.thread.log,
            "unhandled_arguments": getattr(self, "unhandled_arguments", None),
            "d3d": self.d3d.events if self.d3d else [],
            "uploads": self.d3d.uploads if self.d3d else [],
            "audio": self.audio.events if self.audio else [],
            "input": self.input.events if self.input else [],
            "messages": (self.message_loop.events, self.message_loop.idle_polls) if self.message_loop else None,
            "import_sequence": list(self.api_sequence),
            "import_frames": list(self.api_frames),
            "status": self.status, "winmain_arguments": self.winmain, "imports": dict(self.api_calls),
            "blocks": {hex(a): size for a, size in sorted(self.block_sizes.items())},
        }


def load_demo(path):
    executable = path.read_bytes()
    if hashlib.sha256(executable).hexdigest() != DEMO_HASH:
        raise ValueError("expected the original Windows demo")
    runtime = path.with_name("msvcp71.dll")
    cpp = runtime.read_bytes()
    if hashlib.sha256(cpp).hexdigest() != "df96156f6a548fd6fe5672918de5ae4509d3c810a57bffd2a91de45a3ed5b23b":
        raise ValueError("expected the demo's msvcp71.dll next to the executable")
    crt = path.with_name("msvcr71.dll").read_bytes()
    if hashlib.sha256(crt).hexdigest() != "8094af5ee310714caebccaeee7769ffb08048503ba478b879edfef5f1a24fefe":
        raise ValueError("expected the demo's msvcr71.dll next to the executable")
    return executable, cpp, crt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--into-game", action="store_true")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--probe-renderer", action="store_true")
    parser.add_argument("--probe-d3d", action="store_true")
    parser.add_argument("--probe-frame", action="store_true")
    parser.add_argument("--probe-audio", action="store_true")
    parser.add_argument("--frame-after-draw", action="store_true")
    parser.add_argument("--drawn-frame-limit", type=int, choices=range(1, 17), default=1)
    parser.add_argument("--after-worker", action="store_true", help="count drawn frames after the game reaps its loading thread")
    parser.add_argument("--summary", action="store_true", help="omit full instruction and import histories")
    parser.add_argument("--buffer-upload-limit", type=int, default=0)
    parser.add_argument("--compressed-upload-limit", type=int, default=0)
    parser.add_argument("--instruction-limit", type=int, default=20_000_000)
    parser.add_argument("--archive-read-limit", type=int, default=0)
    parser.add_argument("--present-limit", type=int, default=1)
    parser.add_argument("--save-root", type=Path, help="allow profile and language writes under this directory")
    args = parser.parse_args()
    if not 1 <= args.present_limit <= 4096:
        parser.error("--present-limit must be between 1 and 4096")
    report = Boot(*load_demo(args.exe), stop_at_winmain=not (args.into_game or args.probe_renderer or args.probe_d3d or args.probe_frame or args.probe_audio),
                  assets=args.assets.resolve() if args.assets else None,
                  probe_renderer=args.probe_renderer, probe_d3d=args.probe_d3d, probe_frame=args.probe_frame, probe_audio=args.probe_audio, present_limit=args.present_limit, archive_read_limit=args.archive_read_limit, compressed_upload_limit=args.compressed_upload_limit,
                  buffer_upload_limit=args.buffer_upload_limit, instruction_limit=args.instruction_limit,
                  frame_after_draw=args.frame_after_draw, drawn_frame_limit=args.drawn_frame_limit,
                  draw_after_worker=args.after_worker, save_root=args.save_root).run()
    block_count = len(report["blocks"])
    if args.summary:
        report["last_imports"] = list(zip(report.pop("import_sequence")[-32:], report.pop("import_frames")[-32:]))
        report["block_count"] = len(report.pop("blocks"))
        d3d_events = report.pop("d3d")
        report["drawn_frames"] = [event for event in d3d_events if event[0] == "drawn frame"]
        report["last_d3d_calls"] = d3d_events[-24:]
        report["archive_reads"] = {name: len(reads) for name, reads in report["archive_reads"].items()}
        report.pop("uploads")
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(report["status"])
    print(f"{block_count} x86 basic blocks, {sum(report['imports'].values())} import calls")
    if report["winmain_arguments"]:
        print("WinMain:", [hex(arg) for arg in report["winmain_arguments"]])
    else:
        print(report["imports"])


if __name__ == "__main__":
    main()
