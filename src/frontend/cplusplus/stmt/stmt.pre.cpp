/*
 * stmt.pre.cpp: [stmt.pre] Statements: preamble.
 *
 *     statement:
 *         labeled-statement
 *         attribute-specifier-seq_opt expression-statement
 *         attribute-specifier-seq_opt compound-statement
 *         attribute-specifier-seq_opt selection-statement
 *         attribute-specifier-seq_opt iteration-statement
 *         attribute-specifier-seq_opt jump-statement
 *         declaration-statement
 *         attribute-specifier-seq_opt try-block
 *
 *     init-statement:
 *         expression-statement
 *         simple-declaration
 *
 *     condition:
 *         expression
 *         attribute-specifier-seq_opt decl-specifier-seq declarator brace-or-equal-initializer
 *
 * [stmt.pre]/1: "The optional attribute-specifier-seq appertains to the
 * respective statement": with one, the statement is a statement node of
 * the attributes and the alternative; without, the alternative passes
 * through.  try-block is P6's.
 */
#include "stmt.ambig.h"

namespace cplusplus
{

namespace
{

/* attribute-specifier-seq_opt and the alternative parse() parses: a
   statement node when there are attributes */
template <class Alternative>
node_index with_attributes(parser &p, Alternative parse)
{
    size_t mark = p.begin_node();
    token_index first = p.position;
    node_index attributes = p.attribute_specifier_seq();
    if (p.failed())
        return 0;
    node_index n = parse();
    if (p.failed())
        return 0;
    if (!attributes)
        return n;
    return p.finish_node(syntax_kind::statement, mark, first);
}

/* the bracket that closes an opening one, or count */
preprocessing_op_or_punc closer_of(const token &t)
{
    if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return preprocessing_op_or_punc::RIGHT_PARENTHESIS;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET))
        return preprocessing_op_or_punc::RIGHT_BRACKET;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACE))
        return preprocessing_op_or_punc::RIGHT_BRACE;
    return preprocessing_op_or_punc::count;
}

bool is_closer(const token &t)
{
    return is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE);
}

/* whether the decl-specifiers in tokens [first, end) are type-specifiers
   or constexpr; *other is where the first that is not one is.  The tokens
   of a class-specifier's or enum-specifier's braces are not
   decl-specifiers of the sequence. */
bool type_specifiers_or_constexpr(parser &p, token_index first, token_index end,
                                  token_index *other)
{
    unsigned depth = 0;
    for (token_index i = first; i < end; i++) {
        const token &t = p.tokens[i];
        if (closer_of(t) != preprocessing_op_or_punc::count) {
            depth++;
            continue;
        }
        if (is_closer(t)) {
            depth--;
            continue;
        }
        if (depth)
            continue;
        switch (p.keyword_of(t)) {
        case standard_keyword::static_:
        case standard_keyword::thread_local_:
        case standard_keyword::extern_:
        case standard_keyword::mutable_:
        case standard_keyword::typedef_:
        case standard_keyword::consteval_:
        case standard_keyword::constinit_:
        case standard_keyword::inline_:
        case standard_keyword::friend_:
        case standard_keyword::virtual_:
        case standard_keyword::explicit_:
        case standard_keyword::register_:
            *other = i;
            return false;
        default:
            break;
        }
    }
    return true;
}

} // namespace

VPRIV parenthesized_parts scan_parenthesized(parser &p)
{
    parenthesized_parts parts;
    unsigned depth = 0, conditionals = 0;
    for (unsigned i = 0; !p.tokens.at_end(p.position + i); i++) {
        const token &t = p.peek(i);
        if (closer_of(t) != preprocessing_op_or_punc::count) {
            depth++;
        } else if (is_closer(t)) {
            if (!depth)
                break;
            depth--;
        } else if (depth) {
            continue;
        } else if (is_operator(t, preprocessing_op_or_punc::SEMICOLON)) {
            parts.semicolons++;
            parts.range_colon = false;
            conditionals = 0;
        } else if (is_operator(t, preprocessing_op_or_punc::QUESTION)) {
            conditionals++;
        } else if (is_operator(t, preprocessing_op_or_punc::COLON)) {
            if (conditionals)
                conditionals--;
            else
                parts.range_colon = true;
        }
    }
    return parts;
}

