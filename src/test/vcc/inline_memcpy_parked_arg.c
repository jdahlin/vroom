/*
 * __builtin_memcpy inlined with arguments parked in frame slots: inlined calls
 * two deep hold enough temporaries that gen_call parks each argument; the
 * inline copy used the freed registers, here dst == src (common/intern.c's
 * vintern_words16 under vintern_words: the self-built vcc's identifier hashes)
 */
typedef unsigned long u64;
static inline __attribute__((always_inline)) void w16(const char *s, u64 n, u64 *w0, u64 *w1)
{
	__builtin_memcpy(w0, s, 8);
	__builtin_memcpy(w1, s + 8, 8);
	*w0 &= n;
}
static inline __attribute__((always_inline)) void w(const char *s, u64 n, u64 *w0, u64 *w1)
{
	w16(s, n < 16 ? n : 16, w0, w1);
}
__attribute__((noinline)) u64 f(const char *s, u64 n)
{
	u64 a, b;

	w(s, n, &a, &b);
	return a + b;
}
int main(void)
{
	char b[24] = "abcdefghijklmnopq";

	return f(b, 7) != 0x706f6e6d6c6b6a6au || b[0] != 'a';
}
