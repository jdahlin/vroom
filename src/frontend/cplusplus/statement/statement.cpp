/*
 * statement.cpp: [stmt.pre] Statements: preamble.
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
 * through.  try-block is try-block.cpp's.
 */
#include "frontend/cplusplus/sema.h"
#include "ambiguity-resolution.h"

namespace cplusplus
{

namespace
{

/* attribute-specifier-seq_opt and the alternative parse() parses: a
   statement node when there are attributes */
template <class Alternative>
SyntaxRef with_attributes(parser &p, Alternative parse)
{
    size_t mark = p.begin_node();
    token_index first = p.position;
    SyntaxRef attributes = p.attribute_specifier_seq();
    if (p.failed())
        return 0;
    /* [ext.gnu] clang takes GCC's attributes before a declaration, a label,
       an expression statement or a null statement; before a statement
       with substatements it says "an attribute list cannot appear here" */
    if (attributes && has_gnu_attribute(p.tree, attributes) &&
        (p.at(preprocessing_op_or_punc::LEFT_BRACE) ||
         p.at(standard_keyword::if_) || p.at(standard_keyword::switch_) ||
         p.at(standard_keyword::while_) || p.at(standard_keyword::do_) ||
         p.at(standard_keyword::for_) || p.at(standard_keyword::try_)))
        return p.violates("GCC's attributes only before a declaration, a "
                          "label, or an expression or null statement",
                          "ext.gnu", first);
    SyntaxRef n = parse();
    if (p.failed())
        return 0;
    if (!attributes)
        return n;
    return p.finish_node(syntax_kind::statement, mark, first);
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
        if (closing_bracket(t) != preprocessing_op_or_punc::count) {
            depth++;
            continue;
        }
        if (is_closing_bracket(t)) {
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
        if (closing_bracket(t) != preprocessing_op_or_punc::count) {
            depth++;
        } else if (is_closing_bracket(t)) {
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
 * declaration-statement: [stmt.ambig] (ambiguity-resolution.h).  The attributes
 * of a declaration-statement are its block-declaration's, which parses them.
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
        return with_attributes(*this, [&] { return try_block(); });
    /* coroutine-return-statement ([stmt.return.coroutine]) */
    case standard_keyword::co_return_:
        return with_attributes(*this,
                               [&] { return coroutine_return_statement(); });
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
        /* [implimits]: the declaration's diagnostic (simple-declaration.cpp) */
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
    tree.truncate(n.index());
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
        preprocessing_op_or_punc closing = closing_bracket(t);
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
        if (is_closing_bracket(t)) {
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
    if (at(standard_keyword::using_, after)) {
        /* P2360R0's alias-declaration; before C++23 clang's extension,
           [ext:clang_later_edition_features] */
        if (after || !at_identifier(1) ||
            !at(preprocessing_op_or_punc::EQUAL, 2))
            return beyond_this_step();
        flag_set block(block_scope, true);
        return alias_declaration();
    }
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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [stmt.pre]/1: one statement of a function body.  A statement whose rules
 * are not written is unsupported, and the statements after it are still
 * analysed: the names an unsupported declaration declares are held
 * (hold_names), so that nothing after it rests on what it would have
 * declared.
 */
analysis_result semantic_analysis::statement(SyntaxElement s, uint32_t &built)
{
    built = 0;
    if (s.is_token())
        return not_supported(current_declaration, "stmt.pre");
    /* [stmt.dcl]/1: a block-declaration that declares no object: no code */
    auto declares_nothing = [&](SyntaxRef d) {
        analysis_result r = declaration_by_kind(d);
        built = statement_node(semantic_kind::null_statement, d.first_token());
        if (r != analysis_result::well_formed)
            hold_names(d);
        return r;
    };
    auto view = s.statement();
    if (!view)
        /* a node the grammar does not place in a statement */
        return not_supported(s.index());
    return view.visit(Overloaded{
        [&](GnuExtensionDeclaration x) {
            /* [ext:gnu_extension_keyword]: __extension__ changes no meaning */
            return statement(x[1], built);
        },
        [&](CompoundStatement c) { return compound_statement(c, 0, built); },
        [&](ExpressionStatement e) { return expression_statement(e, built); },
        [&](SelectionStatement c) { return selection_statement(c, built); },
        [&](IterationStatement c) { return iteration_statement(c, built); },
        /* (jump-statement's sum also has these two kinds: the sum's case
           would take them, and report their own subclauses elsewhere) */
        [&](CoroutineReturnStatement c) { return not_supported(c); },
        [&](GnuComputedGoto c) { return not_supported(c); },
        [&](JumpStatement c) { return jump_statement(c, built); },
        [&](LabeledStatement c) { return labeled_statement(c, built); },
        [&](SimpleDeclaration d) {
            /* [stmt.dcl]/1: "A declaration statement introduces one or more
               new names into a block" */
            size_t first = block_variables.size();
            analysis_result r = simple_declaration(d);
            built = declaration_statements(first, d.first_token());
            if (r != analysis_result::well_formed)
                hold_names(d);
            return r;
        },
        [&](StaticAssertDeclaration d) { return declares_nothing(d); },
        [&](AliasDeclaration d) { return declares_nothing(d); },
        [&](UsingDirective d) { return declares_nothing(d); },
        [&](NamespaceAliasDefinition d) { return declares_nothing(d); },
        [&](UsingDeclaration d) { return declares_nothing(d); },
        [&](Statement other) {
            if (other.is(syntax_kind::statement))
                return attributed_statement(other, built);
            return not_supported(other);
        }});
}

/*
 * [stmt.pre]/1: "The optional attribute-specifier-seq appertains to the
 * respective statement."  [dcl.attr.likelihood]/1: "The attribute-token
 * likely and unlikely may be applied to labels or statements"; /2 they are
 * hints for the path of execution, which change nothing vcc generates.
 * Other attributes of a statement have no rules here.
 */
analysis_result semantic_analysis::attributed_statement(Statement n,
                                                        uint32_t &built)
{
    /* the attribute-specifiers, then the statement */
    if (n.size() < 2)
        return not_supported(n, "stmt.pre");
    SyntaxElement inner = n[n.size() - 1];
    if (inner.is(syntax_kind::simple_declaration))
        return not_supported(n, "dcl.attr");
    for (uint32_t i = 0; i + 1 < n.size(); i++) {
        SyntaxElement c = n[i];
        if (!c.is(syntax_kind::attribute_specifier,
                  syntax_kind::attribute_specifier_seq))
            return not_supported(n, "dcl.attr");
        for (token_index t = c.first_token(); t < c.end_token(); t++) {
            if (tokens[t].kind == token_kind::operator_or_punctuator)
                continue;
            owned_buffer spelling;
            tokens.spelling(t, spelling.get());
            bool likely =
                spelling.size() == 6 && !memcmp(spelling.data(), "likely", 6);
            bool unlikely =
                spelling.size() == 8 && !memcmp(spelling.data(), "unlikely", 8);
            if (!likely && !unlikely)
                return not_supported(n, "dcl.attr");
        }
    }
    return statement(inner, built);
}

/*
 * [stmt.pre]/2: "A substatement of a statement is one of the following:
 * ... for a selection-statement, any of its statements ...; for an
 * iteration-statement, its statement".  [stmt.select]/2: "If the
 * substatement in a selection-statement is a single statement and not a
 * compound-statement, it is as if it was rewritten to be a
 * compound-statement containing the original substatement" (so for an
 * iteration-statement, [stmt.iter]/2).  conflicts: the scope of the
 * statement's condition, whose names the substatement's outermost block
 * shall not redeclare ([basic.scope.block]/3).
 */
analysis_result semantic_analysis::substatement(SyntaxElement s,
                                                entity_index conflicts,
                                                uint32_t &built)
{
    if (s.is(syntax_kind::compound_statement))
        return compound_statement(s, conflicts, built);
    if (!s.is(syntax_kind::simple_declaration))
        return statement(s, built);
    entity_index outer = current_scope;
    bool claimable = declarations_claimable;
    open_scope(conflicts, false);
    /* a claim just after it would be outside the implied block */
    declarations_claimable = false;
    uint32_t declaration = 0;
    analysis_result r = statement(s, declaration);
    declarations_claimable = claimable;
    current_scope = outer;
    /* the compound-statement it is as if it were rewritten to */
    built = statement_node(semantic_kind::block, first_token(s));
    semantics.set_list(built, &declaration, 1);
    return r;
}

/*
 * The node of a declaration statement whose variables are
 * block_variables[first ..]: one declaration_statement per variable (a: the
 * variable, b: its initializer's node), several in a declarations node, a
 * null statement when it declares no variable (a typedef).
 */
uint32_t semantic_analysis::declaration_statements(size_t first, token_index at)
{
    owned_array<uint32_t> each;
    for (size_t i = first; i < block_variables.size(); i++)
        each.append(statement_node(
            semantic_kind::declaration_statement, at, block_variables[i],
            entity_table[block_variables[i]].definition));
    block_variables.resize(first);
    if (each.empty())
        return statement_node(semantic_kind::null_statement, at);
    if (each.size() == 1)
        return each[0];
    uint32_t built = statement_node(semantic_kind::declarations, at);
    semantics.set_list(built, &each[0], each.size());
    return built;
}

/* the node of a syntax child, or the declaration's for a token (where a
   diagnostic about it is issued) */
SyntaxRef semantic_analysis::node_of(SyntaxElement c) const
{
    if (c.is_token())
        return current_declaration;
    return c.index();
}

/*
 * [stmt.pre]/1:
 *
 *     condition:
 *         expression
 *         attribute-specifier-seq_opt decl-specifier-seq declarator brace-or-equal-initializer
 *
 * [stmt.pre]/6: "The value of a condition that is an initialized
 * declaration in a statement other than a switch statement is the value of
 * the declared variable contextually converted to bool.  If that conversion
 * is ill-formed, the program is ill-formed.  The value of a condition that
 * is an initialized declaration in a switch statement is the value of the
 * declared variable if it has integral or enumeration type ...  The value
 * of a condition that is an expression is the value of the expression,
 * contextually converted to bool for statements other than switch".
 */
analysis_result semantic_analysis::condition(SyntaxElement c, bool for_switch,
                                             expression_meaning &m)
{
    SyntaxRef at = node_of(c);
    analysis_result r = analysis_result::well_formed;
    uint32_t declaration = 0;
    if (c.is(syntax_kind::condition)) {
        if (at.size() != 3 || !at[0].is(syntax_kind::decl_specifier_seq))
            return not_supported(at, "dcl.attr");
        specified s;
        placeholder_allowed = true;
        scoped_value<qualified_type> deduced(declaration_deduced,
                                             qualified_type());
        scoped_value<bool> group(placeholder_group, false);
        r = decl_specifier_seq(at[0], s, false);
        if (r == analysis_result::well_formed &&
            (s.typedef_ || s.static_ || s.extern_ || s.inline_))
            /* [stmt.pre]/8: "In the decl-specifier-seq of a condition, each
               decl-specifier shall be either a type-specifier or
               constexpr." */
            r = ill_formed(at,
                           "in the decl-specifier-seq of a condition, each "
                           "decl-specifier shall be either a type-specifier "
                           "or constexpr",
                           "stmt.pre");
        size_t first = block_variables.size();
        if (r == analysis_result::well_formed) {
            bool claimable = declarations_claimable;
            declarations_claimable = false;
            in_condition = true;
            r = declare_with_initializer(at, at[1], at[2], true, s, false);
            in_condition = false;
            declarations_claimable = claimable;
        }
        if (r != analysis_result::well_formed) {
            block_variables.resize(first);
            hold_names(at);
            return r;
        }
        declaration = declaration_statements(first, first_token(c));
        const entity &e = entity_table[last_declared];
        m = expression_meaning();
        m.category = value_category::lvalue;
        m.type = e.type;
        m.entity = last_declared;
        m.evaluation = constness::unknown;
        const struct type record = type_table[e.type.unqualified()];
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference) {
            m.type = record.operand;
            m.entity = 0;
        }
        m.node = expression_node(semantic_kind::entity, m, first_token(at[1]),
                                 last_declared);
    } else {
        VTRY_SET(r, expression(c, m));
    }
    if (!for_switch)
        r = contextual_bool(at, m);
    else
        r = switch_condition(at, m);
    if (r == analysis_result::well_formed && declaration)
        m.node = expression_node(semantic_kind::condition_declaration, m,
                                 first_token(c), declaration, m.node);
    return r;
}

/* [stmt.switch]/2: the value of a switch's condition, m */
analysis_result semantic_analysis::switch_condition(SyntaxRef at,
                                                    expression_meaning &m)
{
    VTRY(r, operand_value(at, m));
    /* [stmt.switch]/2: "The condition shall be of integral type,
       enumeration type, or class type. ... If the (possibly converted) type
       is subject to integral promotions, the condition is converted to the
       promoted type." */
    if (!promote_enumeration(m))
        enumeration_as_underlying(m);
    if (!is_integral(m.type)) {
        if (fundamental_of(m.type) == fundamental_type::none &&
            !is_pointer(m.type))
            return not_supported(at, "stmt.switch");
        return ill_formed(at,
                          "the condition shall be of integral type, "
                          "enumeration type, or class type",
                          "stmt.switch");
    }
    promote(m);
    return analysis_result::well_formed;
}

} // namespace cplusplus
