/*
 * parser.cpp: what the parser of every clause uses: the tokens, how a
 * function builds its node, tentative parsing, and the scaffolding for the
 * parts of the grammar that are not written yet (doc/PLAN-cxx-parse.md).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

parser::parser(token_stream &tokens, syntax_tree &tree, declared_names &names,
               diagnostic_consumer &diagnostics)
    : tokens(tokens), tree(tree), names(names), diagnostics(diagnostics)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    auto index = [&](const char *spelling) {
        return lookup_keyword(b, spelling, strlen(spelling));
    };
    keyword.namespace_ = index("namespace");
    keyword.inline_ = index("inline");
    keyword.extern_ = index("extern");
    keyword.export_ = index("export");
    keyword.template_ = index("template");
    keyword.class_ = index("class");
    keyword.struct_ = index("struct");
    keyword.union_ = index("union");
    keyword.enum_ = index("enum");
    keyword.operator_ = index("operator");
    keyword.requires_ = index("requires");
    keyword.try_ = index("try");
    keyword.catch_ = index("catch");
}

bool parser::at(preprocessing_op_or_punc op, unsigned ahead)
{
    return is_operator(peek(ahead), op);
}

bool parser::at_keyword(int keyword_index, unsigned ahead)
{
    const token &t = peek(ahead);
    return keyword_index >= 0 && t.kind == token_kind::keyword &&
           t.keyword == keyword_index;
}

node_index parser::finish_node(syntax_kind kind, size_t mark,
                               token_index first_token)
{
    node_index n = tree.make(kind, pending.data() + mark, pending.size() - mark,
                             first_token, position);
    pending.resize(mark);
    return n;
}

parser_statistics parser::statistics()
{
    parser_statistics s = counters;
    s.tokens = tokens.made();
    s.nodes = tree.size() - 1;
    s.lookups = names.lookups.size();
    s.unparsed = tree.count(syntax_kind::unparsed);
    s.erroneous = tree.count(syntax_kind::erroneous);
    return s;
}

parser::tentative_parse::tentative_parse(parser &p)
    : p(p), position(p.position), pending(p.pending.size()),
      nodes(p.tree.size()), messages(p.diagnostics.messages.size()),
      errors(p.diagnostics.error_count)
{
    p.counters.tentative_parses++;
}

parser::tentative_parse::~tentative_parse()
{
    if (committed)
        return;
    p.counters.rewinds++;
    p.counters.rewound_tokens += p.position - position;
    p.position = position;
    p.pending.resize(pending);
    p.tree.truncate(nodes);
    p.diagnostics.messages.resize(messages);
    p.diagnostics.error_count = errors;
}

void parser::tentative_parse::commit()
{
    committed = true;
}

namespace
{

/* the bracket that closes an opening one ([lex.operators]; none: count) */
preprocessing_op_or_punc closing_bracket(const token &t)
{
    if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return preprocessing_op_or_punc::RIGHT_PARENTHESIS;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET))
        return preprocessing_op_or_punc::RIGHT_BRACKET;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACE))
        return preprocessing_op_or_punc::RIGHT_BRACE;
    return preprocessing_op_or_punc::count;
}

bool is_closing_bracket(const token &t)
{
    return is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE);
}

const char *bracket_spelling(preprocessing_op_or_punc op)
{
    switch (op) {
    case preprocessing_op_or_punc::LEFT_PARENTHESIS:
        return "(";
    case preprocessing_op_or_punc::RIGHT_PARENTHESIS:
        return ")";
    case preprocessing_op_or_punc::LEFT_BRACKET:
        return "[";
    case preprocessing_op_or_punc::RIGHT_BRACKET:
        return "]";
    case preprocessing_op_or_punc::LEFT_BRACE:
        return "{";
    default:
        return "}";
    }
}

preprocessing_op_or_punc opening_bracket(preprocessing_op_or_punc closing)
{
    switch (closing) {
    case preprocessing_op_or_punc::RIGHT_PARENTHESIS:
        return preprocessing_op_or_punc::LEFT_PARENTHESIS;
    case preprocessing_op_or_punc::RIGHT_BRACKET:
        return preprocessing_op_or_punc::LEFT_BRACKET;
    default:
        return preprocessing_op_or_punc::LEFT_BRACE;
    }
}

} // namespace

