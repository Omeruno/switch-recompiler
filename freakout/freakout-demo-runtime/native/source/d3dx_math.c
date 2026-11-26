#include "game.h"
#include <math.h>
#include <string.h>

#define IS(s) (!strcmp(name, (s)))

static bool read_values(Game *g, uint32_t address, float *out, size_t count)
{
    const uint8_t *data = address ? game_bytes(g, address, count * sizeof(float)) : NULL;
    if (!data)
        return false;
    memcpy(out, data, count * sizeof(float));
    return true;
}

static bool inverse(const float matrix[16], float output[16], float *determinant)
{
    double rows[4][8] = {{0}}, det = 1.0;
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 4; ++j)
            rows[i][j] = matrix[4 * i + j];
        rows[i][4 + i] = 1.0;
    }
    for (unsigned col = 0; col < 4; ++col) {
        unsigned pivot = col;
        for (unsigned i = col + 1; i < 4; ++i)
            if (fabs(rows[i][col]) > fabs(rows[pivot][col]))
                pivot = i;
        if (rows[pivot][col] == 0.0)
            return false;
        if (pivot != col) {
            for (unsigned i = 0; i < 8; ++i) {
                double temp = rows[pivot][i];
                rows[pivot][i] = rows[col][i];
                rows[col][i] = temp;
            }
            det = -det;
        }
        double divisor = rows[col][col];
        det *= divisor;
        for (unsigned i = 0; i < 8; ++i)
            rows[col][i] /= divisor;
        for (unsigned i = 0; i < 4; ++i) {
            if (i == col)
                continue;
            double factor = rows[i][col];
            for (unsigned j = 0; j < 8; ++j)
                rows[i][j] -= factor * rows[col][j];
        }
    }
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = 0; j < 4; ++j)
            output[4 * i + j] = (float)rows[i][4 + j];
    *determinant = (float)det;
    return true;
}

bool d3dx_math_dispatch(Game *g, const char *name)
{
    bool transpose = IS("D3DXMatrixTranspose"), invert = IS("D3DXMatrixInverse");
    bool multiply = IS("D3DXMatrixMultiply"), transform = IS("D3DXVec4Transform");
    bool normalize = IS("D3DXVec4Normalize");
    if (!transpose && !invert && !multiply && !transform && !normalize)
        return false;

    unsigned count = transform || normalize ? 4 : 16;
    unsigned pop = transpose || normalize ? 8 : 12;
    uint32_t output = arg(g, 0), a = arg(g, 1), b = pop == 12 ? arg(g, 2) : 0;
    void *destination = output ? game_bytes(g, output, count * sizeof(float)) : NULL;
    float first[16], second[16], values[16];
    if (!destination || !read_values(g, invert ? b : a, first, count))
        return result(g, 0, pop);

    if (invert) {
        float determinant;
        if (!inverse(first, values, &determinant))
            return result(g, 0, pop);
        if (a) {
            void *pointer = game_bytes(g, a, sizeof determinant);
            if (!pointer)
                return result(g, 0, pop);
            memcpy(pointer, &determinant, sizeof determinant);
        }
    } else if (transpose) {
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 4; ++col)
                values[row * 4 + col] = first[col * 4 + row];
    } else if (normalize) {
        double length = hypot(hypot(first[0], first[1]), hypot(first[2], first[3]));
        for (unsigned i = 0; i < 4; ++i)
            values[i] = length ? (float)(first[i] / length) : first[i];
    } else {
        if (!read_values(g, b, second, 16))
            return result(g, 0, pop);
        for (unsigned row = 0; row < count / 4; ++row) {
            for (unsigned col = 0; col < 4; ++col) {
                double sum = 0;
                for (unsigned i = 0; i < 4; ++i)
                    sum += (double)first[row * 4 + i] * second[i * 4 + col];
                values[row * 4 + col] = (float)sum;
            }
        }
    }
    memcpy(destination, values, count * sizeof(float));
    return result(g, output, pop);
}
