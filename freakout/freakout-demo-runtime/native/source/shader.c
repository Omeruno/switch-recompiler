#include "shader.h"
#include <math.h>
#include <string.h>

enum {
    REG_TEMP = 0, REG_INPUT = 1, REG_CONST = 2, REG_ADDRESS = 3, REG_RASTER = 4,
    REG_COLOR = 5, REG_TEXCOORD = 6, REG_OUTPUT = 8, REG_SAMPLER = 10,
    MOV = 1, ADD, SUB, MAD, MUL, RCP, RSQ, DP3, DP4, MIN, MAX, SLT, SGE,
    LRP = 18, FRC, M4X4, M4X3, M3X4, M3X3, M3X2, DCL = 31,
    POW, NRM = 36, MOVA = 46, TEXCOORD = 64, TEX = 66, CND = 80, DEF, CMP = 88,
};

static const uint8_t arguments[] = {
    [MOV] = 1, [ADD] = 2, [SUB] = 2, [MAD] = 3, [MUL] = 2, [RCP] = 1, [RSQ] = 1,
    [DP3] = 2, [DP4] = 2, [MIN] = 2, [MAX] = 2, [SLT] = 2, [SGE] = 2, [LRP] = 3,
    [FRC] = 1, [M4X4] = 2, [M4X3] = 2, [M3X4] = 2, [M3X3] = 2, [M3X2] = 2,
    [POW] = 2, [NRM] = 1, [MOVA] = 1, [CND] = 3, [CMP] = 3,
};

static unsigned kind(uint32_t word)
{
    return (word >> 28 & 7) | (word >> 8 & 24);
}

static unsigned index_of(uint32_t word)
{
    return word & 2047;
}

static bool value_register(const Shader *s, uint32_t word, bool write)
{
    unsigned index = index_of(word);
    if (!(word & 0x80000000u))
        return false;
    switch (kind(word)) {
    case REG_TEMP: return index < 32;
    case REG_INPUT: return !write && index < (s->vertex ? 16u : 2u);
    case REG_CONST: return !write && index < (s->vertex ? 256u : 32u);
    case REG_ADDRESS: return index < (s->vertex ? 1u : 8u);
    case REG_RASTER: return s->vertex && index == 0;
    case REG_COLOR: return s->vertex && index < 2;
    case REG_TEXCOORD: return s->vertex && index < 8;
    case REG_OUTPUT: return !s->vertex && index == 0;
    default: return false;
    }
}

static bool decode(Shader *s, uint32_t opcode, uint32_t word, const uint32_t *args, size_t length)
{
    unsigned operation = opcode & 0xffff, count = 0;
    if (!length || s->count == sizeof s->instructions / sizeof *s->instructions)
        return false;
    ShaderInstruction decoded = {.instruction = opcode, .destination = word};
    unsigned modifier = word >> 20 & 15, shift = word >> 24 & 15;
    if (!value_register(s, word, true) || !(word >> 16 & 15) || modifier > 1 ||
        (shift > 3 && shift < 13))
        return false;
    if (operation == TEXCOORD || operation == TEX) {
        unsigned slot = index_of(word);
        if (s->vertex || (s->major == 2 && operation == TEXCOORD))
            return false;
        if (s->major == 1) {
            if (kind(word) != REG_ADDRESS || length != 1)
                return false;
        } else {
            if (length != 3 || kind(args[1]) != REG_SAMPLER || index_of(args[1]) >= 16 ||
                !(s->samplers & (1u << index_of(args[1]))) || !value_register(s, args[0], false) ||
                (args[0] >> 24 & 15) || (args[0] & 0x2000) || (opcode >> 16 & 255) > 1)
                return false;
            decoded.source[0] = args[0];
            slot = index_of(args[1]);
        }
        decoded.source[1] = slot;
        if (operation == TEX)
            s->samplers |= 1u << slot;
    } else {
        if (operation >= sizeof arguments || !(count = arguments[operation]))
            return false;
        size_t cursor = 1;
        for (unsigned i = 0; i < count; ++i) {
            if (cursor >= length)
                return false;
            uint32_t operand = args[cursor++ - 1];
            unsigned mod = operand >> 24 & 15;
            if (!value_register(s, operand, false) || mod == 9 || mod == 10 || mod > 12)
                return false;
            decoded.source[i] = operand;
            if (operand & 0x2000) {
                if (!s->vertex || kind(operand) != REG_CONST)
                    return false;
                decoded.relative[i] = 1;
                if (s->major == 2) {
                    if (cursor >= length)
                        return false;
                    uint32_t address = args[cursor++ - 1];
                    if (kind(address) != REG_ADDRESS || index_of(address))
                        return false;
                    decoded.relative[i] += address >> 16 & 3;
                }
            }
        }
        if (cursor != length)
            return false;
        if (operation >= M4X4 && operation <= M3X2 && kind(decoded.source[1]) != REG_CONST)
            return false;
    }
    s->instructions[s->count++] = decoded;
    return true;
}

