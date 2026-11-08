/*
 * The reference lexer for fuzz-lex: lex.c built with VLEX_SCALAR, so every
 * token goes through its scalar handler and the NEON window path (masks,
 * branch-free token loop) is never taken.  Public symbols get a _ref suffix
 * so it links next to the real lex.c.
 */
#define VLEX_SCALAR 1
#define vlex vlex_ref
#define vlex_get vlex_get_ref
#define vlex_get_hook vlex_get_hook_ref
#define vlex_load_uncached vlex_load_uncached_ref
#define vlex_no_intern vlex_no_intern_ref
#define vlex_prefetch vlex_prefetch_ref
#define vlex_scratch_size vlex_scratch_size_ref
#define vlex_spell vlex_spell_ref
#define vtok_name vtok_name_ref
#include "../vcc/lex.c"
