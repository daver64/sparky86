#include "sparky86.h"

// Thread-safe-ready Guest Command Publisher
void submit_gpu_command(uint32_t opcode, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t current_w = GPU_WRITE_PTR;
    uint32_t next_w = (current_w + 1) % GPU_RING_MAX_SLOTS;

    // Spinlock/Wait if the ring buffer is completely full (write caught up to read)
    while (next_w == GPU_READ_PTR)
    {
        if (quit_requested())
            return;
        __asm__ volatile("pause"); // Avoid wasting CPU pipeline energy while waiting
    }

    // Populate the packet values at our current write pointer slot
    GPU_RING_SLOTS[current_w].opcode = opcode;
    GPU_RING_SLOTS[current_w].arg1 = a1;
    GPU_RING_SLOTS[current_w].arg2 = a2;
    GPU_RING_SLOTS[current_w].arg3 = a3;
    GPU_RING_SLOTS[current_w].arg4 = a4;

    // Advance the write pointer to expose it to the host compiler
    GPU_WRITE_PTR = next_w;

    // Ring the doorbell register to alert the host to process the queue
    outw(0x0010, 1);
}

void clear_screen(uint32_t colour)
{
    submit_gpu_command(GPU_CMD_CLEAR_SCREEN, colour, 0, 0, 0);
}

void draw_colour(uint32_t colour)
{
    submit_gpu_command(GPU_CMD_DRAW_COLOUR, colour, 0, 0, 0);
}

void line(int x1,int y1, int x2, int y2)
{
    submit_gpu_command(GPU_CMD_DRAW_LINE,x1,y1,x2,y2);
}

void rect(int x, int y, int width, int height)
{
    submit_gpu_command(GPU_CMD_DRAW_RECT,x,y,width,height);
}

// Ends the frame and blocks until the host has consumed it, locking the guest to the display rate
void present()
{
    submit_gpu_command(GPU_CMD_PRESENT, 0, 0, 0, 0);
    while (GPU_READ_PTR != GPU_WRITE_PTR && !quit_requested())
    {
        __asm__ volatile("hlt"); // sleep until the next vblank (or key) interrupt instead of spinning
    }
}

static void put_char(char *buf, size_t size, size_t &pos, char c)
{
    if (pos + 1 < size)
        buf[pos] = c;
    pos++;
}

// Minimal printf: %d %i %u %x %X %c %s %f %% with width, '0' padding and '-' alignment.
// %f takes an fp32 (float bit pattern, see the maths unit) and honours a precision of 0-6 (default 6).
// Returns the length the full output would have had, like snprintf.
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap)
{
    size_t pos = 0;
    for (; *fmt; ++fmt)
    {
        if (*fmt != '%')
        {
            put_char(buf, size, pos, *fmt);
            continue;
        }
        ++fmt;
        bool left = false, zero = false;
        for (;; ++fmt)
        {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                zero = true;
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int prec = -1;
        if (*fmt == '.')
        {
            ++fmt;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }

        char tmp[24];
        const char *str = tmp;
        int len = 0;
        bool negative = false;
        switch (*fmt)
        {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        {
            uint32_t v;
            uint32_t base = (*fmt == 'x' || *fmt == 'X') ? 16 : 10;
            if (*fmt == 'd' || *fmt == 'i')
            {
                int32_t sv = __builtin_va_arg(ap, int32_t);
                negative = sv < 0;
                v = negative ? 0u - (uint32_t)sv : (uint32_t)sv;
            }
            else
                v = __builtin_va_arg(ap, uint32_t);
            const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char rev[12];
            int n = 0;
            do
            {
                rev[n++] = digits[v % base];
                v /= base;
            } while (v);
            // Zero padding goes between the sign and the digits
            if (negative && zero && !left)
            {
                put_char(buf, size, pos, '-');
                width--;
                negative = false;
            }
            if (negative)
                tmp[len++] = '-';
            while (n)
                tmp[len++] = rev[--n];
            break;
        }
        case 'f':
        {
            fp32 v = __builtin_va_arg(ap, uint32_t);
            if (prec < 0 || prec > 6)
                prec = 6;
            negative = (v >> 31) != 0;
            if (((v >> 23) & 0xFF) == 0xFF) // NaN or infinity
            {
                const char *word = (v & 0x7FFFFF) ? "nan" : "inf";
                negative = negative && (v & 0x7FFFFF) == 0;
                if (negative)
                    tmp[len++] = '-';
                while (*word)
                    tmp[len++] = *word++;
                break;
            }
            int pow10 = 1;
            for (int i = 0; i < prec; ++i)
                pow10 *= 10;
            const fp32 scale = fp_from_int(pow10);
            // Round half up at the requested precision, then split into integer and fraction
            const fp32 r = fp_add(fp_abs(v), fp_div(FP_HALF, scale));
            const fp32 ipf = fp_floor(r);
            int ip = fp_to_int(ipf); // saturates at 2147483647
            int frac = fp_to_int(fp_floor(fp_mul(fp_sub(r, ipf), scale)));
            if (frac >= pow10)
                frac = pow10 - 1;
            char rev[12];
            int n = 0;
            do
            {
                rev[n++] = (char)('0' + ip % 10);
                ip /= 10;
            } while (ip);
            if (negative && zero && !left)
            {
                put_char(buf, size, pos, '-');
                width--;
                negative = false;
            }
            if (negative)
                tmp[len++] = '-';
            while (n)
                tmp[len++] = rev[--n];
            if (prec > 0)
            {
                tmp[len++] = '.';
                for (int d = pow10 / 10; d; d /= 10)
                {
                    tmp[len++] = (char)('0' + (frac / d) % 10);
                }
            }
            break;
        }
        case 'c':
            tmp[0] = (char)__builtin_va_arg(ap, int);
            len = 1;
            break;
        case 's':
            str = __builtin_va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            while (str[len])
                len++;
            break;
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        case 0:
            --fmt; // dangling '%' at end of string
            continue;
        default:
            tmp[0] = '%';
            tmp[1] = *fmt;
            len = 2;
            break;
        }
        int pad = width > len ? width - len : 0;
        char padc = (zero && !left && *fmt != 's' && *fmt != 'c') ? '0' : ' ';
        if (!left)
            while (pad--)
                put_char(buf, size, pos, padc);
        for (int i = 0; i < len; ++i)
            put_char(buf, size, pos, str[i]);
        if (left)
            while (pad--)
                put_char(buf, size, pos, ' ');
    }
    if (size)
        buf[pos < size ? pos : size - 1] = 0;
    return (int)pos;
}

void textprintf(int x, int y, const char *fmt, ...)
{
    char buf[GPU_TEXT_MAX + 1];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);

    uint32_t len = 0;
    while (buf[len])
        len++;
    if (!len)
        return;

    submit_gpu_command(GPU_CMD_TEXT_BEGIN, x, y, len, 0);
    for (uint32_t off = 0; off < len; off += 16)
    {
        uint32_t words[4] = {0, 0, 0, 0};
        for (uint32_t i = 0; i < 16 && off + i < len; ++i)
            words[i / 4] |= (uint32_t)(uint8_t)buf[off + i] << ((i % 4) * 8);
        submit_gpu_command(GPU_CMD_TEXT_DATA, words[0], words[1], words[2], words[3]);
    }
}
