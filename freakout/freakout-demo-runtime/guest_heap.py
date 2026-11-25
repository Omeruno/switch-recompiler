"""Deterministic, aligned guest heap shared by the x86 and ARM64 probes."""
from bisect import bisect_left, insort


class GuestHeap:
    def __init__(self, start, limit):
        self.top = start
        self.limit = limit
        self.allocations = {}
        self.free_spans = []
        self.free_starts = {}
        self.free_ends = {}

    @staticmethod
    def aligned(size):
        return (max(size, 1) + 15) & ~15

    def _take_span(self, index):
        length, start = self.free_spans.pop(index)
        del self.free_starts[start]
        del self.free_ends[start + length]
        return start, length

    def _remove_span(self, start, length):
        self._take_span(bisect_left(self.free_spans, (length, start)))

    def _add_span(self, start, length):
        previous = self.free_ends.get(start)
        if previous is not None:
            self._remove_span(previous, start - previous)
            length += start - previous
            start = previous
        following = self.free_starts.get(start + length)
        if following is not None:
            self._remove_span(start + length, following)
            length += following

        if start + length == self.top:
            self.top = start
            return
        self.free_starts[start] = length
        self.free_ends[start + length] = start
        insort(self.free_spans, (length, start))

    def allocate(self, size):
        capacity = self.aligned(size)
        index = bisect_left(self.free_spans, (capacity, 0))
        if index < len(self.free_spans):
            pointer, length = self._take_span(index)
            if length > capacity:
                self._add_span(pointer + capacity, length - capacity)
        else:
            pointer = self.top
            if pointer + capacity > self.limit:
                raise MemoryError('guest heap exhausted')
            self.top += capacity
        self.allocations[pointer] = max(size, 1)
        return pointer

    def free(self, pointer):
        if pointer:
            size = self.allocations.pop(pointer, None)
            if size is None:
                raise ValueError(f'invalid guest free {pointer:#x}')
            self._add_span(pointer, self.aligned(size))

    def resize(self, pointer, size, read, write):
        if not pointer:
            return self.allocate(size)
        if not size:
            self.free(pointer)
            return 0
        old_size = self.allocations.get(pointer)
        if old_size is None:
            raise ValueError(f'invalid guest realloc {pointer:#x}')
        old_capacity = self.aligned(old_size)
        capacity = self.aligned(size)
        if capacity <= old_capacity:
            if capacity < old_capacity:
                self._add_span(pointer + capacity, old_capacity - capacity)
            self.allocations[pointer] = size
            return pointer

        new = self.allocate(size)
        write(new, bytes(read(pointer, old_size)))
        self.free(pointer)
        return new
