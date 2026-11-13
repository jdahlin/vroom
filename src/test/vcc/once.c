/* #pragma once is by file identity, as clang: ./, dir/../ and a symlink are
   the same file (TODO.md tier 3) */
#include "once_inc.h"
#include "once_inc.h"
#include "once_inc.h"
#include "once_sym.h"

int main(void)
{
	return once_count != 1;
}