bool shader_prepare(Shader *s, const uint32_t *tokens, size_t bytes, bool vertex, const void *constants)
{
    if (!tokens || bytes < 8 || bytes % 4)
        return false;
    *s = (Shader){.vertex = vertex, .major = tokens[0] >> 8 & 255};
    uint32_t profile = tokens[0] & 0xffff;
    if ((tokens[0] >> 16) != (vertex ? 0xfffeu : 0xffffu) ||
        (profile != 0x101 && profile != 0x200))
        return false;
    const unsigned limit = vertex ? 256 : 32;
    memcpy(s->constants, constants, limit * sizeof(ShaderVec));
    for (size_t i = 1, end = bytes / 4; i < end;) {
        uint32_t opcode = tokens[i++];
        unsigned operation = opcode & 0xffff;
        if (opcode == 0xffff)
            return i == end && s->count;
        unsigned length;
        if (operation == 0xfffe)
            length = opcode >> 16 & 32767;
        else if (s->major == 2)
            length = opcode >> 24 & 15;
        else if (operation == DCL)
            length = 2;
        else if (operation == DEF)
            length = 5;
        else if (operation == TEX || operation == TEXCOORD)
            length = 1;
        else
            length = operation < sizeof arguments && arguments[operation] ? arguments[operation] + 1 : 0;
        if (length > end - i)
            return false;
        const uint32_t *words = tokens + i;
        i += length;
        if (operation == 0 || operation == 0xfffe)
            continue;
        if (operation == DCL) {
            if (length != 2)
                return false;
            unsigned index = index_of(words[1]);
            if (vertex && kind(words[1]) == REG_INPUT && index < 16)
                s->inputs[index] = 0x100 | (words[0] & 15) | (words[0] >> 12 & 0xf0);
            else if (!vertex && kind(words[1]) == REG_SAMPLER && index < 16 && (words[0] >> 27 & 15) == 2)
                s->samplers |= 1u << index;
            else if (vertex || !value_register(s, words[1], false))
                return false;
        } else if (operation == DEF) {
            if (length != 5 || index_of(words[0]) >= limit || kind(words[0]) != REG_CONST)
                return false;
            memcpy(&s->constants[index_of(words[0])], words + 1, sizeof(ShaderVec));
        } else if (!length || !decode(s, opcode, words[0], words + 1, length)) {
            return false;
        }
    }
    return false;
}

typedef struct {
    ShaderVec temp[32], texture[8], address, position, result;
    ShaderVaryings output;
    const ShaderVec *inputs;
} Registers;

static const ShaderVec zero = {0, 0, 0, 0};

static ShaderVec splat(float value)
{
    return (ShaderVec){value, value, value, value};
}

// The parser verifies each register number and write mask before execution.
static ShaderVec *destination(Registers *regs, uint32_t word, bool vertex)
{
    unsigned index = index_of(word);
    switch (kind(word)) {
    case REG_TEMP: return &regs->temp[index];
    case REG_ADDRESS: return vertex ? &regs->address : &regs->texture[index];
    case REG_RASTER: return &regs->position;
    case REG_COLOR: return &regs->output.value[index];
    case REG_TEXCOORD: return &regs->output.value[SHADER_COLORS + index];
    default: return &regs->result;
    }
}

static ShaderVec read_source(const Shader *s, Registers *regs, uint32_t word, unsigned relative, unsigned row)
{
    ShaderVec value;
    unsigned index = index_of(word);
    if (kind(word) == REG_CONST) {
        int at = (int)index + row;
        if (relative) {
            float offset = regs->address[relative - 1];
            if (!isfinite(offset) || offset < -256 || offset > 256)
                return zero;
            at += (int)offset;
        }
        value = (unsigned)at < (s->vertex ? 256u : 32u) ? s->constants[at] : zero;
    } else if (kind(word) == REG_INPUT) {
        value = regs->inputs[index];
    } else {
        value = *destination(regs, word, s->vertex);
    }
    ShaderVec swizzled = {value[word >> 16 & 3], value[word >> 18 & 3],
                         value[word >> 20 & 3], value[word >> 22 & 3]};
    switch (word >> 24 & 15) {
    case 1: return -swizzled;
    case 2: return swizzled - 0.5f;
    case 3: return 0.5f - swizzled;
    case 4: return swizzled * 2.f - 1.f;
    case 5: return 1.f - swizzled * 2.f;
    case 6: return 1.f - swizzled;
    case 7: return swizzled * 2.f;
    case 8: return swizzled * -2.f;
    case 11:
    case 12:
        for (unsigned i = 0; i < 4; ++i)
            swizzled[i] = (word >> 24 & 15) == 11 ? fabsf(swizzled[i]) : -fabsf(swizzled[i]);
        break;
    }
    return swizzled;
}

static float dot(ShaderVec a, ShaderVec b, unsigned width)
{
    ShaderVec product = a * b;
    return product[0] + product[1] + product[2] + (width == 4 ? product[3] : 0.f);
}

static float clamp(float value)
{
    return fminf(fmaxf(value, 0.f), 1.f);
}

