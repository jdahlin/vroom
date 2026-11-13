/* The state of the output image: file buffer, program headers, the ids of the synthetic sections, and ld_input_section_create(). */

/* ---------------- output state ---------------- */

static uint8_t *out;			/* output file image */
static uint64_t filesize, shoff;
static VVEC(elf_program_header) phdrs;
typedef struct { int first, last; bool lma_set, hdr; } ld_segment_sections;	/* hdr: FILEHDR/PHDRS */
static VVEC(ld_segment_sections) phsecs;		/* first/last osec of each phdr */
static VVEC(uint32_t) outsecs;		/* live output sections in header order */
static uint64_t ndyn;			/* RELATIVE relocations */
static uint32_t reladyn_isec = LD_NONE, buildid_isec = LD_NONE, comment_isec = LD_NONE, got_isec = LD_NONE;
static uint32_t ehhdr_isec = LD_NONE;	/* --eh-frame-hdr */
static VVEC(uint32_t) ehfdes;		/* the FDE pieces in the output, for .eh_frame_hdr */
static VVEC(uint32_t) gotsyms;		/* .got slot -> ld_global_symbol id (the globals, first) */
typedef struct { uint32_t obj, sym; } ld_got_location;
static VVEC(ld_got_location) gotlocs;		/* then the locals: slot gotsyms.n + i */
static uint32_t symtab_isec = LD_NONE, strtab_isec = LD_NONE, shstrtab_isec = LD_NONE;
/* -shared: dynamic sections (lld's, for the vDSO; vmlinux discards them) */
static uint32_t dynsym_isec = LD_NONE, dynstr_isec = LD_NONE, hash_isec = LD_NONE, gnuhash_isec = LD_NONE;
static uint32_t versym_isec = LD_NONE, verdef_isec = LD_NONE, dynamic_isec = LD_NONE;
static bool dyn_live;			/* .dynamic survived the script */
static uint32_t nobj_real;		/* objects excluding synthetic */

static uint32_t ld_input_section_create(const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ld_input_section is = {.name = name, .kind = kind, .type = type, .flags = flags, .align = align,
		   .obj = LD_NONE, .osec = LD_NONE, .isd = LD_NONE, .aux = LD_NONE, .shndx = LD_NONE};
	VVEC_PUSH(isecs, is, ld_region_alloc);
	return isecs.n - 1;
}