/*
 * Scaffolding (doc/PLAN-cxx-parse.md, "opaque"): the tokens of one
 * declaration, kept as an unparsed node until the step that owns its
 * grammar is written.  Where the declaration ends is found without parsing
 * it: brackets are balanced as in [dcl.attr.grammar]'s
 *
 *     balanced-token:
 *         ( balanced-token-seq_opt )
 *         [ balanced-token-seq_opt ]
 *         { balanced-token-seq_opt }
 *         any token other than a parenthesis, a bracket, or a brace
 *
 * and outside all brackets the declaration ends with its ';', or with the
 * '}' of a body: a function-body ([dcl.fct.def.general]), a
 * namespace-body ([namespace.def]), the braces of a linkage-specification
 * ([dcl.link]) or of an export-declaration ([module.interface]).  A '{'
 * that is none of those (a class-specifier's, an enum-specifier's, a
 * braced-init-list's, a requirement-body's) is followed to the ';'.
 *
 * Which '{' is a body is a guess from the tokens before it, because the
 * grammar that decides is not written yet.  Each step replaces the guess
 * for its productions with the parse.
 */
node_index parser::unparsed_declaration()
{
    token_index first = position;
    bool erroneous = false;
    /* the open brackets, innermost last, and how many of them are '{' */
    owned_array<token_index> open;
    unsigned open_braces = 0;
    /* outside all brackets: */
    bool equal = false;            /* an '=' was seen: an initializer follows */
    bool parameters = false;       /* a ')' was seen: a function declarator */
    bool class_key = false;        /* class, struct, union or enum was seen */
    bool mem_initializers = false; /* the ':' of a ctor-initializer was seen */
    /* the open '(' follows requires; the ')' of requires ( ... ) was last */
    bool requires_group = false, requires_parameters = false;
    /* the '{' being closed is a body: the declaration ends with it */
    bool body = false;
    /* template < ... >: the '<' that are open, so that an '=' or a class-key
       of a template-parameter is not taken for the declaration's */
    unsigned angles = 0;
    bool template_head = false;
    /* namespace-body, or the braces of extern "C" and export */
    bool block_of_declarations =
        at_keyword(keyword.namespace_) ||
        (at_keyword(keyword.inline_) && at_keyword(keyword.namespace_, 1));

    for (;;) {
        const token &t = peek();
        if (at_end()) {
            if (erroneous) {
                /* [intro.compliance]/2.2: "at least one diagnostic
                   message": the declaration has its one */
            } else if (!open.empty()) {
                const token &opener = tokens[open.back()];
                diagnostics.issue(diagnostic_id::parse_expected_closing,
                                  tokens.location(position))
                    << bracket_spelling(closing_bracket(opener))
                    << bracket_spelling(
                           preprocessing_op_or_punc_primary(opener.op_or_punc))
                    << static_cast<int64_t>(tokens.position(open.back()).line);
                erroneous = true;
            } else if (position > first) {
                diagnostics.issue(
                    diagnostic_id::parse_expected_end_of_declaration,
                    tokens.location(position));
                erroneous = true;
            }
            break;
        }
        bool outside = open.empty();
        if (closing_bracket(t) != preprocessing_op_or_punc::count) {
            if (outside &&
                is_operator(t, preprocessing_op_or_punc::LEFT_BRACE)) {
                const token &before = tokens[position - 1];
                bool after_requires =
                    position > first && (before.kind == token_kind::keyword &&
                                         before.keyword == keyword.requires_);
                bool after_group =
                    position > first &&
                    (is_operator(before,
                                 preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                     is_operator(before,
                                 preprocessing_op_or_punc::RIGHT_BRACE));
                if (after_requires || requires_parameters)
                    body = false; /* requirement-body ([expr.prim.req]) */
                else if (mem_initializers)
                    /* [class.base.init]: mem-initializer-id braced-init-list,
                       or after the last mem-initializer the function-body */
                    body = after_group;
                else if (equal)
                    body = false; /* brace-or-equal-initializer */
                else if (block_of_declarations)
                    body = true;
                else if (class_key && !parameters)
                    body = false; /* class-specifier, enum-specifier */
                else
                    body = parameters; /* else: T x{...} */
            }
            if (outside) {
                /* [expr.prim.req]: requires requirement-parameter-list_opt
                   requirement-body: its ( ) is no function declarator's */
                requires_group =
                    position > first &&
                    tokens[position - 1].kind == token_kind::keyword &&
                    tokens[position - 1].keyword == keyword.requires_;
                requires_parameters = false;
            }
            if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACE))
                open_braces++;
            open.append(position);
            consume();
            continue;
        }
        if (is_closing_bracket(t)) {
            preprocessing_op_or_punc closing =
                preprocessing_op_or_punc_primary(t.op_or_punc);
            if (outside) {
                /* the '}' of an enclosing region is not this declaration's:
                   nothing encloses a declaration of the translation unit */
                diagnostics.issue(diagnostic_id::parse_unmatched_closing,
                                  tokens.location(position))
                    << bracket_spelling(closing)
                    << bracket_spelling(opening_bracket(closing));
                erroneous = true;
                consume();
                break;
            }
            if (closing_bracket(tokens[open.back()]) != closing) {
                /* the innermost bracket is not closed here: if an outer one
                   is, the inner ones end with it; else the token is skipped */
                size_t match = open.size();
                for (size_t i = open.size(); i-- > 0;)
                    if (closing_bracket(tokens[open[i]]) == closing) {
                        match = i;
                        break;
                    }
                const token &opener = tokens[open.back()];
                if (!erroneous)
                    diagnostics.issue(diagnostic_id::parse_expected_closing,
                                      tokens.location(position))
                        << bracket_spelling(closing_bracket(opener))
                        << bracket_spelling(preprocessing_op_or_punc_primary(
                               opener.op_or_punc))
                        << static_cast<int64_t>(
                               tokens.position(open.back()).line);
                erroneous = true;
                if (match == open.size()) {
                    consume();
                    continue;
                }
                open.resize(match + 1);
            }
            open.resize(open.size() - 1);
            open_braces = 0;
            for (token_index opener : open)
                if (is_operator(tokens[opener],
                                preprocessing_op_or_punc::LEFT_BRACE))
                    open_braces++;
            consume();
            if (!open.empty())
                continue;
            if (closing == preprocessing_op_or_punc::RIGHT_PARENTHESIS) {
                if (requires_group)
                    requires_parameters = true;
                else
                    parameters = true;
                continue;
            }
            if (closing != preprocessing_op_or_punc::RIGHT_BRACE || !body)
                continue;
            /* [except.pre]: function-try-block: the handler-seq follows the
               compound-statement */
            if (at_keyword(keyword.catch_))
                continue;
            break;
        }
        if (!outside && !open_braces &&
            is_operator(t, preprocessing_op_or_punc::SEMICOLON)) {
            /* a ';' inside ( ) or [ ] with no { } between: no production
               has one there, so the bracket was not closed and the
               declaration ends here */
            if (!erroneous) {
                const token &opener = tokens[open.back()];
                diagnostics.issue(diagnostic_id::parse_expected_closing,
                                  tokens.location(position))
                    << bracket_spelling(closing_bracket(opener))
                    << bracket_spelling(
                           preprocessing_op_or_punc_primary(opener.op_or_punc))
                    << static_cast<int64_t>(tokens.position(open.back()).line);
            }
            erroneous = true;
            consume();
            break;
        }
        if (outside) {
            if (is_operator(t, preprocessing_op_or_punc::SEMICOLON)) {
                consume();
                break;
            }
            requires_parameters = false;
            if (template_head) {
                /* template < template-parameter-list >: to its '>' */
                if (is_operator(t, preprocessing_op_or_punc::LESS))
                    angles++;
                else if (is_operator(t, preprocessing_op_or_punc::GREATER))
                    angles--;
                else if (is_operator(
                             t, preprocessing_op_or_punc::GREATER_GREATER)) {
                    if (angles >= 2)
                        angles -= 2;
                    else
                        angles = 0;
                }
                consume();
                if (!angles)
                    template_head = false;
                continue;
            }
            if (at_keyword(keyword.template_) &&
                at(preprocessing_op_or_punc::LESS, 1)) {
                template_head = true;
                angles = 0;
                consume();
                continue;
            }
            if (is_operator(t, preprocessing_op_or_punc::EQUAL)) {
                /* operator= is a name, not an initializer */
                if (!(position > first &&
                      tokens[position - 1].kind == token_kind::keyword &&
                      tokens[position - 1].keyword == keyword.operator_))
                    equal = true;
            } else if (is_operator(t, preprocessing_op_or_punc::COLON)) {
                if (parameters && !equal)
                    mem_initializers = true;
            } else if (t.kind == token_kind::keyword) {
                if (t.keyword == keyword.class_ ||
                    t.keyword == keyword.struct_ ||
                    t.keyword == keyword.union_ || t.keyword == keyword.enum_)
                    class_key = true;
                else if (t.keyword == keyword.namespace_ && !equal)
                    /* [namespace.def]: inline_opt namespace ... {, also
                       after export ([module.interface]) or attributes;
                       namespace X = Y; is an alias and has no body */
                    block_of_declarations = true;
            } else if (t.kind == token_kind::literal &&
                       t.literal == literal_kind::string_literal &&
                       position == first + 1 &&
                       tokens[first].kind == token_kind::keyword &&
                       tokens[first].keyword == keyword.extern_ &&
                       at(preprocessing_op_or_punc::LEFT_BRACE, 1)) {
                /* [dcl.link]: extern string-literal { declaration-seq_opt } */
                block_of_declarations = true;
            } else if (position == first && at_keyword(keyword.export_) &&
                       at(preprocessing_op_or_punc::LEFT_BRACE, 1)) {
                /* [module.interface]: export { declaration-seq_opt } */
                block_of_declarations = true;
            }
        }
        consume();
    }
    syntax_kind kind = syntax_kind::unparsed;
    if (erroneous)
        kind = syntax_kind::erroneous;
    return tree.make(kind, nullptr, 0, first, position);
}

} // namespace cplusplus
