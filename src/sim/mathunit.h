#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Maths coprocessor: the guest has no FPU, so it latches two operands (IEEE-754 single bit patterns),
// writes an opcode, and reads the result back. Only the CPU thread touches it.
constexpr uint16_t PORT_MATH_A = 0x60;      // out: operand A
constexpr uint16_t PORT_MATH_B = 0x64;      // out: operand B
constexpr uint16_t PORT_MATH_OP = 0x68;     // out: opcode (executes); in: result
constexpr uint16_t PORT_MATH_STATUS = 0x6C; // in: bit 0 = last operation was invalid
constexpr uint32_t MATH_STATUS_INVALID = 1;

enum MathOpcode : uint32_t
{
    MATH_ADD = 1,   // a + b
    MATH_SUB,       // a - b
    MATH_MUL,       // a * b
    MATH_DIV,       // a / b
    MATH_SQRT,      // sqrt(a)
    MATH_SIN,       // sin(a), radians
    MATH_COS,
    MATH_TAN,
    MATH_ATAN2,     // atan2(a, b)
    MATH_POW,       // pow(a, b)
    MATH_ABS,
    MATH_FLOOR,
    MATH_CEIL,
    MATH_MIN,
    MATH_MAX,
    MATH_ITOF,      // a is a signed int, result is a float
    MATH_FTOI,      // a is a float, result is a signed int (truncated, saturated, NaN = 0)
};

class MathUnit
{
public:
    void reset() { m_a = m_b = m_result = m_status = 0; }

    bool handles(uint16_t port) const
    {
        return port == PORT_MATH_A || port == PORT_MATH_B || port == PORT_MATH_OP || port == PORT_MATH_STATUS;
    }

    void write(uint16_t port, uint32_t value)
    {
        if (port == PORT_MATH_A)
            m_a = value;
        else if (port == PORT_MATH_B)
            m_b = value;
        else if (port == PORT_MATH_OP)
            execute(value);
    }

    uint32_t read(uint16_t port) const
    {
        if (port == PORT_MATH_OP)
            return m_result;
        if (port == PORT_MATH_STATUS)
            return m_status;
        return 0;
    }

private:
    uint32_t m_a = 0, m_b = 0, m_result = 0, m_status = 0;

    static float to_f(uint32_t bits)
    {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    static uint32_t to_bits(float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
    }

    void execute(uint32_t op)
    {
        const float a = to_f(m_a), b = to_f(m_b);
        float r = 0;
        m_status = 0;
        switch (op)
        {
        case MATH_ADD: r = a + b; break;
        case MATH_SUB: r = a - b; break;
        case MATH_MUL: r = a * b; break;
        case MATH_DIV: r = a / b; break;
        case MATH_SQRT: r = std::sqrt(a); break;
        case MATH_SIN: r = std::sin(a); break;
        case MATH_COS: r = std::cos(a); break;
        case MATH_TAN: r = std::tan(a); break;
        case MATH_ATAN2: r = std::atan2(a, b); break;
        case MATH_POW: r = std::pow(a, b); break;
        case MATH_ABS: r = std::fabs(a); break;
        case MATH_FLOOR: r = std::floor(a); break;
        case MATH_CEIL: r = std::ceil(a); break;
        case MATH_MIN: r = std::fmin(a, b); break;
        case MATH_MAX: r = std::fmax(a, b); break;
        case MATH_ITOF: r = static_cast<float>(static_cast<int32_t>(m_a)); break;
        case MATH_FTOI:
        {
            int32_t i = 0;
            if (std::isnan(a))
                i = 0;
            else if (a >= 2147483648.0f)
                i = INT32_MAX;
            else if (a <= -2147483648.0f)
                i = INT32_MIN;
            else
                i = static_cast<int32_t>(a);
            m_result = static_cast<uint32_t>(i);
            return;
        }
        default:
            m_result = 0;
            m_status = MATH_STATUS_INVALID;
            return;
        }
        if (std::isnan(r) || std::isinf(r))
            m_status = MATH_STATUS_INVALID;
        m_result = to_bits(r);
    }
};
