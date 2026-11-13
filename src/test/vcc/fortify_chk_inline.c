/* vcc-flags: -O2 */
/* __builtin___mem*_chk with object size -1 or a constant length within the object is the plain call, and a constant
 * memcpy/memset of at most 64 bytes is inline (ldp/stp), as clang -O2.  The runtime has no __memcpy_chk: a
 * leftover chk call fails to link. */
typedef unsigned long size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memmove(void *, const void *, size_t);
char *strcpy(char *, const char *);

#define SIZES(X) X(1) X(3) X(7) X(8) X(15) X(16) X(17) X(24) X(31) X(32) X(33) X(48) X(63) X(64) X(65)
#define CPY(N) __attribute__((noinline)) void cp##N(char *d, const char *s) { memcpy(d, s, N); } \
	__attribute__((noinline)) void cc##N(char *d, const char *s) { __builtin___memcpy_chk(d, s, N, -1UL); } \
	__attribute__((noinline)) void cd##N(char *d, const char *s) { __builtin___memcpy_chk(d, s, N, 100); } \
	__attribute__((noinline)) void ms##N(char *d) { memset(d, 0x5a, N); } \
	__attribute__((noinline)) void mz##N(char *d) { __builtin___memset_chk(d, 0, N, -1UL); } \
	__attribute__((noinline)) void mm##N(char *d, const char *s) { __builtin___memmove_chk(d, s, N, 200); }
SIZES(CPY)
__attribute__((noinline)) char *sc(char *d, const char *s) { return __builtin___strcpy_chk(d, s, -1UL); }

static char src[128], dst[160];
static int bad;

#define T(N) \
	memset(dst, 1, sizeof dst); cp##N(dst + 8, src); \
	for (int i = 0; i < 160; i++) bad |= dst[i] != (i >= 8 && i < 8 + N ? src[i - 8] : 1); \
	memset(dst, 1, sizeof dst); cc##N(dst + 8, src); \
	for (int i = 0; i < 160; i++) bad |= dst[i] != (i >= 8 && i < 8 + N ? src[i - 8] : 1); \
	memset(dst, 1, sizeof dst); cd##N(dst + 8, src); \
	for (int i = 0; i < 160; i++) bad |= dst[i] != (i >= 8 && i < 8 + N ? src[i - 8] : 1); \
	memset(dst, 1, sizeof dst); ms##N(dst + 8); \
	for (int i = 0; i < 160; i++) bad |= dst[i] != (i >= 8 && i < 8 + N ? 0x5a : 1); \
	memset(dst, 1, sizeof dst); mz##N(dst + 8); \
	for (int i = 0; i < 160; i++) bad |= dst[i] != (i >= 8 && i < 8 + N ? 0 : 1); \
	memset(dst, 1, sizeof dst); mm##N(dst + 8, src); \
	for (int i = 0; i < 160; i++) bad |= dst[i] != (i >= 8 && i < 8 + N ? src[i - 8] : 1);

int main(void)
{
	for (int i = 0; i < 128; i++)
		src[i] = (char)(i * 7 + 3);
	SIZES(T)
	memset(dst, 1, sizeof dst);
	src[5] = 0;
	bad |= sc(dst, src) != dst || dst[4] != src[4] || dst[5] != 0;
	return bad;
}
