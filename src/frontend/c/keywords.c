/*
 * Registers the C keyword names (keywords.h) with the identifier interner at
 * start-up, so that they take ids 1..KW_COUNT in order in every tool that
 * links the interner.
 */
#include <stddef.h>

#include "common/intern.h"
#include "frontend/c/keywords.h"

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
