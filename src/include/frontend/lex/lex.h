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

enum lex_token_kind {
	LEX_TOKEN_EOF,
	LEX_TOKEN_IDENTIFIER,
	LEX_TOKEN_NUMBER,
	LEX_TOKEN_CHARACTER, LEX_TOKEN_WIDE_CHARACTER, LEX_TOKEN_UTF8_CHARACTER, LEX_TOKEN_UTF16_CHARACTER, LEX_TOKEN_UTF32_CHARACTER,
	LEX_TOKEN_STRING, LEX_TOKEN_WIDE_STRING, LEX_TOKEN_UTF8_STRING, LEX_TOKEN_UTF16_STRING, LEX_TOKEN_UTF32_STRING,
	LEX_TOKEN_UNKNOWN,
	/* punctuators */
	LEX_TOKEN_LEFT_SQUARE, LEX_TOKEN_RIGHT_SQUARE, LEX_TOKEN_LEFT_PAREN, LEX_TOKEN_RIGHT_PAREN, LEX_TOKEN_LEFT_BRACE, LEX_TOKEN_RIGHT_BRACE,
	LEX_TOKEN_PERIOD, LEX_TOKEN_ELLIPSIS, LEX_TOKEN_AMPERSAND, LEX_TOKEN_AMPERSAND_AMPERSAND, LEX_TOKEN_AMPERSAND_EQUAL, LEX_TOKEN_STAR, LEX_TOKEN_STAR_EQUAL,
	LEX_TOKEN_PLUS, LEX_TOKEN_PLUS_PLUS, LEX_TOKEN_PLUS_EQUAL, LEX_TOKEN_MINUS, LEX_TOKEN_ARROW, LEX_TOKEN_MINUS_MINUS, LEX_TOKEN_MINUS_EQUAL,
	LEX_TOKEN_TILDE, LEX_TOKEN_EXCLAMATION, LEX_TOKEN_EXCLAMATION_EQUAL, LEX_TOKEN_SLASH, LEX_TOKEN_SLASH_EQUAL, LEX_TOKEN_PERCENT,
	LEX_TOKEN_PERCENT_EQUAL, LEX_TOKEN_LESS, LEX_TOKEN_LESS_LESS, LEX_TOKEN_LESS_EQUAL, LEX_TOKEN_LESS_LESS_EQUAL, LEX_TOKEN_GREATER,
	LEX_TOKEN_GREATER_GREATER, LEX_TOKEN_GREATER_EQUAL, LEX_TOKEN_GREATER_GREATER_EQUAL, LEX_TOKEN_CARET, LEX_TOKEN_CARET_EQUAL,
	LEX_TOKEN_PIPE, LEX_TOKEN_PIPE_PIPE, LEX_TOKEN_PIPE_EQUAL, LEX_TOKEN_QUESTION, LEX_TOKEN_COLON, LEX_TOKEN_COLON_COLON, LEX_TOKEN_SEMICOLON,
	LEX_TOKEN_EQUAL, LEX_TOKEN_EQUAL_EQUAL, LEX_TOKEN_COMMA, LEX_TOKEN_HASH, LEX_TOKEN_HASH_HASH,
	LEX_TOKEN_KIND_COUNT
};

enum {
	LEX_TOKEN_FLAG_LINE_START = 1,	/* first token on a (physical, unspliced) line */
	LEX_TOKEN_FLAG_SPACE_BEFORE = 2,	/* preceded by whitespace or a comment */
	LEX_TOKEN_FLAG_SPLICED = 4,	/* contains a line splice: use lex_token_spell() */
	LEX_TOKEN_FLAG_RAW_STRING = 8,	/* raw string literal */
};

typedef struct lex_token {
	uint32_t off;	/* byte offset in the file */
	uint32_t len;	/* raw length in the file (splices included) */
	uint32_t id;	/* interned id (LEX_TOKEN_IDENTIFIER), else 0 */
	uint8_t kind;
	uint8_t flags;
	uint16_t _pad;
} lex_token;

typedef struct lex_file {
	const char *path;
	const char *src;	/* readable to at least src[size + 128], zero padded */
	uint32_t size;
	uint32_t ntok;		/* tokens, excluding the final LEX_TOKEN_EOF */
	const lex_token *tok;	/* ntok + 1 entries */
	void *pp;		/* preprocessor's per-file data (frontend/pp/file.c), set once with __atomic */
	char *fixdep;		/* utils/fixdep.c's CONFIG_ names in src, set once with __atomic */
	uint64_t dev, ino;	/* the file's identity (0, 0: unknown): #pragma once, as clang */
} lex_file;

/* clang's name for a token kind (as in -dump-raw-tokens) */
const char *lex_token_name(int kind);

/*
 * Lex src[0..size) into out (room for size + 2 tokens).  src must be
 * readable and zero up to src[size + 128].  scratch must hold
 * lex_scratch_size(size) bytes.  Returns the number of tokens, the final
 * LEX_TOKEN_EOF not counted.
 */
size_t lex_scratch_size(size_t size);
extern _Thread_local int lex_identifiers_not_interned;	/* this thread: no interning (ids 0) */
uint32_t lex_tokenize(const char *src, size_t size, lex_token *out, void *scratch);

/* cleaned spelling (splices removed) into buf; returns its length */
size_t lex_token_spell(const char *src, const lex_token *t, char *buf);

/*
 * Token cache: lex the file at path once per process; NULL if it cannot be
 * read (that is cached too).  Thread safe; concurrent callers for the same
 * path wait for the first.  The result is immutable and lives forever.
 * path must be readable to path[pathlen + 7] (it is hashed by words), here
 * and in lex_file_prefetch().
 */
const lex_file *lex_file_get(const char *path, size_t pathlen);
/* f with trigraphs replaced (C phase 1), lexed into a new uncached file;
 * f itself when it has none */
const lex_file *lex_file_trigraphs_replace(const lex_file *f);
/* C23: f with digit separators (1'000) dropped from pp-numbers, as lex_file_trigraphs_replace */
const lex_file *lex_file_digit_separators_remove(const lex_file *f);
const lex_file *lex_file_c89_convert(const lex_file *f);
const lex_file *lex_file_without_digraphs(const lex_file *f);
void lex_cache_flush(void);	/* only with no lookups in flight */
/* The cache epoch (vccd): vcache_new_epoch() makes every cached answer (files, directories, include
 * lookups) be checked against the file system once before it is used again; safe with lookups in flight. */
uint32_t vcache_epoch(void);
void vcache_new_epoch(void);
/*
 * Read-ahead hint: path will probably be needed.  I/O threads open and read
 * it, so that lex_file_get() only lexes.  Never required for correctness.
 */
void lex_file_prefetch(const char *path, size_t pathlen);
/*
 * Fuzzing and tests: when set, lex_file_get() asks only this (no cache, no file
 * system).  The files it returns must stay valid while a pp uses them.
 */
extern const lex_file *(*lex_file_get_hook)(const char *path, size_t pathlen);
int lex_file_load_uncached(const char *path, lex_file *f);	/* benchmarking only */

#endif
