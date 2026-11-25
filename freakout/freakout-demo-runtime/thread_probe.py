"""Cooperative guest threads for deterministic x86 and ARM64 startup traces."""
from dataclasses import dataclass

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

THREAD_RETURN = 0x0FF020
INFINITE = 0xFFFFFFFF
WAIT_TIMEOUT = 258
WAIT_FAILED = 0xFFFFFFFF
ERROR_INVALID_HANDLE = 6
ERROR_INVALID_PARAMETER = 87
CREATE_SUSPENDED = 4
IMPORT_QUANTUM = 8192
CRT_THREAD_BYTES = 0x8C
CRT_ERRNO = 0x08
CRT_DOSERRNO = 0x0C
CRT_UNCAUGHT_EXCEPTIONS = 0x84


@dataclass
class Thread:
    context: object = None
    seh: int = 0xFFFFFFFF
    entry: int = 0
    stack: int = 0
    suspend: int = 0
    wait_handles: tuple = ()
    wait_all: bool = False
    last_error: int = 0
    deadline: int | None = None
    exit_code: int | None = None
    crt_data: int = 0


class ThreadProbe:
    def __init__(self, boot):
        self.boot = boot
        self.current = 1
        self.threads = {1: Thread()}
        self.handles = {}
        self.critical_sections = {}
        self.log = []
        self.clock = 0
        self.import_slice = 0
        self.initial_context = boot.cpu.context_save()

    def create(self, size, entry, argument, flags, id_out):
        if not entry or flags & ~CREATE_SUSPENDED:
            raise ValueError("unsupported guest thread creation")
        ident = len(self.threads) + 1
        stack_size = max(size, 1 << 20)
        stack = (self.boot.allocate(stack_size) + stack_size) & ~15
        stack -= 8
        self.boot.put(stack, THREAD_RETURN)
        self.boot.put(stack + 4, argument)
        self.threads[ident] = Thread(entry=entry, stack=stack, suspend=int(bool(flags)))
        handle = 0x20000 + ident
        self.handles[handle] = ident
        if id_out:
            self.boot.put(id_out, ident)
        self.log.append(("create", ident, entry, argument, flags))
        return handle

    def crt_address(self, offset):
        active = self.threads[self.current]
        if not active.crt_data:
            active.crt_data = self.boot.allocate(CRT_THREAD_BYTES)
            self.boot.cpu.mem_write(active.crt_data, bytes(CRT_THREAD_BYTES))
            self.boot.put(active.crt_data, self.current)
            self.boot.put(active.crt_data + 4, 0xFFFFFFFF)
        return active.crt_data + offset

    def errno_address(self):
        return self.crt_address(CRT_ERRNO)

    def uncaught_exception(self):
        return bool(self.boot.get(self.crt_address(CRT_UNCAUGHT_EXCEPTIONS)))

    def close_handle(self, handle):
        for handles in (self.boot.events, self.boot.handles, self.handles):
            if handle in handles:
                del handles[handle]
                return True
        self.threads[self.current].last_error = ERROR_INVALID_HANDLE
        return False

    def workers_reaped(self):
        workers = [thread for ident, thread in self.threads.items() if ident != 1]
        return bool(workers) and not self.handles and all(thread.exit_code is not None for thread in workers)

    def waitable(self, handle):
        return (handle in self.critical_sections or handle in self.boot.events
                or handle in self.boot.handles or handle in self.handles)

    def ready(self, handle, ident):
        if handle in self.critical_sections:
            return self.critical_sections[handle][0] in (0, ident)
        if handle in self.boot.events:
            return self.boot.events[handle][1]
        if handle in self.boot.handles:
            return self.boot.handles[handle][0] > 0
        return self.threads[self.handles[handle]].exit_code is not None

    def consume(self, handle, ident):
        if handle in self.critical_sections:
            _, depth = self.critical_sections[handle]
            self.critical_sections[handle] = (ident, depth + 1)
        elif handle in self.boot.events:
            manual, _ = self.boot.events[handle]
            if not manual:
                self.boot.events[handle] = (False, False)
        elif handle in self.boot.handles:
            self.boot.handles[handle][0] -= 1

    def wait_status(self, handles, wait_all, ident):
        if not all(map(self.waitable, handles)):
            self.threads[ident].last_error = ERROR_INVALID_HANDLE
            return WAIT_FAILED
        if wait_all:
            if all(self.ready(handle, ident) for handle in handles):
                for handle in handles:
                    self.consume(handle, ident)
                return 0
        else:
            for index, handle in enumerate(handles):
                if self.ready(handle, ident):
                    self.consume(handle, ident)
                    return index
        return None

    def wait_group(self, handles, wait_all, timeout, pop, finish):
        if not 1 <= len(handles) <= 64 or len(set(handles)) != len(handles):
            self.threads[self.current].last_error = ERROR_INVALID_PARAMETER
            finish(WAIT_FAILED, pop)
            return
        result = self.wait_status(handles, wait_all, self.current)
        if result is not None or timeout == 0:
            finish(WAIT_TIMEOUT if result is None else result, pop)
            return
        finish(0, pop)
        active = self.threads[self.current]
        active.wait_handles = handles
        active.wait_all = wait_all
        active.deadline = None if timeout == INFINITE else self.clock + timeout
        if pop == 16:
            self.log.append(("wait group", self.current, handles, wait_all, timeout))
        else:
            self.log.append(("wait", self.current, handles[0], timeout))
        self.schedule()

    def wait(self, handle, timeout, finish):
        self.wait_group((handle,), False, timeout, 8, finish)

    def sleep(self, duration):
        if duration:
            self.threads[self.current].deadline = self.clock + duration
        self.schedule()

    def critical(self, name, address, finish):
        handle = -address
        if name == "InitializeCriticalSection":
            self.critical_sections[handle] = (0, 0)
        elif name == "DeleteCriticalSection":
            del self.critical_sections[handle]
        elif name == "EnterCriticalSection":
            self.critical_sections.setdefault(handle, (0, 0))
            self.wait(handle, INFINITE, lambda value, _: finish(value, 4))
            return
        else:
            owner, depth = self.critical_sections[handle]
            if owner != self.current:
                raise ValueError("guest left a critical section it did not own")
            self.critical_sections[handle] = (owner, depth - 1) if depth > 1 else (0, 0)
        finish(pop=4)

    def leave(self, code):
        self.threads[self.current].exit_code = code
        self.log.append(("exit", self.current, code))
        self.schedule()

    def after_import(self, previous):
        if len(self.threads) == 1 or self.current != previous:
            return
        self.import_slice += 1
        if self.import_slice >= IMPORT_QUANTUM:
            self.clock += 1
            self.schedule()

    def schedule(self):
        self.import_slice = 0
        cpu = self.boot.cpu
        current = self.threads[self.current]
        if current.exit_code is None:
            current.context = cpu.context_save()
            current.seh = self.boot.get(0)

        order = list(self.threads)
        pivot = order.index(self.current) + 1
        order = order[pivot:] + order[:pivot]
        while True:
            deadlines = []
            for ident in order:
                thread = self.threads[ident]
                if thread.suspend or thread.exit_code is not None:
                    continue
                expired = thread.deadline is not None and thread.deadline <= self.clock
                result = None
                if thread.wait_handles:
                    result = self.wait_status(thread.wait_handles, thread.wait_all, ident)
                    if result is None and expired:
                        result = WAIT_TIMEOUT
                    if result is None:
                        if thread.deadline is not None:
                            deadlines.append(thread.deadline)
                        continue
                elif thread.deadline is not None and not expired:
                    deadlines.append(thread.deadline)
                    continue
                thread.wait_handles = ()
                thread.deadline = None
                if thread.context is not None:
                    if ident != self.current:
                        cpu.context_restore(thread.context)
                        cpu.reg_write(UC_X86_REG_EIP, cpu.reg_read(UC_X86_REG_EIP))
                else:
                    cpu.context_restore(self.initial_context)
                    cpu.reg_write(UC_X86_REG_EIP, thread.entry)
                    cpu.reg_write(UC_X86_REG_ESP, thread.stack)
                    cpu.reg_write(UC_X86_REG_EAX, 0)
                self.boot.put(0, thread.seh)
                if result is not None:
                    cpu.reg_write(UC_X86_REG_EAX, result)
                    self.log.append(("wake", ident, result))
                if ident != self.current:
                    self.log.append(("switch", self.current, ident))
                self.current = ident
                return
            if not deadlines:
                self.boot.status = "guest threads have no runnable work"
                cpu.emu_stop()
                return
            self.clock = min(deadlines)
