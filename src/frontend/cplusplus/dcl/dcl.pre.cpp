/*
 * dcl.pre.cpp: [dcl.pre] Declarations: preamble.
 *
 *     declaration-seq:
 *         declaration
 *         declaration-seq declaration
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* declaration-seq, or nothing when there is no declaration (the callers
   have declaration-seq_opt); inside braces it ends at the '}' */
node_index parser::declaration_seq(bool until_closing_brace)
{
    size_t mark = begin_node();
    token_index first = position;
    while (!at_end()) {
        if (until_closing_brace && at(preprocessing_op_or_punc::RIGHT_BRACE))
            break;
        declaration();
    }
    if (position == first)
        return 0;
    return finish_node(syntax_kind::declaration_seq, mark, first);
}

namespace
{

/*
 * Whether tokens [first, end) use grammar a later step of
 * doc/PLAN-cxx-parse.md writes: then a declaration that did not parse is
 * kept as an unparsed node, not reported.  These are templates, operator
 * names, attributes, class members' syntax and the extensions' keywords
 * (P2-P9).
 */
bool uses_later_grammar(parser &p, token_index first, token_index end)
{
    for (token_index i = first; i < end; i++) {
        const token &t = p.tokens[i];
        switch (p.keyword_of(t)) {
        case standard_keyword::template_:
        case standard_keyword::operator_:
        case standard_keyword::requires_:
        case standard_keyword::concept_:
        case standard_keyword::typename_:
        case standard_keyword::alignas_:
        case standard_keyword::asm_:
        case standard_keyword::friend_:
        case standard_keyword::try_:
        case standard_keyword::export_:
        case standard_keyword::co_await_:
        case standard_keyword::co_yield_:
        case standard_keyword::co_return_:
        case standard_keyword::virtual_:
        case standard_keyword::explicit_:
        case standard_keyword::extension:
            return true;
        default:
            break;
        }
        if (is_operator(t, preprocessing_op_or_punc::LESS) ||
            is_operator(t, preprocessing_op_or_punc::COLON))
            return true;
        if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET) &&
            is_operator(p.tokens[i + 1],
                        preprocessing_op_or_punc::LEFT_BRACKET))
            return true;
        if (t.kind == token_kind::identifier) {
            /* the reserved identifiers ([lex.name]/3) the implementations
               use for their extensions: __attribute__, __builtin_va_list */
            owned_buffer spelling;
            p.tokens.spelling(i, spelling.get());
            const char *s = spelling.data();
            if (spelling.size() >= 2 && s[0] == '_' &&
                (s[1] == '_' || (s[1] >= 'A' && s[1] <= 'Z')))
                return true;
        }
    }
    return false;
}

} // namespace

/* [lex.name]/2 Table 4: module, import and export are identifiers with
   special meaning at the start of a declaration; where [cpp.module] and
   [cpp.import] made them the module-keyword, import-keyword and
   export-keyword, phase 7 gives them no token kind of [lex.token] */
bool parser::starts_module_declaration()
{
    owned_buffer spelling;
    tokens.spelling(position, spelling.get());
    size_t n = spelling.size();
    const char *s = spelling.data();
    return (n == 6 && !memcmp(s, "module", 6)) ||
           (n == 6 && !memcmp(s, "import", 6)) ||
           (n == 6 && !memcmp(s, "export", 6));
}

/*
 * [dcl.pre]/1:
 *
 *     declaration:
 *         block-declaration
 *         nodeclspec-function-declaration
 *         function-definition
 *         template-declaration
 *         deduction-guide
 *         explicit-instantiation
 *         explicit-specialization
 *         export-declaration
 *         linkage-specification
 *         namespace-definition
 *         empty-declaration
 *         attribute-declaration
 *         module-import-declaration
 *
 *     block-declaration:
 *         simple-declaration
 *         asm-declaration
 *         namespace-alias-definition
 *         using-declaration
 *         using-enum-declaration
 *         using-directive
 *         static_assert-declaration
 *         alias-declaration
 *         opaque-enum-declaration
 *
 *     empty-declaration:
 *         ;
 *
 * Which alternative a declaration is follows from its first tokens, except
 * for simple-declaration and function-definition, which share their
 * decl-specifier-seq and declarator (block_or_function_declaration).
 *
 * A declaration that does not parse is either erroneous, and its one
 * diagnostic is issued ([intro.compliance]/2.2: "at least one diagnostic
 * message"), or, when it uses grammar a later step writes, kept as its
 * tokens; either way the next declaration starts after its ';' or '}'.
 */
