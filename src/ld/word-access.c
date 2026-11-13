/* Unaligned 32/64-bit loads and stores and the alignment helpers every other part of the linker uses. */

static inline uint32_t ld_u32_load(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void ld_u32_store(void *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void ld_u64_store(void *p, uint64_t v) { memcpy(p, &v, 8); }
static inline uint64_t ld_value_align_up(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) & ~(a - 1); }
/* smallest x >= v with x % a == skew % a (llvm::alignTo with skew) */
static inline uint64_t ld_value_align_skew(uint64_t v, uint64_t a, uint64_t skew)
{
	skew %= a;
	return ((v + a - 1 - skew) / a) * a + skew;
}
