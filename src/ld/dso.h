#ifndef VCC_LD_DSO_H
#define VCC_LD_DSO_H
typedef struct {
	const char *path, *soname;
	const uint8_t *buf;
	size_t size;
	const elf_symbol *dsym;
	uint32_t ndsym;
	const char *dstr;
	uint64_t dstrsz;
	const uint16_t *versym;		/* per .dynsym entry, or NULL */
	const char **vername;		/* verdef index -> name (NULL: none) */
	uint32_t nver;
	bool as_needed;
	_Atomic bool used;		/* some symbol resolved to it */
} Dso;



/* name -> first default-version definition among the shared objects */
typedef struct { const char *name; uint32_t len, h, dso, idx; } DsoSym;
struct ld_dso_state {
	VVEC(Dso *) dsos;
	struct { DsoSym *v; uint32_t mask, n; } symbols;
};


typedef VVEC(ld_input_argument) ld_input_vector;
struct ld_context;
VPRIV void ld_dso_add(struct ld_context *, const char *, const uint8_t *, size_t, bool);
VPRIV const DsoSym *ld_dso_lookup(struct ld_context *, const char *);
VPRIV const char *ld_dso_vername(const Dso *, uint32_t);
VPRIV void ld_input_expand(struct ld_context *, ld_input_vector *, ld_input_argument, int);
VPRIV const char *ld_library_find(struct ld_context *, const char *);
#endif
