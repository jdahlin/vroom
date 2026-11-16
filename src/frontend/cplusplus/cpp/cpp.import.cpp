/*
 * cpp.import.cpp: [cpp.import] Header unit importation.
 *
 *     pp-import:
 *         'export'_opt 'import' header-name pp-tokens_opt ';' new-line
 *         'export'_opt 'import' header-name-tokens pp-tokens_opt ';' new-line
 *         'export'_opt 'import' pp-tokens ';' new-line
 *
 * [cpp.import]/4: "In all three forms of pp-import, the import and export
 * (if it exists) preprocessing tokens are replaced by the import-keyword
 * and export-keyword preprocessing tokens respectively."  Header units
 * themselves (importing their macros, [cpp.import]/5-7) are not supported:
 * the directive's tokens pass to phase 7, which rejects the import.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* [cpp.pre]/1: "an import preprocessing token immediately followed on the
   same logical line by a header-name, <, identifier, string-literal, or :
   preprocessing token" */
static bool begins_import_directive(const preprocessing_token &next)
{
    return !next.at_start_of_line &&
           (next.kind == preprocessing_token_kind::header_name ||
            is_identifier(next) || is_string_literal(next) ||
            is_operator(next, preprocessing_op_or_punc::LESS) ||
            is_operator(next, preprocessing_op_or_punc::COLON));
}

/*
 * An identifier read from a source file at the start of a line in C++20
 * and later: true if it begins a pp-module or a pp-import, and then it has
 * become the module-keyword, import-keyword or export-keyword.  The token
 * after it is read again next (a header-name after import formed as
 * [lex.pptoken]/3.3 says).
 */
bool preprocessor::module_or_import_directive(preprocessing_token &first)
{
    owned_buffer s;
    files.spelling(first, s.get());
    auto is = [&](const char *word) {
        return s.size() == strlen(word) && !memcmp(s.data(), word, s.size());
    };
    bool is_export = is("export"), is_import = is("import"),
         is_module = is("module");
    if (!is_export && !is_import && !is_module)
        return false;
    if (contexts.size() || names_object_like_macro(first))
        return false;
    inclusion &in = inclusions.back();
    preprocessing_token next =
        in.has_lookahead ? in.lookahead : files.next(*in.file, is_import);
    in.has_lookahead = false;
    bool directive = false;
    if (is_import)
        directive = begins_import_directive(next);
    else if (is_module)
        directive = begins_module_directive(next);
    else if (is_identifier(next) && !next.at_start_of_line) {
        /* export followed by one of the two preceding forms */
        preprocessing_token second = next;
        bool inner = module_or_import_directive(second);
        /* the import or module token is read next, before the token the
           inner call read after it */
        push_tokens(&second, 1, -1, false);
        if (inner)
            first.kind = preprocessing_token_kind::export_keyword;
        return inner;
    }
    unread_file_token(next);
    if (!directive)
        return false;
    first.kind = is_import ? preprocessing_token_kind::import_keyword
                           : preprocessing_token_kind::module_keyword;
    return true;
}

} // namespace cplusplus
