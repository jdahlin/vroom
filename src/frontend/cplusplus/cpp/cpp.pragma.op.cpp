/*
 * cpp.pragma.op.cpp: [cpp.pragma.op] Pragma operator.
 *
 *     '_Pragma' '(' string-literal ')'
 *
 * [cpp.pragma.op]/1: "The string-literal is destringized by deleting the L
 * prefix, if present, deleting the leading and trailing double-quotes,
 * replacing each escape sequence \" by a double-quote, and replacing each
 * escape sequence \\ by a single backslash.  The resulting sequence of
 * characters is processed through translation phase 3 to produce
 * preprocessing tokens that are executed as if they were the pp-tokens in a
 * pragma directive.  The original four preprocessing tokens in the unary
 * operator expression are removed."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

bool preprocessor::pragma_operator(const preprocessing_token &name)
{
    preprocessing_token t[3];
    for (int i = 0; i < 3; i++) {
        bool more = read_expanded(t[i]);
        bool ok = false;
        if (!more)
            ok = false;
        else if (i == 0)
            ok = is_operator(t[0], preprocessing_op_or_punc::LEFT_PARENTHESIS);
        else if (i == 1)
            ok = t[1].kind == preprocessing_token_kind::string_literal &&
                 (t[1].prefix == encoding_prefix::none ||
                  t[1].prefix == encoding_prefix::L) &&
                 !t[1].raw;
        else
            ok = is_operator(t[2], preprocessing_op_or_punc::RIGHT_PARENTHESIS);
        if (!ok) {
            source_location at = name.location;
            if (more)
                at = t[i].location;
            diagnostics.issue(diagnostic_id::err__Pragma_malformed, at);
            if (more && t[i].kind != preprocessing_token_kind::end_of_file)
                push_tokens(&t[i], 1, -1, false);
            else if (more)
                unread_file_token(t[i]);
            return true;
        }
    }
    owned_buffer s, text;
    files.spelling(t[1], s.get());
    /* after the L prefix, if any, and the opening quote */
    size_t begin = 1, end = s.size() - 1;
    if (s.data()[0] == 'L')
        begin = 2;
    for (size_t i = begin; i < end; i++) {
        char c = s.data()[i];
        if (c == '\\' && i + 1 < end &&
            (s.data()[i + 1] == '"' || s.data()[i + 1] == '\\'))
            c = s.data()[++i];
        vbuffer_append(text.get(), &c, 1);
    }
    source_files::entry &e =
        files.add_text("<scratch space>", text.data(), text.size(), true);
    owned_array<preprocessing_token> tokens;
    source_location expansion = name.location;
    if (name.expansion)
        expansion = name.expansion;
    for (preprocessing_token p = files.next(e);
         p.kind != preprocessing_token_kind::end_of_file; p = files.next(e)) {
        p.expansion = expansion;
        tokens.append(p);
    }
    pragma_end = t[2].location;
    execute_pragma(tokens.data(), tokens.size(), name.location);
    /* [impl.clang] the pragma is a line of its own: the next token starts
       a line */
    pending_flags = true;
    pending_start_of_line = true;
    pending_white_space = false;
    return true;
}

} // namespace cplusplus
