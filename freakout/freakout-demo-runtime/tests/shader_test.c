#include "../native/source/shader.h"
#include <assert.h>

static ShaderVec sample(void *context, unsigned slot, ShaderVec coordinates)
{
    assert(context == NULL && slot == 0);
    assert(coordinates[0] == 0.25f && coordinates[1] == 0.75f);
    return (ShaderVec){0.25f, 0.5f, 0.75f, 1.f};
}

int main(void)
{
    // Read a relative constant using a0.x, then write a swizzled varying.
    const uint32_t vs[] = {
        0xfffe0101, 0x1f, 0x80000000, 0x900f0000,
        0x01, 0xb0010000, 0x90000000, 0x01, 0xc00f0000, 0xa0e42003,
        0x01, 0xd00f0000, 0x901b0000, 0x01, 0xe00f0001, 0x90550000, 0xffff,
    };
    ShaderVec constants[256] = {[4] = {9.f, 10.f, 11.f, 12.f}};
    ShaderVec inputs[16] = {{1.f, 2.f, 3.f, 4.f}};
    ShaderVec position;
    ShaderVaryings varyings;
    Shader shader;
    assert(shader_prepare(&shader, vs, sizeof vs, true, constants));
    assert(shader.inputs[0] == 0x100);
    assert(shader_vertex(&shader, inputs, &position, &varyings));
    for (unsigned i = 0; i < 4; ++i) {
        assert(position[i] == constants[4][i]);
        assert(varyings.value[0][i] == inputs[0][3 - i]);
        assert(varyings.value[SHADER_COLORS + 1][i] == 2.f);
    }

    // The alpha instruction co-issues with MUL and reads the previous r0.r.
    const uint32_t ps[] = {
        0xffff0101, 0x42, 0xb00f0000, 0x01, 0x800f0000, 0xb0e40000,
        0x05, 0x80070000, 0x80e40000, 0xa0e40000,
        0x40000001, 0x80080000, 0x80000000, 0xffff,
    };
    constants[0] = (ShaderVec){2.f, 1.f, 0.5f, 0.f};
    varyings.value[SHADER_COLORS] = (ShaderVec){0.25f, 0.75f, 0.f, 1.f};
    assert(shader_prepare(&shader, ps, sizeof ps, false, constants));
    assert(shader.samplers == 1);
    ShaderVec result = shader_pixel(&shader, &varyings, sample, NULL);
    assert(result[0] == 0.5f && result[1] == 0.5f && result[2] == 0.375f && result[3] == 0.25f);

    assert(!shader_prepare(&shader, ps, sizeof ps - 4, false, constants));
    return 0;
}