/*
 * Which alternative a statement is follows from its first token after an
 * attribute-specifier-seq, except for expression-statement and
 * declaration-statement: [stmt.ambig] (stmt.ambig.h).  The attributes of a
 * declaration-statement are its block-declaration's, which parses them.
 */
node_index parser::statement_alternative()
{
    nesting_level level(*this);
    if (!nesting_within_limit("a statement nested no deeper than this "
                              "implementation's limit"))
        return 0;
    unsigned after = attribute_specifier_seq_end(0);
    standard_keyword k = keyword_of(peek(after));
    if ((at_identifier(after) &&
         at(preprocessing_op_or_punc::COLON, after + 1)) ||
        k == standard_keyword::case_ ||
        (k == standard_keyword::default_ &&
         at(preprocessing_op_or_punc::COLON, after + 1)))
        return labeled_statement();
    switch (k) {
    case standard_keyword::if_:
    case standard_keyword::switch_:
        return with_attributes(*this, [&] { return selection_statement(); });
    case standard_keyword::while_:
    case standard_keyword::do_:
    case standard_keyword::for_:
        return with_attributes(*this, [&] { return iteration_statement(); });
    case standard_keyword::break_:
    case standard_keyword::continue_:
    case standard_keyword::return_:
    case standard_keyword::goto_:
        return with_attributes(*this, [&] { return jump_statement(); });
    case standard_keyword::try_:
    /* coroutine-return-statement ([stmt.return.coroutine]) */
    case standard_keyword::co_return_:
        return beyond_this_step(); /* P6 */
    default:
        break;
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE, after))
        return with_attributes(*this, [&] { return compound_statement(); });
    if (at(preprocessing_op_or_punc::SEMICOLON, after))
        return with_attributes(*this, [&] { return expression_statement(); });
    return declaration_or_expression(
        reading_at(after), [&] { return block_declaration(); },
        [&] {
            return with_attributes(*this,
                                   [&] { return expression_statement(); });
        });
}

/*
 * [intro.compliance]/2.2: a statement that does not parse gets "at least
 * one diagnostic message", and the statements after it are parsed as if it
 * had parsed (the recovery of doc/PLAN-cxx-parse.md: skipping to the ';' or
 * the matching '}' at that nesting depth).  A statement that uses grammar
 * a later step writes, or names something the parser cannot classify, is
 * kept as its tokens without a diagnostic.
 *
 * Recovery is for the statements of a declaration or statement being
 * parsed for what it is; inside another tentative parse (a reading of an
 * ambiguous construct) a statement's failure is the reading's.
 */
node_index parser::statement()
{
    if (speculation != recovery_level)
        return statement_alternative();
    token_index first = position;
    bool unknown_before = unknown_name;
    unknown_name = false;
    unsigned level = recovery_level;
    failure_state failed_with;
    {
        tentative_parse attempt(*this);
        recovery_level = speculation;
        node_index n = statement_alternative();
        recovery_level = level;
        if (!failed()) {
            attempt.commit();
            unknown_name = unknown_before || unknown_name;
            return n;
        }
        failed_with = failure;
    }
    bool unknown = unknown_name;
    unknown_name = unknown_before;
    if (nesting_limit_exceeded) {
        /* [implimits]: the declaration's diagnostic (dcl.pre.cpp) */
        failure = failed_with;
        return 0;
    }
    bool ended = false;
    node_index n = unparsed_statement(&ended);
    /* every statement ends with its ';' or the '}' of a compound-statement
       (its own or its last substatement's): one that stops at the '}' of
       the enclosing block, or at the end of the translation unit, is
       erroneous whatever its names are (a label there is P2324R2's) */
    bool unterminated =
        !ended &&
        !(position > first &&
          is_operator(tokens[position - 1], preprocessing_op_or_punc::COLON));
    if (!unterminated &&
        (failed_with.beyond_step || unknown ||
         statement_uses_later_grammar(*this, first, position))) {
        counters.beyond_step++;
        return n;
    }
    pending.resize(pending.size() - 1);
    tree.truncate(n);
    n = tree.make(syntax_kind::erroneous, nullptr, 0, first, position);
    add(n);
    if (failed_with.beyond_step || failed_with.at > position) {
        diagnostics.issue(diagnostic_id::parse_expected,
                          tokens.location(position))
            << "';' or '}' to end the statement" << "stmt.pre";
        return n;
    }
    diagnostics.issue(failed_with.id, tokens.location(failed_with.at))
        << failed_with.expected << failed_with.subclause;
    return n;
}

