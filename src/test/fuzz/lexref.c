/*
 * The reference lexer for fuzz-lex: lex.c built with VLEX_SCALAR, so every
 * token goes through its scalar handler and the NEON window path (masks,
 * branch-free token loop) is never taken.  Public symbols get a _ref suffix
 * so it links next to the real lex.c.
 */
#define VLEX_SCALAR 1
#define lex_tokenize vlex_ref
#define lex_file_get vlex_get_ref
#define lex_file_get_hook vlex_get_hook_ref
#define lex_cache_flush vlex_cache_flush_ref
#define vcache_epoch vcache_epoch_ref
#define vcache_new_epoch vcache_new_epoch_ref
#define lex_file_trigraphs_replace vlex_trigraphs_ref
#define lex_file_digit_separators_remove vlex_digitsep_ref
#define lex_file_c89_convert vlex_c89_ref
#define lex_file_without_digraphs vlex_nodigraphs_ref
#define lex_file_load_uncached vlex_load_uncached_ref
#define lex_identifiers_not_interned vlex_no_intern_ref
#define lex_file_prefetch vlex_prefetch_ref
#define lex_scratch_size vlex_scratch_size_ref
#define lex_token_spell vlex_spell_ref
#define lex_token_name vtok_name_ref
#include "../../frontend/lex/lex.c"
