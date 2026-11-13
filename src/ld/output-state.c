/* The state of the output image: file buffer, program headers, the ids of the synthetic sections, and new_isec(). */

/* ---------------- output state ---------------- */

static uint8_t *out;			/* output file image */
static uint64_t filesize, shoff;
static VVEC(Phdr) phdrs;
typedef struct { int first, last; bool lma_set, hdr; } PhdrSecs;	/* hdr: FILEHDR/PHDRS */
static VVEC(PhdrSecs) phsecs;		/* first/last osec of each phdr */
static VVEC(uint32_t) outsecs;		/* live output sections in header order */
static uint64_t ndyn;			/* RELATIVE relocations */
static uint32_t reladyn_isec = NONE, buildid_isec = NONE, comment_isec = NONE, got_isec = NONE;
static uint32_t ehhdr_isec = NONE;	/* --eh-frame-hdr */
static VVEC(uint32_t) ehfdes;		/* the FDE pieces in the output, for .eh_frame_hdr */
static VVEC(uint32_t) gotsyms;		/* .got slot -> G id (the globals, first) */
typedef struct { uint32_t obj, sym; } GotLoc;
static VVEC(GotLoc) gotlocs;		/* then the locals: slot gotsyms.n + i */
static uint32_t symtab_isec = NONE, strtab_isec = NONE, shstrtab_isec = NONE;
/* -shared: dynamic sections (lld's, for the vDSO; vmlinux discards them) */
static uint32_t dynsym_isec = NONE, dynstr_isec = NONE, hash_isec = NONE, gnuhash_isec = NONE;
static uint32_t versym_isec = NONE, verdef_isec = NONE, dynamic_isec = NONE;
static bool dyn_live;			/* .dynamic survived the script */
static uint32_t nobj_real;		/* objects excluding synthetic */

static uint32_t new_isec(const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ISec is = {.name = name, .kind = kind, .type = type, .flags = flags, .align = align,
		   .obj = NONE, .osec = NONE, .isd = NONE, .aux = NONE, .shndx = NONE};
	VVEC_PUSH(isecs, is, alloc);
	return isecs.n - 1;
}
