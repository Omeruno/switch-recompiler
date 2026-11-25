"""D3DX9 row-vector matrix operations used by the demo renderer."""
import math
import struct


def multiply(left, right):
    return tuple(sum(left[row * 4 + k] * right[k * 4 + column] for k in range(4))
                 for row in range(4) for column in range(4))


def transpose(matrix):
    return tuple(matrix[column * 4 + row] for row in range(4) for column in range(4))


def transform(vector, matrix):
    return tuple(sum(vector[row] * matrix[row * 4 + column] for row in range(4))
                 for column in range(4))


def invert(matrix):
    rows = [list(matrix[i * 4:i * 4 + 4]) + [float(i == j) for j in range(4)] for i in range(4)]
    determinant = 1.0
    for column in range(4):
        pivot = max(range(column, 4), key=lambda row: abs(rows[row][column]))
        if rows[pivot][column] == 0:
            return None
        if pivot != column:
            rows[column], rows[pivot] = rows[pivot], rows[column]
            determinant = -determinant
        value = rows[column][column]
        determinant *= value
        rows[column] = [element / value for element in rows[column]]
        for row in range(4):
            if row != column:
                scale = rows[row][column]
                rows[row] = [left - scale * right for left, right in zip(rows[row], rows[column])]
    return tuple(value for row in rows for value in row[4:]), determinant


def handle_math(cpu, name, arg, finish):
    count = {
        'D3DXMatrixMultiply': 3, 'D3DXMatrixTranspose': 2, 'D3DXMatrixInverse': 3,
        'D3DXVec4Transform': 3, 'D3DXVec4Normalize': 2,
    }.get(name)
    if not count:
        return False

    def read(address, size):
        return struct.unpack(f'<{size}f', cpu.mem_read(address, 4 * size))

    if not arg(0) or not arg(count - 1) or (name != 'D3DXMatrixInverse' and not arg(1)):
        finish(0, count * 4)
        return True
    if name == 'D3DXMatrixMultiply':
        result = multiply(read(arg(1), 16), read(arg(2), 16))
    elif name == 'D3DXMatrixTranspose':
        result = transpose(read(arg(1), 16))
    elif name == 'D3DXMatrixInverse':
        inverse = invert(read(arg(2), 16))
        if inverse is None:
            finish(0, count * 4)
            return True
        result, determinant = inverse
        if arg(1):
            cpu.mem_write(arg(1), struct.pack('<f', determinant))
    elif name == 'D3DXVec4Transform':
        result = transform(read(arg(1), 4), read(arg(2), 16))
    else:
        vector = read(arg(1), 4)
        length = math.hypot(*vector)
        result = tuple(value / length for value in vector) if length else vector
    cpu.mem_write(arg(0), struct.pack(f'<{len(result)}f', *result))
    finish(arg(0), count * 4)
    return True
