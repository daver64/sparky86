#include "sparky86.h"

// Must match MathOpcode / ports in src/sim/mathunit.h
enum : uint32_t
{
    OP_ADD = 1, OP_SUB, OP_MUL, OP_DIV, OP_SQRT, OP_SIN, OP_COS, OP_TAN, OP_ATAN2, OP_POW,
    OP_ABS, OP_FLOOR, OP_CEIL, OP_MIN, OP_MAX, OP_ITOF, OP_FTOI
};
constexpr uint16_t PORT_A = 0x60, PORT_B = 0x64, PORT_OP = 0x68, PORT_STATUS = 0x6C;

static fp32 math_op(uint32_t op, fp32 a, fp32 b)
{
    outl(PORT_A, a);
    outl(PORT_B, b);
    outl(PORT_OP, op);
    return inl(PORT_OP);
}

fp32 fp_from_int(int v) { return math_op(OP_ITOF, (uint32_t)v, 0); }
int fp_to_int(fp32 v) { return (int)math_op(OP_FTOI, v, 0); }
fp32 fp_add(fp32 a, fp32 b) { return math_op(OP_ADD, a, b); }
fp32 fp_sub(fp32 a, fp32 b) { return math_op(OP_SUB, a, b); }
fp32 fp_mul(fp32 a, fp32 b) { return math_op(OP_MUL, a, b); }
fp32 fp_div(fp32 a, fp32 b) { return math_op(OP_DIV, a, b); }
fp32 fp_sqrt(fp32 a) { return math_op(OP_SQRT, a, 0); }
fp32 fp_sin(fp32 a) { return math_op(OP_SIN, a, 0); }
fp32 fp_cos(fp32 a) { return math_op(OP_COS, a, 0); }
fp32 fp_tan(fp32 a) { return math_op(OP_TAN, a, 0); }
fp32 fp_atan2(fp32 y, fp32 x) { return math_op(OP_ATAN2, y, x); }
fp32 fp_pow(fp32 a, fp32 b) { return math_op(OP_POW, a, b); }
fp32 fp_abs(fp32 a) { return math_op(OP_ABS, a, 0); }
fp32 fp_floor(fp32 a) { return math_op(OP_FLOOR, a, 0); }
fp32 fp_ceil(fp32 a) { return math_op(OP_CEIL, a, 0); }
fp32 fp_min(fp32 a, fp32 b) { return math_op(OP_MIN, a, b); }
fp32 fp_max(fp32 a, fp32 b) { return math_op(OP_MAX, a, b); }
bool fp_invalid() { return inl(PORT_STATUS) & 1; }
