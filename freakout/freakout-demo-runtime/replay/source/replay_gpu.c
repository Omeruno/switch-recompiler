#include "replay_gpu.h"
#include <stdio.h>
#include <string.h>

DkMemBlock replay_memory(DkDevice device, uint32_t bytes, uint32_t flags)
{
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device,
                            (bytes + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

bool replay_load_shader(DkMemBlock code, DkShader *shader, uint32_t offset, const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    void *destination = (uint8_t *)dkMemBlockGetCpuAddr(code) + offset;
    size_t count = fread(destination, 1, REPLAY_SHADER_STRIDE, file);
    bool valid = count >= 4 && count < REPLAY_SHADER_STRIDE && !ferror(file) &&
                 memcmp(destination, "DKSH", 4) == 0;
    fclose(file);
    if (!valid)
        return false;
    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, code, offset);
    dkShaderInitialize(shader, &maker);
    return true;
}

