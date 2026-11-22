#if defined(__aarch64__)
#include <arm_neon.h>

static volatile unsigned char start = 240;

int main(void)
{
    unsigned char bytes[16], output[16];
    for (unsigned i = 0; i < 16; ++i)
        bytes[i] = start + i;
    uint8x16_t data = vld1q_u8(bytes);
    vst1q_u8(output, vminq_u8(vcntq_u8(data), vdupq_n_u8(6)));

    for (unsigned i = 0; i < 16; ++i) {
        unsigned value = bytes[i], count = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            count += (value >> bit) & 1;
        if (output[i] != (count < 6 ? count : 6))
            return 1;
    }
    return 0;
}
#else
int main(void) { return 0; }
#endif
