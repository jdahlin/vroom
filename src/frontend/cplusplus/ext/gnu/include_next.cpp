/*
 * include_next.cpp: [ext:gnu_include_next] (GNU cpp manual 2.7, Wrapper
 * Headers):
 *
 *     control-line:
 *         # include_next pp-tokens new-line
 *
 * searches for the header from the directory
 * after the one the including file was found in, so that a header can wrap
 * the one of the same name later in the search.  Also clang's
 * __has_include_next ([ext:clang_has_include_next], in
 * preprocessor::has_include).
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

/* the place in the search after which #include_next looks (-1: the whole
   search, with clang's warning, in the main file or for a file not found
   in the search) */
int32_t preprocessor::include_next_start(const preprocessing_token &name)
{
    if (inclusions.size() <= 1) {
        diagnostics.issue(diagnostic_id::pp_include_next_in_primary,
                          name.location);
        return -1;
    }
    int32_t d = inclusions.back().directory;
    if (d < 0)
        diagnostics.issue(diagnostic_id::pp_include_next_absolute_path,
                          name.location);
    return d;
}

} // namespace cplusplus
