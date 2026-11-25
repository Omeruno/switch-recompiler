import struct
import unittest

from unicorn import UC_ARCH_X86, UC_MODE_32, Uc
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

from thread_probe import (CRT_DOSERRNO, CRT_UNCAUGHT_EXCEPTIONS, CREATE_SUSPENDED,
                          ERROR_INVALID_HANDLE, INFINITE, THREAD_RETURN, WAIT_FAILED,
                          WAIT_TIMEOUT, ThreadProbe)


class GuestBoot:
    def __init__(self):
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0, 0x210000)
        self.cursor = 0x10000
        self.events = {}
        self.handles = {}
        self.status = None

    def allocate(self, size):
        address = self.cursor
        self.cursor += size
        return address

    def put(self, address, value):
        self.cpu.mem_write(address, struct.pack("<I", value))

    def get(self, address):
        return struct.unpack("<I", self.cpu.mem_read(address, 4))[0]

    def finish(self, value=0, pop=0):
        self.cpu.reg_write(UC_X86_REG_EAX, value)


class ThreadProbeTest(unittest.TestCase):
    def setUp(self):
        self.boot = GuestBoot()
        self.cpu = self.boot.cpu
        self.thread = ThreadProbe(self.boot)

    def test_worker_can_block_wake_and_return_to_parent(self):
        event = 0x10000
        self.boot.events[event] = (False, False)
        worker = self.thread.create(0, 0x2000, 42, 0, 4)
        self.assertEqual(self.boot.get(4), 2)

        self.cpu.reg_write(UC_X86_REG_EIP, 0x1000)
        self.boot.put(0, 0x8000)
        self.thread.schedule()
        self.assertEqual(self.thread.current, 2)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EIP), 0x2000)
        stack = self.cpu.reg_read(UC_X86_REG_ESP)
        self.assertEqual((self.boot.get(stack), self.boot.get(stack + 4)), (THREAD_RETURN, 42))

        self.cpu.reg_write(UC_X86_REG_EIP, 0x2004)
        self.boot.put(0, 0xA000)
        self.thread.wait(event, INFINITE, self.boot.finish)
        self.assertEqual(self.thread.current, 1)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EIP), 0x1000)
        self.assertEqual(self.boot.get(0), 0x8000)

        self.boot.events[event] = (False, True)
        self.thread.wait(worker, INFINITE, self.boot.finish)
        self.assertEqual(self.thread.current, 2)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EIP), 0x2004)
        self.assertEqual(self.boot.get(0), 0xA000)
        self.assertEqual(self.boot.events[event], (False, False))
        self.thread.leave(7)
        self.assertEqual(self.thread.current, 1)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EAX), 0)
        self.assertEqual(self.thread.threads[2].exit_code, 7)

    def test_timeout_does_not_run_a_suspended_thread(self):
        worker = self.thread.create(0, 0x2000, 42, CREATE_SUSPENDED, 0)
        self.thread.wait(worker, 15, self.boot.finish)
        self.assertEqual(self.thread.current, 1)
        self.assertGreaterEqual(self.thread.clock, 15)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EAX), WAIT_TIMEOUT)

    def test_crt_data_follows_current_thread(self):
        parent_errno = self.thread.errno_address()
        parent_unwinding = self.thread.crt_address(CRT_UNCAUGHT_EXCEPTIONS)
        self.assertFalse(self.thread.uncaught_exception())
        self.boot.put(parent_errno, 34)
        self.boot.put(self.thread.crt_address(CRT_DOSERRNO), 123)
        self.boot.put(parent_unwinding, 2)
        self.assertTrue(self.thread.uncaught_exception())

        self.thread.create(0, 0x2000, 0, 0, 4)
        self.thread.schedule()
        self.assertFalse(self.thread.uncaught_exception())
        self.assertEqual(self.boot.get(self.thread.errno_address()), 0)
        self.assertEqual(self.boot.get(self.thread.crt_address(CRT_DOSERRNO)), 0)
        self.thread.leave(0)
        self.assertEqual(self.thread.errno_address(), parent_errno)
        self.assertEqual(self.boot.get(parent_errno), 34)
        self.assertEqual(self.boot.get(self.thread.crt_address(CRT_DOSERRNO)), 123)
        self.assertTrue(self.thread.uncaught_exception())

    def test_wait_all_leaves_resources_untouched_until_ready(self):
        semaphore, event = 20, 21
        self.boot.handles[semaphore] = [1, 1]
        self.boot.events[event] = (False, False)
        self.thread.wait_group((semaphore, event), True, 0, 16, self.boot.finish)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EAX), WAIT_TIMEOUT)
        self.assertEqual(self.boot.handles[semaphore][0], 1)
        self.boot.events[event] = (False, True)
        self.thread.wait_group((semaphore, event), True, 0, 16, self.boot.finish)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EAX), 0)
        self.assertEqual(self.boot.handles[semaphore][0], 0)
        self.assertFalse(self.boot.events[event][1])

    def test_wait_any_returns_first_ready_index_and_rejects_closed_handles(self):
        events = (11, 12, 13)
        self.boot.events.update({11: (True, False), 12: (False, True), 13: (False, True)})
        self.thread.wait_group(events, False, 0, 16, self.boot.finish)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EAX), 1)
        self.assertFalse(self.boot.events[12][1])
        self.assertTrue(self.boot.events[13][1])
        self.assertTrue(self.thread.close_handle(12))
        self.thread.wait_group(events, False, 0, 16, self.boot.finish)
        self.assertEqual(self.cpu.reg_read(UC_X86_REG_EAX), WAIT_FAILED)
        self.assertEqual(self.thread.threads[1].last_error, ERROR_INVALID_HANDLE)
        self.assertTrue(self.boot.events[13][1])



if __name__ == "__main__":
    unittest.main()
