/* psabi.cpp: where -Wpsabi's sites are written, for the code generator's
   check (lower-internal.h; frontend/c/ast-builder.h) */
#include "lower-internal.h"
#include <ctype.h>

namespace cplusplus::lowering
{

/* the first token from at that is no function or storage specifier and no
   GNU attribute */
token_index c_lowering::type_specifier_token(token_index at) const
{
    static const char *const specifiers[] = {
        "inline",   "__inline",  "__inline__",   "static",
        "extern",   "constexpr", "consteval",    "virtual",
        "explicit", "friend",    "__extension__"};
    for (;;) {
        owned_buffer word;
        analysis.token_spelling(at, word.get());
        vbuffer_append(word.get(), "", 1);
        bool skipped = false;
        for (const char *specifier : specifiers)
            skipped |= !strcmp(word.data(), specifier);
        if (skipped) {
            at++;
            continue;
        }
        if (strcmp(word.data(), "__attribute__"))
            return at;
        unsigned depth = 0;
        do {
            owned_buffer next;
            analysis.token_spelling(++at, next.get());
            if (next.size() == 1 && next.data()[0] == '(')
                depth++;
            else if (next.size() == 1 && next.data()[0] == ')')
                depth--;
        } while (depth);
        at++;
    }
}

/* the first token in [first, end) that declares parameter e: its name with
   what may follow a parameter's name; first if none */
token_index c_lowering::parameter_token(entity_index e, token_index first,
                                        token_index end) const
{
    for (token_index t = first; e && t < end; t++) {
        if (tokens.identifier(t) != analysis.entity_table[e].name)
            continue;
        owned_buffer next;
        analysis.token_spelling(t + 1, next.get());
        if (next.size() == 1 && strchr(",)=[", next.data()[0]))
            return t;
    }
    return first;
}

/* [impl.clang] clang reports a result where the return type is written and
   a parameter at its name */
void c_lowering::psabi_definition(entity_index e, uint32_t s,
                                  const semantic_node &body,
                                  const owned_array<uint32_t> &parameters)
{
    const struct ast *a = ast_builder_ast(c);
    ast_qualified_type result =
        a->ty[AST_QUALIFIED_TYPE_INDEX(a->syms[s].ty)].base;
    bool wanted = ast_builder_psabi_wanted(c, result);
    for (uint32_t p : parameters)
        wanted |= ast_builder_psabi_wanted(c, a->syms[p].ty);
    if (!wanted || analysis.token_in_system_header(body.at))
        return;

    entity_index declared = e;
    if (analysis.entity_table[e].instantiation &&
        analysis.specialized_template(e))
        declared = analysis.specialized_template(e);
    /* the declaration that is the definition: the last one that starts at
       or before the body */
    token_index first = 0;
    SyntaxElement definition;
    for (uint32_t d = analysis.entity_table[declared].first_declaration; d;
         d = analysis.entity_table.declaration(d).next) {
        SyntaxElement n = analysis.entity_table.declaration(d).node;
        if (n.first_token() <= body.at && n.first_token() >= first) {
            first = n.first_token();
            definition = n;
        }
    }
    token_index end = body.at, result_at = body.at;
    if (first) {
        result_at = first;
        if (definition.is(syntax_kind::template_declaration))
            for (uint32_t i = 0; i < definition.size(); i++)
                if (definition[i].is(syntax_kind::function_definition) ||
                    definition[i].is(syntax_kind::member_declaration))
                    definition = definition[i];
        for (uint32_t i = 0; i < definition.size(); i++)
            if (definition[i].is(syntax_kind::decl_specifier_seq)) {
                result_at = type_specifier_token(definition[i].first_token());
                break;
            }
    } else
        first = body.at;

    ast_builder_psabi_site(c, AST_BUILDER_PSABI_RESULT, s,
                           tokens.location(result_at));
    const uint32_t *items = analysis.semantics.items(body.first);
    for (uint32_t i = 0; i < body.count && i < parameters.size(); i++) {
        token_index at = parameter_token(items[i], first, end);
        ast_builder_psabi_site(c, AST_BUILDER_PSABI_PARAMETER, parameters[i],
                               tokens.location(at));
    }
}

bool c_lowering::psabi_call_wanted(uint32_t call) const
{
    const struct ast *a = ast_builder_ast(c);
    const struct node &made = a->nodes[call];
    bool wanted = ast_builder_psabi_wanted(c, made.ty);
    for (uint32_t i = 0; i < made.c; i++)
        wanted |=
            ast_builder_psabi_wanted(c, a->nodes[a->lists[made.b + i]].ty);
    return wanted;
}

/* the declared name, where at is its initializer's '(' or '{' */
token_index c_lowering::name_before(token_index at) const
{
    owned_buffer word;
    analysis.token_spelling(at, word.get());
    if (at && word.size() == 1 && strchr("({", word.data()[0]))
        return at - 1;
    return at;
}

void c_lowering::psabi_constructor_call(token_index at, uint32_t call)
{
    if (psabi_call_wanted(call) && !analysis.token_in_system_header(at))
        ast_builder_psabi_site(c, AST_BUILDER_PSABI_CALL, call,
                               tokens.location(name_before(at)));
}

/* [impl.clang] at the member's name in a member access, else where the
   (qualified) name starts (an overloaded operator: where vcc's node is).  A call through a pointer is not checked */
void c_lowering::psabi_call(const semantic_node &n, uint32_t call)
{
    if (!psabi_call_wanted(call) ||
        record(node(n.a).type).kind == type_kind::pointer ||
        analysis.token_in_system_header(n.at))
        return;
    uint32_t named = n.a;
    if (node(named).kind == semantic_kind::conversion)
        named = node(named).a;
    token_index at = n.at;
    if (node(named).at)
        at = node(named).at;
    auto spelled = [&](token_index t, const char *text) {
        owned_buffer word;
        analysis.token_spelling(t, word.get());
        return word.size() == strlen(text) &&
               !memcmp(word.data(), text, word.size());
    };
    /* an operator expression: the node is at the operator, which is no name
       and no parenthesis */
    owned_buffer word;
    analysis.token_spelling(n.at, word.get());
    if (word.size() && ispunct((unsigned char)word.data()[0]) &&
        word.data()[0] != '_' && !spelled(n.at, "(") && !spelled(n.at, "{") &&
        !spelled(n.at, "::")) {
        at = n.at;
    } else {
        while (spelled(at + 1, ".") || spelled(at + 1, "->"))
            at += 2;
        while (at > 2 && spelled(at - 1, "::"))
            at -= 2;
    }
    ast_builder_psabi_site(c, AST_BUILDER_PSABI_CALL, call,
                           tokens.location(at));
}

} // namespace cplusplus::lowering
