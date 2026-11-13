/* Both the native AdvSIMD path and the portable path must preserve all lanes,
 * tuple layout, wraparound addition and single evaluation of operands. */
#include "../../headers/arm_neon.h"
#include <stdlib.h>

#define CHECK(C) do { if (!(C)) abort(); } while (0)
#define TEST(S, V, P, E, N, LOW, HIGH) do { \
 E aa[N], bb[N], out[N]; \
 for (int k = 0; k < N; ++k) { aa[k] = (E)(LOW + k); bb[k] = (E)(HIGH - k); } \
 int ai = 0, bi = 0; \
 V a = vld1q_##S(aa), b = vld1q_##S(bb); \
 P z = vzipq_##S((ai++, a), (bi++, b)); \
 CHECK(ai == 1 && bi == 1); \
 CHECK(sizeof(P) == 32 && _Alignof(P) == 16); \
 for (int k = 0; k < N; ++k) { \
   CHECK(z.val[k / (N/2)][2 * (k % (N/2))] == aa[k]); \
   CHECK(z.val[k / (N/2)][2 * (k % (N/2)) + 1] == bb[k]); } \
 P u = vuzpq_##S((ai++, z.val[0]), (bi++, z.val[1])); \
 CHECK(ai == 2 && bi == 2); \
 vst1q_##S(out, u.val[0]); \
 for (int k = 0; k < N; ++k) CHECK(out[k] == aa[k] && u.val[1][k] == bb[k]); \
 int once = 0; V d = vmovq_n_##S((once++, HIGH)); CHECK(once == 1); \
 V sum = vaddq_##S(d, vmovq_n_##S(1)); \
 for (int k = 0; k < N; ++k) CHECK(d[k] == HIGH && sum[k] == LOW); \
} while (0)

int main(void)
{
 TEST(s8, int8x16_t, int8x16x2_t, int8_t, 16, -128, 127);
 TEST(s16, int16x8_t, int16x8x2_t, int16_t, 8, -32768, 32767);
 TEST(s32, int32x4_t, int32x4x2_t, int32_t, 4, (-2147483647 - 1), 2147483647);
 return 0;
}