/*
 * Where a statement that did not parse ends, found without parsing it:
 * brackets balanced, at its ';' outside them, or at the '}' of a
 * compound-statement that ends it (the substatement of a selection or
 * iteration statement, a compound-statement itself), unless else, catch or
 * the while of a do statement follow.  A '}' outside all brackets is the
 * enclosing compound-statement's.
 */
node_index parser::unparsed_statement(bool *ended)
{
    *ended = false;
    token_index first = position;
    owned_array<preprocessing_op_or_punc> closers;
    /* a lambda-expression's compound-statement does not end the statement
       ([](){ ... }();) */
    bool lambda = at(preprocessing_op_or_punc::LEFT_BRACKET) &&
                  !at(preprocessing_op_or_punc::LEFT_BRACKET, 1);
    bool do_pending = at(standard_keyword::do_);
    bool statement_brace = false;
    while (!at_end()) {
        const token &t = peek();
        bool outside = closers.empty();
        if (outside && is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE))
            break;
        if (outside && is_operator(t, preprocessing_op_or_punc::SEMICOLON)) {
            consume();
            *ended = true;
            if (at(standard_keyword::else_) || at(standard_keyword::catch_))
                continue;
            if (do_pending && at(standard_keyword::while_)) {
                do_pending = false;
                continue;
            }
            break;
        }
        preprocessing_op_or_punc closing = closer_of(t);
        if (closing != preprocessing_op_or_punc::count) {
            if (outside &&
                is_operator(t, preprocessing_op_or_punc::LEFT_BRACE)) {
                const token &before = tokens[position - 1];
                standard_keyword k = keyword_of(before);
                statement_brace =
                    position == first ||
                    (is_operator(before,
                                 preprocessing_op_or_punc::RIGHT_PARENTHESIS) &&
                     !lambda) ||
                    k == standard_keyword::else_ ||
                    k == standard_keyword::do_ || k == standard_keyword::try_ ||
                    k == standard_keyword::consteval_ ||
                    is_operator(before, preprocessing_op_or_punc::COLON) ||
                    (is_operator(before,
                                 preprocessing_op_or_punc::RIGHT_BRACKET) &&
                     position >= first + 2 &&
                     is_operator(tokens[position - 2],
                                 preprocessing_op_or_punc::RIGHT_BRACKET));
            }
            closers.append(closing);
            consume();
            continue;
        }
        if (is_closer(t)) {
            preprocessing_op_or_punc kind =
                preprocessing_op_or_punc_primary(t.op_or_punc);
            size_t i = closers.size();
            while (i > 0 && closers[i - 1] != kind)
                i--;
            consume();
            if (i == 0)
                continue; /* closes nothing: skipped */
            closers.resize(i - 1);
            if (closers.empty() &&
                kind == preprocessing_op_or_punc::RIGHT_BRACE &&
                statement_brace) {
                *ended = true;
                if (at(standard_keyword::else_) || at(standard_keyword::catch_))
                    continue;
                if (do_pending && at(standard_keyword::while_)) {
                    do_pending = false;
                    continue;
                }
                break;
            }
            continue;
        }
        consume();
    }
    node_index n =
        tree.make(syntax_kind::unparsed, nullptr, 0, first, position);
    add(n);
    return n;
}

