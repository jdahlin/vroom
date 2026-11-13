/* The command-line options of one link (struct opt) and the directory descriptor paths are relative to. */

/* ---------------- options ---------------- */

typedef struct {
	const char *path;
	bool whole;
} ld_input_argument;

static struct {
	const char *out;
	const char *script;
	const char *soname;
	const char *entry;
	int orphan;			/* --orphan-handling: 0 place, 1 warn, 2 error */		/* -e/--entry: over the script's ENTRY() */
	const char *comment;		/* .comment linker string */
	const char *build_id;		/* NULL, "sha1", "md5", "fast" */
	const char *map;
	bool relocatable, shared, bsymbolic, notext, no_apply_dyn, no_undefined;
	bool discard_locals, discard_all, strip_debug, pic_veneer, nmagic;
	bool stubs;			/* define unresolved symbols as -O0 stubs */
	bool relax;
	uint64_t max_page;
	VVEC(ld_input_argument) inputs;
	VVEC(const char *) libdirs;
	bool bstatic;
	bool gc, print_gc;		/* --gc-sections, --print-gc-sections */
	bool norelro;			/* -z norelro: no PT_GNU_RELRO */
	bool eh_frame_hdr;		/* --eh-frame-hdr: .eh_frame_hdr and PT_GNU_EH_FRAME */
} opt;

static int dirfd_base = AT_FDCWD;
