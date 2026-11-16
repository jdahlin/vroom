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
    /* each entry of the lexer's keyword table as the standard's keyword it
       is, or an extension's */
    struct named {
        const char *spelling;
        standard_keyword keyword;
    };
    static constexpr named standard[] = {
#define STANDARD_KEYWORD(name, spelling) {spelling, standard_keyword::name},
#include "keywords.inc"
#undef STANDARD_KEYWORD
    };
    int size = keyword_table_size();
    keyword_kinds.resize(static_cast<size_t>(size));
    for (int i = 0; i < size; i++) {
        keyword_kinds[i] = standard_keyword::extension;
        for (const named &n : standard)
            if (!strcmp(n.spelling, keyword_spelling(i)))
                keyword_kinds[i] = n.keyword;
    }
}

standard_keyword parser::keyword_of(const token &t) const
{
    if (t.kind != token_kind::keyword || t.keyword < 0 ||
        static_cast<size_t>(t.keyword) >= keyword_kinds.size())
        return standard_keyword::none;
    return keyword_kinds[t.keyword];
}

bool parser::at(preprocessing_op_or_punc op, unsigned ahead)
{
    return is_operator(peek(ahead), op);
}

bool parser::at(standard_keyword k, unsigned ahead)
{
    return keyword_of(peek(ahead)) == k;
}

bool parser::at_identifier(unsigned ahead)
{
    return peek(ahead).kind == token_kind::identifier;
}

bool parser::accept(preprocessing_op_or_punc op)
{
    if (!at(op))
        return false;
    add_token();
    return true;
}

bool parser::accept(standard_keyword k)
{
    if (!at(k))
        return false;
    add_token();
    return true;
}

node_index parser::expected(const char *what, const char *subclause)
{
    if (!failure.failed) {
        failure.failed = true;
        failure.at = position;
        failure.expected = what;
        failure.subclause = subclause;
    }
    return 0;
}

node_index parser::violates(const char *rule, const char *subclause,
                            token_index at)
{
    if (!failure.failed) {
        failure.failed = true;
        failure.at = at;
        failure.expected = rule;
        failure.subclause = subclause;
        failure.id = diagnostic_id::parse_violation;
    }
    return 0;
}

/*
 * [implimits]/1: "Because computers are finite, C++ implementations are
 * inevitably limited in the size of the programs they can successfully
 * process."  /2's "Nesting levels of parenthesized expressions within a
 * full-expression [256]" is a recommended minimum; the limit here is for
 * every way an expression or an initializer nests (operands of unary
 * operators and casts, the operands of ?: and of assignments, braces), so
 * that the parser's own recursion is bounded.  A tentative parse does not
 * undo having passed it: the declaration is ill-formed for it.
 */
bool parser::nesting_within_limit(const char *what)
{
    if (nesting <= nesting_limit && !nesting_limit_exceeded)
        return true;
    if (!nesting_limit_exceeded) {
        nesting_limit_exceeded = true;
        nesting_limit_at = position;
    }
    violates(what, "implimits", position);
    return false;
}

node_index parser::beyond_this_step()
{
    failure.beyond_step = true;
    return expected("grammar of a later step", "");
}

