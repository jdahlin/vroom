/* __builtin_constant_p evaluates its operand dry (references claim nothing).
 * When that evaluation ran out of temporaries with register variables live,
 * emit_function started over (longjmp 2, value.c) and the dry count was never
 * lowered: the retry claimed no strings, and the assembler stopped at
 * "undefined temporary symbol '.L.str.N'" (mlx5's fs_ft_pool.c, allmodconfig).
 * The operand: 40 right-nested levels, each holding a temporary. */
static const char *__attribute__((noinline)) first(const int *p, int n, int *out)
{
    int s = 0, t = 1, u = 2, v = 3;
    for (int i = 0; i < n; i++) { s += p[i]; t ^= p[i]; u += t; v -= s; }
    if (__builtin_constant_p(p[0] * (p[1] + p[1] * (p[2] + p[2] * (p[3] + p[3] * (p[4] + p[4] * (p[5] + p[5] * (p[6] + p[6] * (p[7] + p[7] * (p[8] + p[8] * (p[9] + p[9] * (p[10] + p[10] * (p[11] + p[11] * (p[12] + p[12] * (p[13] + p[13] * (p[14] + p[14] * (p[15] + p[15] * (p[16] + p[16] * (p[17] + p[17] * (p[18] + p[18] * (p[19] + p[19] * (p[20] + p[20] * (p[21] + p[21] * (p[22] + p[22] * (p[23] + p[23] * (p[24] + p[24] * (p[25] + p[25] * (p[26] + p[26] * (p[27] + p[27] * (p[28] + p[28] * (p[29] + p[29] * (p[30] + p[30] * (p[31] + p[31] * (p[32] + p[32] * (p[33] + p[33] * (p[34] + p[34] * (p[35] + p[35] * (p[36] + p[36] * (p[37] + p[37] * (p[38] + p[38] * (p[39] + p[39])))))))))))))))))))))))))))))))))))))))))
        s += 100;
    *out = s + t + u + v;
    return "first string";
}

static const char *__attribute__((noinline)) second(void)
{
    return "second string";
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

int main(void)
{
    static int p[41];
    int o;
    for (int i = 0; i < 41; i++)
        p[i] = i + 1;
    const char *a = first(p, 4, &o);
    if (o != 8) return 1;	/* s 10, t 5, u 10, v -17 */
    if (!same(a, "first string")) return 2;
    if (!same(second(), "second string")) return 3;
    return 0;
}
