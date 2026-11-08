#ifndef VCC_LEX_H
#define VCC_LEX_H
/*
 * vcc lexer: C (gnu11) preprocessing tokens.
 *
 * A file is lexed once per build into an immutable token array; the
 * preprocessor then only walks arrays.  The lexer works in two passes over
 * the (mmap'ed, read-only) source:
 *
 *   1. NEON classification: every 64-byte block yields three 64-bit masks
 *      (identifier chars, whitespace incl. newline, newline).
 *   2. a scalar token loop that finds the end of identifier and whitespace
 *      runs with ctz on those masks, and scans comments and literals with
 *      16-byte NEON searches.  Only line splices ("\\\n") and non-ASCII take
 *      a slow path.
 *
 * Tokens are 16 bytes.  Identifiers are interned (intern.h) while lexing, so
 * the preprocessor compares identifiers by id.
 */
#include <stddef.h>
#include <stdint.h>

enum vtok_kind {
	T_EOF,
	T_IDENT,
	T_NUMBER,
	T_CHAR, T_WCHAR, T_U8CHAR, T_U16CHAR, T_U32CHAR,
	T_STRING, T_WSTRING, T_U8STRING, T_U16STRING, T_U32STRING,
	T_UNKNOWN,
	/* punctuators */
	T_LSQUARE, T_RSQUARE, T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE,
	T_PERIOD, T_ELLIPSIS, T_AMP, T_AMPAMP, T_AMPEQ, T_STAR, T_STAREQ,
	T_PLUS, T_PLUSPLUS, T_PLUSEQ, T_MINUS, T_ARROW, T_MINUSMINUS, T_MINUSEQ,
	T_TILDE, T_EXCLAIM, T_EXCLAIMEQ, T_SLASH, T_SLASHEQ, T_PERCENT,
	T_PERCENTEQ, T_LESS, T_LESSLESS, T_LESSEQ, T_LESSLESSEQ, T_GREATER,
	T_GREATERGREATER, T_GREATEREQ, T_GREATERGREATEREQ, T_CARET, T_CARETEQ,
	T_PIPE, T_PIPEPIPE, T_PIPEEQ, T_QUESTION, T_COLON, T_COLONCOLON, T_SEMI,
	T_EQUAL, T_EQUALEQUAL, T_COMMA, T_HASH, T_HASHHASH,
	T_NKINDS
};

enum {
	TF_SOL = 1,	/* first token on a (physical, unspliced) line */
	TF_SPACE = 2,	/* preceded by whitespace or a comment */
	TF_DIRTY = 4,	/* contains a line splice: use vlex_spell() */
	TF_RAW = 8,	/* raw string literal */
};

typedef struct vtok {
	uint32_t off;	/* byte offset in the file */
	uint32_t len;	/* raw length in the file (splices included) */
	uint32_t id;	/* interned id (T_IDENT), else 0 */
	uint8_t kind;
	uint8_t flags;
	uint16_t _pad;
} vtok;

typedef struct vlex_file {
	const char *path;
	const char *src;	/* readable to at least src[size + 128], zero padded */
	uint32_t size;
	uint32_t ntok;		/* tokens, excluding the final T_EOF */
	const vtok *tok;	/* ntok + 1 entries */
	void *pp;		/* preprocessor's per-file data (pp.c), set once with __atomic */
} vlex_file;

/* clang's name for a token kind (as in -dump-raw-tokens) */
const char *vtok_name(int kind);

/*
 * Lex src[0..size) into out (room for size + 2 tokens).  src must be
 * readable and zero up to src[size + 128].  scratch must hold
 * vlex_scratch_size(size) bytes.  Returns the number of tokens, the final
 * T_EOF not counted.
 */
size_t vlex_scratch_size(size_t size);
extern _Thread_local int vlex_no_intern;	/* this thread: no interning (ids 0) */
uint32_t vlex(const char *src, size_t size, vtok *out, void *scratch);

/* cleaned spelling (splices removed) into buf; returns its length */
size_t vlex_spell(const char *src, const vtok *t, char *buf);

/*
 * Token cache: lex the file at path once per process; NULL if it cannot be
 * read (that is cached too).  Thread safe; concurrent callers for the same
 * path wait for the first.  The result is immutable and lives forever.
 * path must be readable to path[pathlen + 7] (it is hashed by words), here
 * and in vlex_prefetch().
 */
const vlex_file *vlex_get(const char *path, size_t pathlen);
/*
 * Read-ahead hint: path will probably be needed.  I/O threads open and read
 * it, so that vlex_get() only lexes.  Never required for correctness.
 */
void vlex_prefetch(const char *path, size_t pathlen);
/*
 * Fuzzing and tests: when set, vlex_get() asks only this (no cache, no file
 * system).  The files it returns must stay valid while a pp uses them.
 */
extern const vlex_file *(*vlex_get_hook)(const char *path, size_t pathlen);
int vlex_load_uncached(const char *path, vlex_file *f);	/* benchmarking only */

#endif