node_index parser::finish_node(syntax_kind kind, size_t mark,
                               token_index first_token)
{
    node_index n = tree.make(kind, pending.data() + mark, pending.size() - mark,
                             first_token, position);
    pending.resize(mark);
    pending.append(syntax_child::node(n));
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
      errors(p.diagnostics.error_count), lookups(p.names.lookups.size()),
      splits(p.tree.split_tokens.size()), split_active(p.split_active),
      split_rest(p.split_rest), names(p.names.journal_mark()),
      failure(p.failure)
{
    p.counters.tentative_parses++;
    p.speculation++;
}

parser::tentative_parse::~tentative_parse()
{
    p.speculation--;
    if (committed)
        return;
    p.counters.rewinds++;
    p.counters.rewound_tokens += p.position - position;
    p.position = position;
    p.pending.resize(pending);
    p.tree.truncate(nodes);
    p.diagnostics.messages.resize(messages);
    p.diagnostics.error_count = errors;
    p.names.lookups.resize(lookups);
    p.tree.split_tokens.resize(splits);
    p.split_active = split_active;
    p.split_rest = split_rest;
    p.names.rollback(names);
    p.failure = failure;
}

void parser::tentative_parse::commit()
{
    committed = true;
}

namespace
{

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

/* the lookahead after the bracketed group at tokens[ahead]: after the
   bracket that closes it, or the end of the translation unit */
unsigned parser::group_end(unsigned ahead)
{
    if (closing_bracket(peek(ahead)) == preprocessing_op_or_punc::count)
        return ahead;
    unsigned depth = 0;
    for (unsigned i = ahead;; i++) {
        const token &t = peek(i);
        if (tokens.at_end(position + i))
            return i;
        if (closing_bracket(t) != preprocessing_op_or_punc::count)
            depth++;
        else if (is_closing_bracket(t) && !--depth)
            return i + 1;
    }
}

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
        at(standard_keyword::namespace_) ||
        (at(standard_keyword::inline_) && at(standard_keyword::namespace_, 1));

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
                    position > first &&
                    keyword_of(before) == standard_keyword::requires_;
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
                    position > first && keyword_of(tokens[position - 1]) ==
                                            standard_keyword::requires_;
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
            if (outside && enclosing_braces &&
                closing == preprocessing_op_or_punc::RIGHT_BRACE) {
                /* the '}' of the namespace-body or linkage-specification
                   the declaration is in: the declaration ended before it */
                if (position > first && !erroneous) {
                    diagnostics.issue(
                        diagnostic_id::parse_expected_end_of_declaration,
                        tokens.location(position));
                    erroneous = true;
                }
                break;
            }
            if (outside) {
                /* the '}' of an enclosing region is not this declaration's:
                   nothing encloses a declaration of the translation unit */
                if (!erroneous)
                    diagnostics.issue(diagnostic_id::parse_unmatched_closing,
                                      tokens.location(position))
                        << bracket_spelling(closing)
                        << bracket_spelling(opening_bracket(closing));
                erroneous = true;
                bool alone = position == first;
                consume();
                /* a stray bracket on its own is a declaration of its own;
                   inside a declaration, the declaration still ends at its
                   ';' */
                if (alone)
                    break;
                continue;
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
                /* a class-key before the parameters is the return type's; one
                   after them (a trailing-return-type's) is what a '{' is for */
                class_key = false;
                continue;
            }
            if (closing != preprocessing_op_or_punc::RIGHT_BRACE || !body)
                continue;
            /* [except.pre]: function-try-block: the handler-seq follows the
               compound-statement */
            if (at(standard_keyword::catch_))
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
            if (at(standard_keyword::template_) &&
                at(preprocessing_op_or_punc::LESS, 1)) {
                template_head = true;
                angles = 0;
                consume();
                continue;
            }
            if (is_operator(t, preprocessing_op_or_punc::EQUAL)) {
                /* operator= is a name, not an initializer */
                if (!(position > first && keyword_of(tokens[position - 1]) ==
                                              standard_keyword::operator_))
                    equal = true;
            } else if (is_operator(t, preprocessing_op_or_punc::COLON)) {
                if (parameters && !equal)
                    mem_initializers = true;
            } else if (t.kind == token_kind::keyword) {
                if (keyword_of(t) == standard_keyword::class_ ||
                    keyword_of(t) == standard_keyword::struct_ ||
                    keyword_of(t) == standard_keyword::union_ ||
                    keyword_of(t) == standard_keyword::enum_)
                    class_key = true;
                else if (keyword_of(t) == standard_keyword::namespace_ &&
                         !equal)
                    /* [namespace.def]: inline_opt namespace ... {, also
                       after export ([module.interface]) or attributes;
                       namespace X = Y; is an alias and has no body */
                    block_of_declarations = true;
            } else if (t.kind == token_kind::literal &&
                       t.literal == literal_kind::string_literal &&
                       position == first + 1 &&
                       keyword_of(tokens[first]) == standard_keyword::extern_ &&
                       at(preprocessing_op_or_punc::LEFT_BRACE, 1)) {
                /* [dcl.link]: extern string-literal { declaration-seq_opt } */
                block_of_declarations = true;
            } else if (position == first && at(standard_keyword::export_) &&
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
    node_index n = tree.make(kind, nullptr, 0, first, position);
    add(n);
    return n;
}

/*
 * Scaffolding: the tokens of a part of the grammar that is not written yet
 * (an expression, an initializer-clause, an array bound), up to one of the
 * stops outside all brackets.  A closing bracket outside all brackets ends
 * it too: it is the enclosing production's.
 */
node_index parser::unparsed_until(const preprocessing_op_or_punc *stops,
                                  size_t stop_count)
{
    token_index first = position;
    unsigned depth = 0;
    while (!at_end()) {
        const token &t = peek();
        if (!depth) {
            bool stop = is_closing_bracket(t);
            for (size_t i = 0; i < stop_count && !stop; i++)
                stop = is_operator(t, stops[i]);
            if (stop)
                break;
        }
        if (closing_bracket(t) != preprocessing_op_or_punc::count)
            depth++;
        else if (is_closing_bracket(t))
            depth--;
        consume();
    }
    if (position == first)
        return 0;
    node_index n =
        tree.make(syntax_kind::unparsed, nullptr, 0, first, position);
    add(n);
    return n;
}

/* ( ... ), [ ... ] or { ... } at the current token, kept as its tokens */
node_index parser::unparsed_group()
{
    token_index first = position;
    owned_array<preprocessing_op_or_punc> closers;
    do {
        const token &t = peek();
        preprocessing_op_or_punc closing = closing_bracket(t);
        if (closing != preprocessing_op_or_punc::count)
            closers.append(closing);
        else if (is_closing_bracket(t)) {
            if (preprocessing_op_or_punc_primary(t.op_or_punc) !=
                closers.back()) {
                position = first;
                return expected(bracket_spelling(closers.back()),
                                "lex.operators");
            }
            closers.resize(closers.size() - 1);
        }
        consume();
    } while (!closers.empty() && !at_end());
    if (!closers.empty())
        return expected(bracket_spelling(closers.back()), "lex.operators");
    node_index n =
        tree.make(syntax_kind::unparsed, nullptr, 0, first, position);
    add(n);
    return n;
}

} // namespace cplusplus