node_index parser::declaration()
{
    token_index first = position;
    failure_state failed_with;
    {
        tentative_parse attempt(*this);
        size_t mark = begin_node();
        node_index n = 0;
        if (at(preprocessing_op_or_punc::SEMICOLON)) {
            add_token();
            n = finish_node(syntax_kind::empty_declaration, mark, first);
        } else if (at(standard_keyword::namespace_) ||
                   (at(standard_keyword::inline_) &&
                    at(standard_keyword::namespace_, 1))) {
            if (at_identifier(1) && at(preprocessing_op_or_punc::EQUAL, 2))
                n = namespace_alias_definition();
            else
                n = namespace_definition();
        } else if (at(standard_keyword::using_) &&
                   at(standard_keyword::namespace_, 1)) {
            n = using_directive();
        } else if (at(standard_keyword::using_) && at_identifier(1) &&
                   at(preprocessing_op_or_punc::EQUAL, 2)) {
            n = alias_declaration();
        } else if (at(standard_keyword::extern_) &&
                   peek(1).literal == literal_kind::string_literal) {
            n = linkage_specification();
        } else if (at(standard_keyword::static_assert_)) {
            n = static_assert_declaration();
        } else if ((at_identifier() || peek().kind == token_kind::invalid) &&
                   starts_module_declaration()) {
            /* module-declaration, module-import-declaration and
               export-declaration ([module]): P8 */
            n = beyond_this_step();
        } else if (at(standard_keyword::using_) ||
                   at(standard_keyword::template_) ||
                   at(standard_keyword::export_) ||
                   at(standard_keyword::asm_) ||
                   at(standard_keyword::alignas_) ||
                   (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
                    at(preprocessing_op_or_punc::LEFT_BRACKET, 1))) {
            /* using-declaration, using-enum-declaration (P2),
               template-declaration and its kin (P7), export-declaration
               (P8), asm-declaration, attribute-specifier-seq (P2) */
            n = beyond_this_step();
        } else {
            n = block_or_function_declaration();
        }
        if (!failed()) {
            attempt.commit();
            return n;
        }
        failed_with = failure;
    }
    /* the tokens were not a declaration of the grammar written so far */
    if (debug_fallbacks) {
        const char *why = "expected ";
        if (failed_with.beyond_step)
            why = "later step: ";
        fprintf(stderr, "cxxparse: declaration at %u:%u: %s%s at %u:%u\n",
                tokens.position(first).line, tokens.position(first).column, why,
                failed_with.expected, tokens.position(failed_with.at).line,
                tokens.position(failed_with.at).column);
    }
    size_t messages = diagnostics.messages.size();
    node_index n = unparsed_declaration();
    if (failed_with.beyond_step || uses_later_grammar(*this, first, position)) {
        counters.beyond_step++;
        if (tree[n].kind == syntax_kind::erroneous &&
            diagnostics.messages.size() > messages) {
            /* the splitter's bracket diagnostics are about tokens a later
               step reads: not reported yet */
            diagnostics.messages.resize(messages);
            diagnostics.error_count--;
        }
        return n;
    }
    if (tree[n].kind == syntax_kind::unparsed) {
        /* the declaration is erroneous: the splitter found its end, the
           failed parse where it went wrong */
        pending.resize(pending.size() - 1);
        tree.truncate(n);
        n = tree.make(syntax_kind::erroneous, nullptr, 0, first, position);
        add(n);
        diagnostics.issue(failed_with.id, tokens.location(failed_with.at))
            << failed_with.expected << failed_with.subclause;
    }
    return n;
}

/*
 * [dcl.pre]/1, and [dcl.fct.def.general]/1:
 *
 *     simple-declaration:
 *         decl-specifier-seq init-declarator-list_opt ;
 *         attribute-specifier-seq decl-specifier-seq init-declarator-list ;
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ] initializer ;
 *
 *     nodeclspec-function-declaration:
 *         attribute-specifier-seq_opt declarator ;
 *
 *     function-definition:
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator virt-specifier-seq_opt function-body
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator requires-clause function-body
 *
 * The attribute-specifier-seqs are P2's, the structured binding
 * declaration (the third alternative, [dcl.struct.bind]) P5's,
 * virt-specifier-seq and requires-clause P5's and P7's.  A declaration
 * whose first declarator declares a function and is followed by a
 * function-body is a function-definition; a decl-specifier-seq_opt that is
 * empty is a constructor's, destructor's or conversion function's
 * ([class.ctor], [class.dtor]: S::S() { }).
 */
node_index parser::block_or_function_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    decl_specifier_facts specifiers;
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    return simple_declaration_rest(mark, first, specifiers);
}

