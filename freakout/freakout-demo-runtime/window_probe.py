"""Deterministic Win32 message queue for the startup trace."""
from collections import deque
import struct

from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

WM_QUIT = 0x0012
PM_REMOVE = 1
WINDOW_RETURN = 0x0FF010


class WindowProbe:
    def __init__(self, boot):
        self.boot = boot
        self.messages = deque()
        self.events = []
        self.idle_polls = 0
        self.callbacks = []

    def post(self, window, code, wparam=0, lparam=0):
        self.messages.append((window, code, wparam, lparam, self.idle_polls, 0, 0))
        self.events.append(("post", code, wparam))

    def restore(self):
        return_address, stack = self.callbacks.pop()
        self.boot.cpu.reg_write(UC_X86_REG_ESP, stack + 8)
        self.boot.cpu.reg_write(UC_X86_REG_EIP, return_address)

    def handle(self, name, arg, finish):
        if name == "PeekMessageA":
            hwnd, low, high = (arg(i) for i in range(1, 4))
            for position, message in enumerate(self.messages):
                window, code = message[:2]
                window_matches = not hwnd or (not window if hwnd == 0xFFFFFFFF else hwnd == window)
                if code != WM_QUIT and not window_matches:
                    continue
                if code != WM_QUIT and (low or high) and not low <= code <= high:
                    continue
                self.boot.cpu.mem_write(arg(0), struct.pack("<7I", *message))
                if arg(4) & PM_REMOVE:
                    del self.messages[position]
                self.events.append(("peek", code, bool(arg(4) & PM_REMOVE)))
                finish(1, 20)
                return True
            self.idle_polls += 1
            if self.idle_polls >= 10000:
                self.boot.status = "message loop reached its idle poll limit"
                self.boot.cpu.emu_stop()
            else:
                finish(0, 20)
            return True
        if name == "TranslateMessage":
            finish(pop=4)
            return True
        if name == "DispatchMessageA":
            hwnd, code, wparam, lparam = struct.unpack("<4I", self.boot.cpu.mem_read(arg(0), 16))
            if hwnd not in self.boot.window_handles or not self.boot.window_proc:
                finish(pop=4)
                return True
            stack = self.boot.cpu.reg_read(UC_X86_REG_ESP)
            self.callbacks.append((self.boot.get(stack), stack))
            callback_stack = stack - 20
            self.boot.cpu.mem_write(callback_stack, struct.pack("<5I", WINDOW_RETURN, hwnd, code, wparam, lparam))
            self.boot.cpu.reg_write(UC_X86_REG_ESP, callback_stack)
            self.boot.cpu.reg_write(UC_X86_REG_EIP, self.boot.window_proc)
            self.events.append(("dispatch", code, hwnd))
            return True
        if name == "DefWindowProcA":
            finish(pop=16)
            return True
        if name == "PostQuitMessage":
            self.post(0, WM_QUIT, arg(0))
            finish(pop=4)
            return True
        return False
