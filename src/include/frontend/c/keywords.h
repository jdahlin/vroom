/*
 * The C keywords and the preprocessor's directive and builtin names, with
 * their identifier ids KW_*.  They are interned first, in this order, so the
 * preprocessor and parser can switch on an id.  Including this header
 * registers the table with the interner (a constructor), before any
 * vintern_init.
 */
#ifndef VCC_FRONTEND_C_KEYWORDS_H
#define VCC_FRONTEND_C_KEYWORDS_H

#include <stddef.h>

#include "common/intern.h"

#define KEYWORDS(X) \
	X(auto) X(break) X(case) X(char) X(const) X(continue) X(default) X(do) \
	X(double) X(else) X(enum) X(extern) X(float) X(for) X(goto) X(if) \
	X(inline) X(int) X(long) X(register) X(restrict) X(return) X(short) \
	X(signed) X(sizeof) X(static) X(struct) X(switch) X(typedef) X(union) \
	X(unsigned) X(void) X(volatile) X(while) X(_Alignas) X(_Alignof) \
	X(_Atomic) X(_Bool) X(_Generic) X(_Noreturn) X(_Static_assert) \
	X(_Thread_local) X(__attribute__) X(__attribute) X(asm) X(__asm__) \
	X(__asm) X(typeof) X(__typeof__) X(__typeof) X(typeof_unqual) \
	X(__typeof_unqual__) X(__inline__) X(__inline) X(__volatile__) \
	X(__volatile) X(__const__) X(__const) X(__signed__) X(__signed) \
	X(__restrict__) X(__restrict) X(__extension__) X(__alignof__) \
	X(__alignof) X(__auto_type) X(__label__) X(__builtin_va_arg) \
	X(__builtin_offsetof) X(__builtin_types_compatible_p) \
	X(__builtin_choose_expr) X(__builtin_bit_cast) X(__builtin_shufflevector) X(__builtin_convertvector) X(__int128) X(_BitInt) X(_ExtInt) X(_Float16) X(__bf16) X(__fp16) X(__float128) \
	X(__thread) X(__real__) X(__imag__) X(_Complex) X(__complex__) X(__complex) X(__real) X(__imag) X(static_assert) \
	X(bool) X(true) X(false) X(nullptr) X(alignof) X(alignas) X(constexpr) \
	X(thread_local) \
	/* preprocessor */ \
	X(define) X(undef) X(include) X(include_next) X(ifdef) X(ifndef) \
	X(elif) X(elifdef) X(elifndef) X(endif) X(line) X(error) X(warning) \
	X(pragma) X(defined) X(_Pragma) \
	X(__has_include) X(__has_include_next) X(__has_attribute) \
	X(__has_builtin) X(__has_feature) X(__has_extension) \
	X(__has_c_attribute) X(__has_cpp_attribute) X(__is_identifier) \
	X(__has_declspec_attribute) X(__FILE__) X(__LINE__) X(__COUNTER__) \
	X(__DATE__) X(__TIME__) X(__BASE_FILE__) X(__FILE_NAME__) \
	X(__INCLUDE_LEVEL__) X(__TIMESTAMP__) X(once)

enum {
	KW_NONE,
#define X(k) KW_##k,
	KEYWORDS(X)
#undef X
	KW_VA_ARGS,	/* __VA_ARGS__ */
	KW_VA_OPT,	/* __VA_OPT__ */
	KW_END
};
#define KW_COUNT (KW_END - 1)

static const char *const vkeyword_names[] = {
#define X(k) #k,
	KEYWORDS(X)
#undef X
	"__VA_ARGS__", "__VA_OPT__",
};

__attribute__((constructor)) static void vkeyword_register(void)
{
	vintern_preset(vkeyword_names, sizeof(vkeyword_names) / sizeof(vkeyword_names[0]));
}

#endif
