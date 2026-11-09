#ifndef VCC_PP_H
#define VCC_PP_H
/*
 * vcc preprocessor: C (gnu11) translation phase 4, pull model.
 *
 * pp_next() hands out one fully macro-expanded token at a time.  Files come
 * from the token cache (lex.h) and are never copied: a file context and an
 * object-like macro body are (pointer, index) into the immutable token
 * arrays.  Only function-like expansions write tokens, into a per-TU arena
 * that is reset whenever the context stack is back at file level.
 *
 * Expansion follows clang's model exactly (it is the parity target): a
 * "disabled" bit per macro while its expansion is on the stack, a paint bit
 * (PF_NOEXP) on identifier tokens read while their macro was disabled, and
 * clang's argument pre-expansion, paste and whitespace rules.
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdarg.h>

#include "../lex/lex.h"

enum {
	P_DIRECTIVE = T_NKINDS,	/* a line to pass through: s = "pragma ...", "ident ..." */
};

enum {
	PF_NOEXP = 16,		/* painted: never expand (C11 6.10.3.4p2) */
	PF_NOPASTE = 32,	/* a ## that came from an argument */
	PF_MACRO = 64,		/* from a macro expansion (diagnostics) */
};

typedef struct ptok {
	const char *s;		/* spelling; raw (with splices) if TF_DIRTY */
	uint32_t len;
	uint32_t id;		/* interned id (T_IDENT) */
	uint32_t loc;		/* TU source location (file base + offset) */
	uint8_t kind, flags;	/* vtok_kind / P_*, TF_* | PF_* */
	uint16_t ctr;		/* pp internal: __COUNTER__ digits in the spelling (header cache) */
} ptok;

typedef struct pp_opts {
	const char *main;		/* the source file */
	const char *const *dirs;	/* search path: -iquote dirs, then -I, -isystem, -idirafter */
	int ndirs, nquote;		/* dirs[0..nquote) only for "" includes */
	int nsys;			/* dirs[nsys..) are system directories (0: none) */
	const char *predef;		/* "<built-in>": #define/#undef/#include lines, run first */
	const char *prefix_from, *prefix_to;	/* -fmacro-prefix-map (one) */
	const char *const *fwdirs;	/* framework dirs: <A/b.h> -> DIR/A.framework/Headers/b.h */
	int nfwdirs;
	int unsigned_char;
	const char *cwd;		/* relative paths are relative to this (NULL: process cwd) */
	int errfd;			/* diagnostics (0: stderr) */
	int asm_mode;			/* assembler-with-cpp: unknown directives pass, # need not stringify */
	int target;			/* PP_LINUX_AARCH64 or PP_DARWIN (column of the feature table) */
	int diag_color;			/* diagnostics: 1 colors, -1 none, 0 when errfd is a terminal */
	int diag_nocaret;		/* -fno-caret-diagnostics: no source lines */
	const char *const *wargs;	/* -W..., -w options in command-line order */
	int nwargs;
	int error_limit;		/* -ferror-limit=: 0 default (20), -1 none */
} pp_opts;

enum { PP_LINUX_AARCH64, PP_DARWIN };

typedef struct pp pp;

/* NULL if the main file cannot be read or has a fatal error (reported) */
pp *pp_open(const pp_opts *o);
void pp_close(pp *p);
/* fatal errors (missing #include) longjmp here instead of exiting */
void pp_set_fatal(pp *p, jmp_buf *jb);
/* the distinct files read so far, as spelled (for -MD dependency files) */
size_t pp_files(const pp *p, const char **names, size_t max);
/* the next token; T_EOF at the end (and ever after) */
void pp_next(pp *p, ptok *t);
/*
 * Spellings of tokens pp made (#x, a##b, __LINE__) live in an arena that
 * pp_next reuses once expansion is back at file level.  A consumer that
 * still holds earlier tokens when it asks for the next one (lookahead)
 * brackets that pp_next with pp_hold_tmp(p, 1) ... (p, 0).
 */
void pp_hold_tmp(pp *p, int on);
/*
 * -E: the whole TU as clang -E -P prints it (instead of pp_next), with the
 * header cache (VPP_NOHC=1: off).  *out stays valid until pp_close.
 * Returns the number of errors.
 */
int pp_text(pp *p, const char **out, size_t *len);
/* header cache counters (process-wide) */
void pp_hc_stats(char *buf, size_t n);
/* errors reported so far */
int pp_errors(const pp *p);