/*
 * init-statement: [stmt.ambig] holds for it as for a statement.  An
 * attribute-specifier-seq can only begin its simple-declaration.
 * (P2360R0, C++23, adds alias-declaration.)
 */
node_index parser::init_statement()
{
    unsigned after = attribute_specifier_seq_end(0);
    reading r = reading::declaration;
    if (!after) {
        if (at(preprocessing_op_or_punc::SEMICOLON))
            return expression_statement();
        r = reading_at(0);
    }
    if (at(standard_keyword::using_, after))
        return beyond_this_step();
    return declaration_or_expression(
        r,
        [&] {
            flag_set block(block_scope, true);
            return block_or_function_declaration();
        },
        [&] { return expression_statement(); });
}

/*
 * condition: [stmt.pre]/7: "If a condition can be syntactically resolved
 * as either an expression or the declaration of a block-scope name, it is
 * interpreted as a declaration."  The declaration's name is declared in
 * the statement's scope ([basic.scope.block]/3).  /4's "The declarator
 * shall not specify a function or an array" and /8's rule on the
 * decl-specifiers are semantic.
 */
node_index parser::condition(preprocessing_op_or_punc closing)
{
    unsigned after = attribute_specifier_seq_end(0);
    reading r = reading::declaration;
    if (!after)
        r = reading_at(0);
    const char *what = "')' after the condition";
    if (closing == preprocessing_op_or_punc::SEMICOLON)
        what = "';' after the condition";
    return declaration_or_expression(
        r,
        [&]() -> node_index {
            size_t mark = begin_node();
            token_index first = position;
            attribute_specifier_seq();
            if (failed())
                return 0;
            decl_specifier_facts specifiers;
            token_index specifiers_first = position;
            decl_specifier_seq(specifiers, false);
            if (failed())
                return 0;
            if (!specifiers.node)
                return expected("a decl-specifier-seq", "stmt.pre");
            /* [stmt.pre]/8: "In the decl-specifier-seq of a condition,
               each decl-specifier shall be either a type-specifier or
               constexpr." */
            token_index other = 0;
            if (!type_specifiers_or_constexpr(*this, specifiers_first, position,
                                              &other))
                return violates("only type-specifiers and constexpr in the "
                                "decl-specifier-seq of a condition",
                                "stmt.pre", other);
            declarator_facts facts;
            declarator(declarator_form::concrete, facts);
            if (failed())
                return 0;
            if (facts.name && !facts.qualified_id)
                names.declare(facts.name, name_kind::other);
            if (at(preprocessing_op_or_punc::EQUAL)) {
                size_t initializer_mark = begin_node();
                token_index initializer_first = position;
                add_token();
                initializer_clause();
                if (failed())
                    return 0;
                finish_node(syntax_kind::brace_or_equal_initializer,
                            initializer_mark, initializer_first);
            } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                if (list_initialization_missing())
                    return 0;
                braced_init_list();
                if (failed())
                    return 0;
            } else {
                return expected("a brace-or-equal-initializer", "stmt.pre");
            }
            if (!at(closing))
                return expected(what, "stmt.pre");
            return finish_node(syntax_kind::condition, mark, first);
        },
        [&]() -> node_index {
            node_index n = expression();
            if (failed())
                return 0;
            if (!at(closing))
                return expected(what, "stmt.pre");
            return n;
        });
}

/*
 * [stmt.select]/2: "The substatement in a selection-statement (each
 * substatement, in the else form of the if statement) implicitly defines a
 * block scope"; [stmt.iter]/2 the same for an iteration-statement's.
 */
node_index parser::substatement()
{
    declarative_region block(names, scope_kind::block);
    return statement();
}

} // namespace cplusplus
