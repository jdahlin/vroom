/*
 * cpp.rescan.cpp: [cpp.rescan] Rescanning and further replacement.
 *
 * [cpp.rescan]/1: "After all parameters in the replacement list have been
 * substituted and # and ## processing has taken place, all placemarker
 * preprocessing tokens are removed.  Then the resulting preprocessing token
 * sequence is rescanned, along with all subsequent preprocessing tokens of
 * the source file, for more macro names to replace."
 *
 * The resulting sequence is a token context, read before the rest of the
 * source file; contexts nest as replacements do.  While a context is read,
 * the macro it replaced is being replaced: [cpp.rescan]/3 "If the name of
 * the macro being replaced is found during this scan of the replacement list
 * (not including the rest of the source file's preprocessing tokens), it is
 * not replaced. Furthermore, if any nested replacements encounter the name
 * of the macro being replaced, it is not replaced.  These nonreplaced macro
 * name preprocessing tokens are no longer available for further replacement
 * even if they are later (re)examined in contexts in which that macro name
 * preprocessing token would otherwise have been replaced."  A context ends,
 * and its macro may be replaced again, when a token after its last is read.
 *
 * A barrier context holds the tokens of a macro argument ([cpp.subst]/1:
 * replaced "as if they formed the rest of the preprocessing file with no
 * other preprocessing tokens being available") or of a directive's operands:
 * reading stops at its end.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::push_tokens(const preprocessing_token *tokens, size_t count,
                               int32_t macro, bool barrier)
{
    uint32_t begin = (uint32_t)context_tokens.size();
    preprocessing_token *to = context_tokens.extend(count);
    for (size_t i = 0; i < count; i++)
        to[i] = tokens[i];
    contexts.append(
        {begin, begin + (uint32_t)count, begin, macro, barrier, false});
    if (macro >= 0)
        macros[macro].being_replaced = true;
}

void preprocessor::pop_context()
{
    token_context c = contexts[contexts.size() - 1];
    if (c.macro >= 0)
        macros[c.macro].being_replaced = false;
    if (c.trailing_space && !c.barrier) {
        pending_flags = true;
        pending_white_space = true;
    }
    context_tokens.resize(c.begin);
    contexts.resize(contexts.size() - 1);
}

/* the next preprocessing token, no macro replaced: from the innermost
   context, or from the source file; false at a barrier's end */
bool preprocessor::read_unexpanded(preprocessing_token &t)
{
    while (!contexts.empty()) {
        token_context &c = contexts[contexts.size() - 1];
        if (c.next < c.end) {
            t = context_tokens[c.next++];
            read_from_file = false;
            if (is_identifier(t) && !t.not_replaceable) {
                int32_t m = identifiers[identifier(t)].macro;
                if (m >= 0 && macros[m].being_replaced)
                    t.not_replaceable = true;
            }
            return true;
        }
        if (c.barrier)
            return false;
        pop_context();
    }
    t = read_file_token();
    if (t.kind == preprocessing_token_kind::placemarker)
        return read_unexpanded(t); /* a directive made tokens to read */
    read_from_file = true;
    return true;
}

/* the same, macros replaced */
bool preprocessor::read_expanded(preprocessing_token &t)
{
    for (;;) {
        if (!read_unexpanded(t))
            return false;
        if (is_identifier(t) && !t.not_replaceable) {
            int32_t m = identifiers[identifier(t)].macro;
            if (m >= 0 && replace_macro(t, m))
                continue;
        }
        return true;
    }
}

/* [cpp.replace]/12: "followed by a ( as the next preprocessing token"; a
   token that is not one is read again next */
bool preprocessor::peek_left_parenthesis()
{
    preprocessing_token t;
    /* [impl.clang] the ( is not looked for past the end of an included
       file */
    stop_at_file_end = true;
    bool more = read_unexpanded(t);
    stop_at_file_end = false;
    if (!more)
        return false;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return true;
    if (read_from_file)
        unread_file_token(t);
    else
        push_tokens(&t, 1, -1, false);
    return false;
}

/*
 * Replaces the invocation that starts with a macro name: [cpp.replace]/10
 * "causes each subsequent instance of the macro name to be replaced by the
 * replacement list"; false if the name is not replaced (a function-like
 * macro's name without a ( after it).
 */
bool preprocessor::replace_macro(preprocessing_token &name, int32_t index)
{
    const macro_definition m = macros[index];
    if (m.being_replaced) {
        name.not_replaceable = true;
        return false;
    }
    if (m.dynamic != dynamic_macro::none)
        return replace_dynamic(name, m);

    owned_array<preprocessing_token> result;
    trailing_space = false;
    if (!m.function_like) {
        owned_array<argument> none;
        owned_array<preprocessing_token> no_tokens;
        substitute(name, m, none, no_tokens, result);
    } else {
        if (!peek_left_parenthesis())
            return false;
        owned_array<argument> arguments;
        owned_array<preprocessing_token> argument_tokens;
        preprocessing_token right_parenthesis;
        /* [impl.clang] after an invocation that is in error, the name is
           left as it is and the arguments read are dropped */
        if (!collect_arguments(name, m, arguments, argument_tokens,
                               &right_parenthesis)) {
            name.not_replaceable = true;
            return false;
        }
        substitute(name, m, arguments, argument_tokens, result);
    }
    macros[index].used = true;
    finish_replacement(name, result);
    push_tokens(result.data(), result.size(), index, false);
    if (!result.empty())
        contexts.back().trailing_space = trailing_space;
    return true;
}

/* the replacement takes the place of the invocation: its first token the
   name's line start and white space, every token the invocation's place
   (clang's expansion location) */
void preprocessor::finish_replacement(const preprocessing_token &name,
                                      owned_array<preprocessing_token> &result)
{
    source_location expansion = name.expansion ? name.expansion : name.location;
    size_t kept = 0;
    for (size_t i = 0; i < result.size(); i++) {
        /* [cpp.rescan]/1: "all placemarker preprocessing tokens are
           removed" */
        if (result[i].kind == preprocessing_token_kind::placemarker)
            continue;
        preprocessing_token t = result[i];
        t.expansion = expansion;
        /* [cpp.concat]/5 Example 2: a ## that a replacement produces "is not
           the ## operator" (nor a token of phase 7: clang's unknown) */
        if (is_operator(t, preprocessing_op_or_punc::HASH_HASH))
            t.kind = preprocessing_token_kind::other_character;
        result[kept++] = t;
    }
    result.resize(kept);
    if (kept) {
        result[0].at_start_of_line = name.at_start_of_line;
        result[0].after_white_space = name.after_white_space;
    } else {
        pending_flags = true;
        pending_start_of_line = name.at_start_of_line;
        pending_white_space = name.after_white_space || trailing_space;
    }
}

} // namespace cplusplus