/* is loc in a system header */
int pp_in_system_header(const pp *p, uint32_t loc);
/* is loc in the main file (not a header, not <built-in>) */
int pp_in_main_file(const pp *p, uint32_t loc);
/* presumed file name, line and column of a location; 0 if it has none */
int pp_where(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col);
/* a diagnostic at loc ("error", "warning", "note", "fatal error": longjmp/exit) */
void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap);

/*
 * The same with clang's source snippet extras: ranges underlined with ~
 * (locations [b, e)), and fix-it hints (replace [b, e) with s; b == e
 * inserts) printed under the caret line.
 */
typedef struct pp_span { uint32_t b, e; } pp_span;
typedef struct pp_fixit { uint32_t b, e; const char *s; } pp_fixit;
typedef struct pp_dinfo {
	const char *what;
	uint32_t loc;
	const pp_span *r;
	int nr;
	const pp_fixit *fx;
	int nfx;
	/* the diagnosed token when it came from a macro expansion (loc: the expansion's): its spelling */
	const char *ms;
	uint32_t mlen;
} pp_dinfo;
void pp_vdiagx(pp *p, const pp_dinfo *d, const char *fmt, va_list ap);
__attribute__((format(printf, 3, 4)))
void pp_diagx(pp *p, const pp_dinfo *d, const char *fmt, ...);
/* a warning's state after the -W options: 0 off, 1 warning, 2 error; on: its default
   (flag without "-W"; groups: the warning's groups, NULL-terminated, e.g. "all") */
int pp_wstate(const pp *p, const char *flag, bool on, const char *const *groups);
/* report a warning of flag (clang's " [-Wflag]" / " [-Werror,-Wflag]" suffix); false if off */
__attribute__((format(printf, 6, 7)))
bool pp_warnx(pp *p, const char *flag, bool on, const char *const *groups, pp_dinfo *d, const char *fmt, ...);
/* -ferror-limit= as given (0: default, -1: none) */
int pp_error_limit(const pp *p);
/* "N errors generated." (clang's summary), if there were any */
void pp_diag_summary(pp *p);
/* warnings reported so far */
int pp_warnings(const pp *p);
/* the end of the token at loc (an identifier, number or punctuator; loc + 1 if unknown) */
uint32_t pp_tok_end(const pp *p, uint32_t loc);
/* the source text from loc b to loc e (one file), NULL if unknown */
const char *pp_src(const pp *p, uint32_t b, uint32_t e, size_t *len);

/*
 * Top-level regions (headers included from the main file, cached or being
 * recorded), for the front end's declaration cache.  Positions count the
 * tokens pp_next has delivered.  A region's start comes before its first
 * token, its end after its last; e identifies the cached entry (the same
 * pointer means the same tokens), NULL for a region that was not stored.
 */
typedef struct pp_topev {
	const void *e;
	uint64_t pos;
	uint32_t base;		/* location base of the region */
	unsigned ctr;		/* __COUNTER__ at its start */
	uint8_t end, hit;	/* hit: replayed (can be skipped) */
} pp_topev;
/* this TU's events so far; NULL once there were too many */
const pp_topev *pp_top_events(const pp *p, uint32_t *n);
/* tokens delivered so far; tokens pp_skip_top dropped */
uint64_t pp_ntok(const pp *p);
uint64_t pp_skipped(const pp *p);
/*
 * Drop the rest of the replayed region that the last event started, whose
 * first token was the last one delivered: as if it had been delivered and
 * the delivered count stood at its start.  0 if that is not the case.
 */
int pp_skip_top(pp *p);

/*
 * Lazy bodies straight from the header cache.  When the token pp_next just
 * delivered is a '{' replayed from a cached entry, and its matching '}' is in
 * the same entry's own tokens (no child entry in between), pp_lazy_skip moves
 * past the '}' as if the tokens had been delivered and fills z; pp_lazy_tok
 * then hands out '{' .. '}' again, later in the same TU (z is a cursor).
 * The entry is immutable and lives as long as the process.
 */
typedef struct pp_lazy {
	const void *e;
	uint32_t open, close;	/* own-token indices of '{' and '}' */
	uint32_t base;		/* location base of the replay */
	unsigned cbase;		/* __COUNTER__ at the entry's start */
	uint32_t ci;		/* the entry's first __COUNTER__ record after '{' */
	uint32_t k;		/* cursor: next token (pp_lazy_tok) */
	uint32_t rloc;		/* the '}': location and flags */
	uint8_t rflags;
} pp_lazy;
int pp_lazy_skip(pp *p, const ptok *lbrace, pp_lazy *z);
void pp_lazy_tok(pp *p, pp_lazy *z, ptok *t);

/* the token's spelling without splices; buf must hold t->len bytes */
const char *pp_spell(const ptok *t, char *buf, size_t *len);

#endif