static ShaderVec execute(const Shader *s, Registers *regs, const ShaderInstruction *instruction,
                         const ShaderVaryings *input, ShaderSampler sample, void *context)
{
    unsigned opcode = instruction->instruction & 0xffff;
    const uint32_t *words = instruction->source;
    const uint8_t *relative = instruction->relative;
    if (opcode == TEX || opcode == TEXCOORD) {
        unsigned slot = words[1];
        ShaderVec uv = s->major == 1 ? input->value[SHADER_COLORS + slot] :
                       read_source(s, regs, words[0], 0, 0);
        if (opcode == TEXCOORD)
            return (ShaderVec){clamp(uv[0]), clamp(uv[1]), clamp(uv[2]), 1.f};
        if ((instruction->instruction >> 16 & 255) == 1)
            uv /= uv[3];
        return sample(context, slot, uv);
    }
    ShaderVec a = read_source(s, regs, words[0], relative[0], 0);
    ShaderVec b = arguments[opcode] > 1 ? read_source(s, regs, words[1], relative[1], 0) : zero;
    ShaderVec c = arguments[opcode] > 2 ? read_source(s, regs, words[2], relative[2], 0) : zero;
    switch (opcode) {
    case MOV: case MOVA: return a;
    case ADD: return a + b;
    case SUB: return a - b;
    case MAD: return a * b + c;
    case MUL: return a * b;
    case RCP: return splat(1.f / a[0]);
    case RSQ: return splat(1.f / sqrtf(fabsf(a[0])));
    case DP3: return splat(dot(a, b, 3));
    case DP4: return splat(dot(a, b, 4));
    case LRP: return (b - c) * a + c;
    case POW: return splat(powf(fabsf(a[0]), b[0]));
    case NRM: {
        float length = sqrtf(dot(a, a, 3));
        return length ? a / length : zero;
    }
    case CND: return a[0] > 0.5f ? b : c;
    case M4X4: case M4X3: case M3X4: case M3X3: case M3X2: {
        static const uint8_t rows[] = {4, 3, 4, 3, 2};
        ShaderVec value = {0};
        for (unsigned i = 0; i < rows[opcode - M4X4]; ++i)
            value[i] = dot(a, i ? read_source(s, regs, words[1], relative[1], i) : b, opcode <= M4X3 ? 4 : 3);
        return value;
    }
    default:
        for (unsigned i = 0; i < 4; ++i) {
            switch (opcode) {
            case MIN: a[i] = fminf(a[i], b[i]); break;
            case MAX: a[i] = fmaxf(a[i], b[i]); break;
            case SLT: a[i] = a[i] < b[i]; break;
            case SGE: a[i] = a[i] >= b[i]; break;
            case FRC: a[i] -= floorf(a[i]); break;
            case CMP: a[i] = a[i] >= 0.f ? b[i] : c[i]; break;
            }
        }
        return a;
    }
}

static void write_value(const Shader *s, Registers *regs, const ShaderInstruction *ins, ShaderVec value)
{
    uint32_t word = ins->destination;
    unsigned shift = word >> 24 & 15, mask = word >> 16 & 15, opcode = ins->instruction & 65535;
    if (shift)
        value *= ldexpf(1.f, shift < 8 ? (int)shift : (int)shift - 16);
    if (opcode == NRM || opcode == M4X3 || opcode == M3X3)
        mask &= 7;
    if (opcode == M3X2)
        mask &= 3;
    ShaderVec *reg = destination(regs, word, s->vertex);
    bool address = s->vertex && kind(word) == REG_ADDRESS;
    for (unsigned i = 0; i < 4; ++i) {
        if (mask >> i & 1) {
            float lane = word >> 20 & 1 ? clamp(value[i]) : value[i];
            (*reg)[i] = address ? (opcode == MOVA ? nearbyintf(lane) : floorf(lane)) : lane;
        }
    }
}

static void run(const Shader *s, Registers *regs, const ShaderVaryings *input, ShaderSampler sample, void *context)
{
    for (unsigned i = 0; i < s->count; ++i) {
        const ShaderInstruction *ins = s->instructions + i;
        ShaderVec value = execute(s, regs, ins, input, sample, context);
        if (i + 1 < s->count && ins[1].instruction & 0x40000000) {
            ShaderVec parallel = execute(s, regs, ins + 1, input, sample, context);
            write_value(s, regs, ins, value);
            write_value(s, regs, ins + 1, parallel);
            ++i;
        } else {
            write_value(s, regs, ins, value);
        }
    }
}

bool shader_vertex(const Shader *shader, const ShaderVec inputs[16], ShaderVec *position, ShaderVaryings *output)
{
    Registers regs = {.inputs = inputs};
    run(shader, &regs, NULL, NULL, NULL);
    *position = regs.position;
    *output = regs.output;
    for (unsigned i = 0; i < 4; ++i)
        if (!isfinite(regs.position[i]))
            return false;
    return true;
}

ShaderVec shader_pixel(const Shader *shader, const ShaderVaryings *input, ShaderSampler sample, void *context)
{
    Registers regs = {.inputs = input->value};
    memcpy(regs.texture, input->value + SHADER_COLORS, sizeof regs.texture);
    run(shader, &regs, input, sample, context);
    return shader->major == 1 ? regs.temp[0] : regs.result;
}