node_index
parser::simple_declaration_rest(size_t mark, token_index first,
                                const decl_specifier_facts &specifiers)
{
    if (at(preprocessing_op_or_punc::SEMICOLON)) {
        /* decl-specifier-seq init-declarator-list_opt ; ([dcl.pre]/5:
           "the decl-specifier-seq shall introduce one or more names into
           the program": a semantic rule) */
        if (!specifiers.node)
            return expected("a declaration", "dcl.pre");
        add_token();
        return finish_node(syntax_kind::simple_declaration, mark, first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACKET) ||
        ((at(preprocessing_op_or_punc::AMPERSAND) ||
          at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND)) &&
         at(preprocessing_op_or_punc::LEFT_BRACKET, 1)))
        return beyond_this_step(); /* a structured binding: P5 */

    declarator_facts facts;
    size_t list_mark = begin_node();
    token_index list_first = position;
    declarator(declarator_form::concrete, facts);
    if (failed())
        return 0;
    /* [basic.scope.pdecl]/1: "The point of declaration for a name is
       immediately after its complete declarator and before its initializer
       (if any)" */
    if (facts.name && !facts.qualified_id) {
        name_kind kind = name_kind::other;
        if (specifiers.typedef_)
            kind = name_kind::type_name;
        names.declare(facts.name, kind);
    }
    bool function = facts.first == derivation::function;
    if (function && (at(preprocessing_op_or_punc::LEFT_BRACE) ||
                     (at(preprocessing_op_or_punc::EQUAL) &&
                      (at(standard_keyword::default_, 1) ||
                       at(standard_keyword::delete_, 1))))) {
        function_body();
        if (failed())
            return 0;
        return finish_node(syntax_kind::function_definition, mark, first);
    }
    if (function &&
        (at(standard_keyword::try_) || at(preprocessing_op_or_punc::COLON) ||
         at(standard_keyword::requires_)))
        /* function-try-block (P6), ctor-initializer (P5), requires-clause
           (P7) */
        return beyond_this_step();
    if (!specifiers.node && !facts.qualified_id)
        /* nodeclspec-function-declaration is a member's (P5), or an
           explicit-specialization's: a declaration at namespace scope
           needs a decl-specifier-seq */
        return expected("a decl-specifier-seq", "dcl.spec");

    /* init-declarator-list:
           init-declarator
           init-declarator-list , init-declarator
       the first init-declarator's declarator is parsed already */
    init_declarator(specifiers, facts, true);
    if (failed())
        return 0;
    unsigned count = 1;
    while (accept(preprocessing_op_or_punc::COMMA)) {
        declarator_facts next;
        init_declarator(specifiers, next, false);
        if (failed())
            return 0;
        count++;
    }
    if (count > 1)
        finish_node(syntax_kind::init_declarator_list, list_mark, list_first);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the declaration", "dcl.pre");
    return finish_node(syntax_kind::simple_declaration, mark, first);
}

/*
 *     static_assert-declaration:
 *       static_assert ( constant-expression ) ;
 *       static_assert ( constant-expression , string-literal ) ;
 *
 * The constant-expression is P3's: kept as its tokens.
 */
node_index parser::static_assert_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after static_assert", "dcl.pre");
    static constexpr preprocessing_op_or_punc stops[] = {
        preprocessing_op_or_punc::COMMA};
    if (!unparsed_until(stops, 1))
        return expected("a constant-expression", "dcl.pre");
    if (accept(preprocessing_op_or_punc::COMMA)) {
        if (peek().literal != literal_kind::string_literal)
            return expected("a string-literal", "dcl.pre");
        add_token();
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')'", "dcl.pre");
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the static_assert-declaration", "dcl.pre");
    return finish_node(syntax_kind::static_assert_declaration, mark, first);
}

/*
 *     alias-declaration:
 *         using identifier attribute-specifier-seq_opt = defining-type-id ;
 *
 * [dcl.typedef]/2: "the identifier following the using keyword becomes a
 * typedef-name".  [basic.scope.pdecl]/3: its point of declaration is
 * immediately after the defining-type-id.
 */
node_index parser::alias_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    uint32_t name = tokens.identifier(position);
    add_token();
    add_token(); /* = */
    size_t type_mark = begin_node();
    token_index type_first = position;
    decl_specifier_facts specifiers;
    decl_specifier_seq(specifiers, true);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a defining-type-specifier-seq", "dcl.name");
    declarator_facts facts;
    declarator(declarator_form::abstract, facts);
    if (failed())
        return 0;
    finish_node(syntax_kind::defining_type_id, type_mark, type_first);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the alias-declaration", "dcl.pre");
    names.declare(name, name_kind::type_name);
    return finish_node(syntax_kind::alias_declaration, mark, first);
}

} // namespace cplusplus
