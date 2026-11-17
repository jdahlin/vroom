/*
 * dcl.pre.cpp: [dcl.pre] Declarations: preamble.
 *
 *     declaration-seq:
 *         declaration
 *         declaration-seq declaration
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

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
 * kept as an unparsed node, not reported.  These are templates, modules
 * and the extensions' keywords (P7-P9).
 */
bool uses_later_grammar(parser &p, token_index first, token_index end,
                        bool statement)
{
    for (token_index i = first; i < end; i++) {
        const token &t = p.tokens[i];
        switch (p.keyword_of(t)) {
        case standard_keyword::asm_:
        case standard_keyword::extension:
            return true;
        default:
            break;
        }
        /* in a statement a '<' after a name that may be a template's sets
           unknown_name */
        if (!statement && is_operator(t, preprocessing_op_or_punc::LESS))
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

VPRIV bool statement_uses_later_grammar(parser &p, token_index first,
                                        token_index end)
{
    return uses_later_grammar(p, first, end, true);
}

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
 *     attribute-declaration:
 *         attribute-specifier-seq ;
 *
 * Which alternative a declaration is follows from its first tokens (after
 * an attribute-specifier-seq, which begins a simple-declaration,
 * nodeclspec-function-declaration, function-definition,
 * attribute-declaration, using-directive or asm-declaration), except for
 * simple-declaration and function-definition, which share their
 * decl-specifier-seq and declarator (block_or_function_declaration).
 *
 * A declaration that does not parse is either erroneous, and its one
 * diagnostic is issued ([intro.compliance]/2.2: "at least one diagnostic
 * message"), or, when it uses grammar a later step writes, kept as its
 * tokens; either way the next declaration starts after its ';' or '}'.
 */
node_index parser::declaration()
{
    return recover_declaration(&parser::declaration_alternative);
}

node_index parser::recover_declaration(node_index (parser::*alternative)())
{
    token_index first = position;
    failure_state failed_with;
    /* the statements of a function-body recover here or in a statement,
       the member-declarations of a class here: what the enclosing
       declaration noted is restored when this one is done */
    struct recovery_point {
        parser &p;
        unsigned level;
        bool block_scope, unknown_name, limit;
        explicit recovery_point(parser &p)
            : p(p), level(p.recovery_level), block_scope(p.block_scope),
              unknown_name(p.unknown_name), limit(p.nesting_limit_exceeded)
        {
            p.block_scope = false;
            p.unknown_name = false;
            p.nesting_limit_exceeded = false;
        }
        ~recovery_point()
        {
            p.recovery_level = level;
            p.block_scope = block_scope;
            p.unknown_name = unknown_name;
            p.nesting_limit_exceeded = limit;
        }
    } recovery(*this);
    {
        tentative_parse attempt(*this);
        recovery_level = speculation;
        node_index n = (this->*alternative)();
        if (!failed()) {
            attempt.commit();
            return n;
        }
        failed_with = failure;
    }
    bool limit = nesting_limit_exceeded;
    if (limit) {
        /* [implimits]: the declaration nests deeper than this
           implementation parses, which is its diagnostic whatever else a
           tentative parse then found */
        failed_with = failure_state();
        failed_with.failed = true;
        failed_with.at = nesting_limit_at;
        failed_with.expected = "an expression or initializer nested no "
                               "deeper than this implementation's limit";
        failed_with.subclause = "implimits";
        failed_with.id = diagnostic_id::parse_violation;
        unknown_name = false;
        nesting_limit_exceeded = false;
    }
    /* the tokens were not a declaration of the grammar written so far */
    if (debug_fallbacks) {
        const char *why = "expected ";
        if (failed_with.beyond_step)
            why = "later step: ";
        owned_buffer spelling;
        tokens.spelling(failed_with.at, spelling.get());
        source_position at = tokens.position(failed_with.at);
        fprintf(stderr,
                "cxxparse: declaration at %s:%u:%u: %s%s at %u:%u `%.*s`\n",
                at.file_name, tokens.position(first).line,
                tokens.position(first).column, why, failed_with.expected,
                at.line, at.column, (int)spelling.size(), spelling.data());
    }
    size_t messages = diagnostics.messages.size();
    node_index n = unparsed_declaration();
    /* a declaration that named something the parser does not know may have
       failed for what the name is (a type, a template): a later step's */
    if (!limit && (failed_with.beyond_step || unknown_name ||
                   uses_later_grammar(*this, first, position, false))) {
        /* whatever grammar its tokens are, a declaration ends with ';' or
           with the '}' of its own braces: one that runs into the '}' of
           the region it is in is erroneous in every step (the splitter
           said so) */
        bool runs_into_brace =
            position > first && at(preprocessing_op_or_punc::RIGHT_BRACE) &&
            !is_operator(tokens[position - 1],
                         preprocessing_op_or_punc::SEMICOLON) &&
            !is_operator(tokens[position - 1],
                         preprocessing_op_or_punc::RIGHT_BRACE);
        if (tree[n].kind == syntax_kind::erroneous && runs_into_brace)
            return n;
        counters.beyond_step++;
        if (tree[n].kind == syntax_kind::erroneous) {
            /* the splitter's bracket diagnostics are about tokens a later
               step reads: not reported yet, and the node is unparsed */
            if (diagnostics.messages.size() > messages) {
                diagnostics.messages.resize(messages);
                diagnostics.error_count--;
            }
            pending.resize(pending.size() - 1);
            tree.truncate(n);
            n = tree.make(syntax_kind::unparsed, nullptr, 0, first, position);
            add(n);
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

/* which alternative of declaration the tokens are, by their first ones */
node_index parser::declaration_alternative()
{
    if (at(extension_keyword::extension)) /* [ext.gnu] */
        return gnu_extension_prefixed(&parser::declaration_alternative);
    size_t mark = begin_node();
    node_index n = 0;
    token_index first = position;
    unsigned after_attributes = attribute_specifier_seq_end(0);
    if (after_attributes) {
        if (at(preprocessing_op_or_punc::SEMICOLON, after_attributes))
            n = attribute_declaration();
        else if (at(standard_keyword::using_, after_attributes) &&
                 at(standard_keyword::namespace_, after_attributes + 1))
            n = using_directive();
        else if (at(standard_keyword::asm_, after_attributes))
            n = asm_declaration();
        else if (at(standard_keyword::using_, after_attributes))
            /* before a using-declaration or alias-declaration: clang's
               extension, P9's */
            return beyond_this_step();
        else
            n = block_or_function_declaration();
    } else if (at(preprocessing_op_or_punc::SEMICOLON)) {
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
    } else if (shared_declaration_alternative(&n)) {
    } else if (at(standard_keyword::asm_)) {
        n = asm_declaration();
    } else if (at(standard_keyword::extern_) &&
               peek(1).literal == literal_kind::string_literal) {
        n = linkage_specification();
    } else if (at_phase4_keyword(preprocessing_token_kind::import_keyword)) {
        n = module_import_declaration();
    } else if (at_phase4_keyword(preprocessing_token_kind::export_keyword) &&
               !at_module_declaration()) {
        n = export_declaration();
    } else if (at_phase4_keyword(preprocessing_token_kind::module_keyword) ||
               at_phase4_keyword(preprocessing_token_kind::export_keyword)) {
        /* [basic.link]/2: a module-declaration, a global or private module
           fragment only where translation-unit puts them */
        n = expected("a declaration (a module-declaration only at the start "
                     "of the translation unit)",
                     "basic.link");
    } else if (at(standard_keyword::template_) ||
               (at(standard_keyword::extern_) &&
                at(standard_keyword::template_, 1))) {
        n = template_declaration(&parser::declaration_alternative);
    } else if (deduction_guide_follows()) {
        n = deduction_guide();
    } else if (at(standard_keyword::export_)) {
        n = export_declaration();
    } else {
        n = block_or_function_declaration();
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
 * The third alternative is [dcl.struct.bind]'s structured binding
 * declaration (C++17); virt-specifier-seq is a member's (class.mem.cpp),
 * requires-clause P7's.  A declaration
 * whose first declarator declares a function and is followed by a
 * function-body is a function-definition; a decl-specifier-seq_opt that is
 * empty is a constructor's, destructor's or conversion function's
 * ([class.ctor], [class.dtor]: S::S() { }).
 */
node_index parser::block_or_function_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    node_index attributes = attribute_specifier_seq();
    if (failed())
        return 0;
    decl_specifier_facts specifiers;
    /* [temp.res]/5: "a decl-specifier of the decl-specifier-seq of a
       simple-declaration or a function-definition in namespace scope" */
    specifiers.implicit_typename =
        !block_scope &&
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    return simple_declaration_rest(mark, first, specifiers, attributes != 0);
}

node_index
parser::simple_declaration_rest(size_t mark, token_index first,
                                const decl_specifier_facts &specifiers,
                                bool attributes)
{
    /* [class.friend]/1: "A friend of a class is a function or class that
       is given permission to name the private and protected members of
       the class": the friend specifier ([dcl.friend]/1) is for a
       member-declaration (class.mem.cpp), not this declaration */
    if (specifiers.friend_)
        return violates("friend only in a member-declaration", "dcl.friend",
                        specifiers.friend_at);
    if (at(preprocessing_op_or_punc::SEMICOLON)) {
        /* decl-specifier-seq init-declarator-list_opt ; ([dcl.pre]/5:
           "the decl-specifier-seq shall introduce one or more names into
           the program": a semantic rule) */
        if (!specifiers.node)
            return expected("a declaration", "dcl.pre");
        /* attribute-specifier-seq decl-specifier-seq init-declarator-list ; */
        if (attributes)
            return expected("an init-declarator-list after an "
                            "attribute-specifier-seq and decl-specifier-seq",
                            "dcl.pre");
        /* [dcl.type.elab]/1: "If an elaborated-type-specifier is the sole
           constituent of a declaration, the declaration is ill-formed
           unless it is an explicit specialization, an explicit
           instantiation or it has one of the following forms: class-key
           attribute-specifier-seq_opt identifier ; ..." (the others are
           friend declarations, P5's) */
        /* [temp.explicit]/1: "A class, function, variable, or member
           template specialization can be explicitly instantiated": no
           enumeration */
        if (explicit_instantiation_ && specifiers.elaborated &&
            keyword_of(tokens[specifiers.elaborated_at]) ==
                standard_keyword::enum_)
            return violates("no enumeration in an explicit-instantiation",
                            "temp.explicit", specifiers.elaborated_at);
        if (specifiers.elaborated && specifiers.count == 1 &&
            !specifiers.elaborated_declaring_form && !explicit_declaration)
            return violates("an elaborated-type-specifier alone only as "
                            "class-key attribute-specifier-seq_opt "
                            "identifier ;",
                            "dcl.type.elab", specifiers.elaborated_at);
        add_token();
        return finish_node(syntax_kind::simple_declaration, mark, first);
    }
    if (structured_binding_follows()) {
        /* attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt
           [ identifier-list ] initializer ; */
        if (!specifiers.node) {
            /* Microsoft's ATL attributes, [uuid("...")] before a
               declaration: an extension, P9's */
            const implementation_defined_behavior &b = tokens.files.behavior;
            if (b.microsoft_extensions || b.microsoft_compatibility)
                return beyond_this_step();
            return expected("a decl-specifier-seq", "dcl.struct.bind");
        }
        structured_binding_identifiers();
        if (failed())
            return 0;
        if (!initializer() && !failed())
            return expected("an initializer", "dcl.struct.bind");
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the declaration", "dcl.pre");
        return finish_node(syntax_kind::simple_declaration, mark, first);
    }

    declarator_facts facts;
    size_t list_mark = begin_node();
    token_index list_first = position;
    if (template_declarator_violates(specifiers, true))
        return 0;
    declarator(declarator_form::concrete, facts);
    if (failed())
        return 0;
    /* [basic.scope.pdecl]/1: "The point of declaration for a name is
       immediately after its complete declarator and before its initializer
       (if any)" */
    if (facts.name && !facts.qualified_id)
        declare_declarator(specifiers, facts);
    bool function = facts.first == derivation::function;
    /* [dcl.decl]/1: init-declarator: declarator requires-clause, and
       [dcl.fct.def.general]/1: declarator requires-clause function-body */
    /* [dcl.decl]/3 as clang reads it: "trailing requires clause can only
       be used when declaring a function" */
    if (at(standard_keyword::requires_) && !function)
        return violates("a requires-clause only after a function's declarator",
                        "dcl.decl", position);
    if (at(standard_keyword::requires_)) {
        requires_clause();
        if (failed())
            return 0;
    }
    /* function-body: ctor-initializer_opt compound-statement (a
       constructor's ctor-initializer, [class.base.init]),
       function-try-block, = default ;, = delete ; */
    if (function &&
        (at(preprocessing_op_or_punc::LEFT_BRACE) ||
         at(preprocessing_op_or_punc::COLON) || at(standard_keyword::try_) ||
         (at(preprocessing_op_or_punc::EQUAL) &&
          (at(standard_keyword::default_, 1) ||
           at(standard_keyword::delete_, 1))))) {
        /* [dcl.typedef]/1: "nor in the decl-specifier-seq of a
           function-definition" */
        if (specifiers.typedef_)
            return violates("no typedef in a function-definition",
                            "dcl.typedef", specifiers.typedef_at);
        /* [dcl.fct.def.general]/2: "A function shall be defined only in
           namespace or class scope." */
        if (block_scope)
            return violates("a function-definition only in namespace or "
                            "class scope",
                            "dcl.fct.def.general", position);
        function_body(facts);
        if (failed())
            return 0;
        return finish_node(syntax_kind::function_definition, mark, first);
    }
    if (!specifiers.node && !facts.qualified_id)
        /* nodeclspec-function-declaration is a member's, or an
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
        if (template_declarator_violates(specifiers, false))
            return 0;
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
 */
bool parser::shared_declaration_alternative(node_index *n)
{
    if (at(standard_keyword::using_) && at(standard_keyword::enum_, 1))
        *n = using_enum_declaration();
    else if (at(standard_keyword::using_) && at_identifier(1) &&
             (at(preprocessing_op_or_punc::EQUAL, 2) ||
              at_attribute_specifier(2)))
        *n = alias_declaration();
    else if (at(standard_keyword::using_))
        *n = using_declaration();
    else if (at(standard_keyword::static_assert_))
        *n = static_assert_declaration();
    else if (at(standard_keyword::enum_) && opaque_enum_declaration_follows())
        *n = opaque_enum_declaration();
    else
        return false;
    return true;
}

node_index parser::static_assert_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after static_assert", "dcl.pre");
    constant_expression();
    if (failed())
        return 0;
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
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::EQUAL))
        return expected("'=' in the alias-declaration", "dcl.pre");
    size_t type_mark = begin_node();
    token_index type_first = position;
    decl_specifier_facts specifiers;
    /* [temp.res]/5: the defining-type-id of an alias-declaration */
    specifiers.implicit_typename =
        tokens.files.behavior.has(language_feature::implicit_typename);
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
    /* [dcl.typedef]/2: the typedef-name is a synonym of the type: of a
       class's, its members are found through it ([basic.lookup.qual]) */
    scope_index region = 0;
    if (facts.first == derivation::none)
        region = specifiers.type_region;
    names.declare(name, declared_kind(name_kind::type_name), region,
                  specifiers.dependent);
    return finish_node(syntax_kind::alias_declaration, mark, first);
}

} // namespace cplusplus

namespace cplusplus
{

/* attribute-declaration: [dcl.pre]/3 "Except where otherwise specified,
   the meaning of an attribute-declaration is implementation-defined." */
node_index parser::attribute_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the attribute-specifier-seq", "dcl.pre");
    return finish_node(syntax_kind::attribute_declaration, mark, first);
}

/*
 *     empty-declaration:
 *         ';'
 *
 * [dcl.pre]/7: "An empty-declaration has no effect."
 */
analysis_result semantic_analysis::empty_declaration(node_index)
{
    return analysis_result::well_formed;
}

/*
 *     simple-declaration:
 *         decl-specifier-seq init-declarator-list_opt ;
 *         attribute-specifier-seq decl-specifier-seq init-declarator-list ;
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ] initializer ;
 *
 * At global namespace scope, the declarations S1 of doc/PLAN-cxx-sema.md
 * has rules for: declarators with no initializer, no attributes.
 */
analysis_result semantic_analysis::simple_declaration(node_index n)
{
    const syntax_node &node = tree[n];
    if (!node.child_count ||
        !syntax_is(tree, tree.children(n)[0], syntax_kind::decl_specifier_seq))
        return not_supported(n, "dcl.attr");
    return init_declarators(n, tree.children(n)[0].index(), false);
}

/*
 * [dcl.fct.def.general]/1:
 *
 *     function-definition:
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator virt-specifier-seq_opt function-body
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator requires-clause function-body
 *
 * The declarator's type and name are analysed as a declaration's; the
 * function-body is not yet ([stmt]).
 */
analysis_result semantic_analysis::function_definition(node_index n)
{
    const syntax_node &node = tree[n];
    if (!node.child_count ||
        !syntax_is(tree, tree.children(n)[0], syntax_kind::decl_specifier_seq))
        return not_supported(n, "dcl.fct.def.general");
    analysis_result r = init_declarators(n, tree.children(n)[0].index(), true);
    if (r != analysis_result::well_formed)
        return r;
    entity_index f = last_declared;
    syntax_child body;
    for (uint32_t i = 0; i < node.child_count; i++)
        if (syntax_is(tree, tree.children(n)[i], syntax_kind::function_body))
            body = tree.children(n)[i];
    if (!f || entity_table[f].kind != entity_kind::function ||
        body.is_token() || !body.index())
        return not_supported(n, "dcl.fct.def.general");
    /* function-body: compound-statement; a ctor-initializer, a
       function-try-block, = default and = delete are not analysed yet */
    const syntax_node &b = tree[body.index()];
    if (b.child_count != 1 || !syntax_is(tree, tree.children(body.index())[0],
                                         syntax_kind::compound_statement))
        return not_supported(n, "dcl.fct.def.general");
    return function_body(n, tree.children(body.index())[0]);
}

/*
 * The declarators of a simple-declaration, or the one of a
 * function-definition.  [dcl.decl]/3: "Each init-declarator in a declaration
 * is analyzed separately as if it was in a declaration by itself."  The
 * declaration is ill-formed if one of them is, and unsupported if one is.
 */
analysis_result semantic_analysis::init_declarators(node_index declaration,
                                                    node_index specifiers,
                                                    bool definition)
{
    specified s;
    analysis_result r = decl_specifier_seq(specifiers, s, false);
    if (r != analysis_result::well_formed)
        return r;
    const syntax_node &node = tree[declaration];
    owned_array<syntax_child> items;
    for (uint32_t i = 1; i < node.child_count; i++) {
        syntax_child c = tree.children(declaration)[i];
        if (syntax_is(tree, c, syntax_kind::function_body))
            break;
        /* a declarator that is only its identifier is the token */
        if (c.is_token()) {
            if (tokens[c.index()].kind != token_kind::identifier)
                break;
            items.append(c);
            continue;
        }
        if (syntax_is(tree, c, syntax_kind::init_declarator_list)) {
            const syntax_node &list = tree[c.index()];
            for (uint32_t k = 0; k < list.child_count; k++) {
                syntax_child item = tree.children(c.index())[k];
                if (!item.is_token() ||
                    tokens[item.index()].kind == token_kind::identifier)
                    items.append(item);
            }
        } else {
            items.append(c);
        }
    }
    /* [dcl.pre]/5: "the optional init-declarator-list can be omitted only
       when declaring a class or enumeration": the decl-specifier-seq has
       neither (it would be unsupported), and clang accepts it with a
       warning */
    if (items.empty() && s.declares_class) {
        /* [dcl.pre]/5: a class declared: a storage class or other
           specifier with it is [dcl.pre]/5's "ill-formed" or clang's
           warning, not analysed */
        if (s.typedef_ || s.static_ || s.extern_ || s.inline_ || s.constexpr_ ||
            s.type.cv())
            return not_supported(declaration, "dcl.pre");
        return analysis_result::well_formed;
    }
    if (items.empty())
        return extension_used(diagnostic_id::sema_no_declarator,
                              tree[declaration].first_token, nullptr);
    analysis_result result = analysis_result::well_formed;
    for (syntax_child item : items) {
        r = init_declarator(declaration, item, s, definition);
        if (r == analysis_result::ill_formed)
            result = r;
        else if (r == analysis_result::unsupported &&
                 result == analysis_result::well_formed)
            result = r;
    }
    return result;
}

/*
 * One init-declarator: its declarator, the name it declares, and the
 * initializer.  [basic.scope.pdecl]/1: "The point of declaration for a name
 * is immediately after its complete declarator and before its initializer
 * (if any)".
 */
analysis_result semantic_analysis::init_declarator(node_index declaration,
                                                   syntax_child item,
                                                   const specified &s,
                                                   bool definition)
{
    syntax_child d = item;
    syntax_child init;
    bool initialized = false;
    if (syntax_is(tree, item, syntax_kind::init_declarator)) {
        const syntax_node &node = tree[item.index()];
        if (node.child_count != 2 ||
            syntax_is(tree, tree.children(item.index())[1],
                      syntax_kind::requires_clause))
            return not_supported(item.index(), "dcl.decl");
        d = tree.children(item.index())[0];
        init = tree.children(item.index())[1];
        initialized = true;
    }
    return declare_with_initializer(declaration, d, init, initialized, s,
                                    definition);
}

analysis_result semantic_analysis::declare_with_initializer(
    node_index declaration, syntax_child d, syntax_child init, bool initialized,
    const specified &s, bool definition)
{
    last_declared = 0;
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    analysis_result r = declarator(declaration, d, type, m);
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id)
        return not_supported(declaration, "dcl.meaning");
    if (m.aligned)
        return not_supported(declaration, "ext.gnu");
    if (m.qualifier) {
        if (initialized)
            return not_supported(declaration, "class.static.data");
        return member_redeclaration(declaration, m.id, m.qualifier, type, s,
                                    definition);
    }
    /* [dcl.fct]/6: "A function type with a cv-qualifier-seq or a
       ref-qualifier ... shall appear only as the function type for a
       non-static member function" */
    if (function_cv)
        return ill_formed_at_token(m.id,
                                   "a function type with a cv-qualifier-seq "
                                   "shall appear only as the function type "
                                   "for a non-static member function",
                                   "dcl.fct");
    /* [stmt.pre]/4: "The declarator shall not specify a function or an
       array." */
    if (in_condition &&
        (type_table[type.unqualified()].kind == type_kind::function ||
         type_table[type.unqualified()].kind == type_kind::array))
        return ill_formed_at_token(m.id,
                                   "the declarator of a condition shall not "
                                   "specify a function or an array",
                                   "stmt.pre");
    entity_kind kind = entity_kind::variable;
    if (s.typedef_)
        kind = entity_kind::typedef_name;
    else if (type_table[type.unqualified()].kind == type_kind::function)
        kind = entity_kind::function;
    if (s.constexpr_ && kind == entity_kind::function)
        /* a constexpr function's rules ([dcl.constexpr]/3) */
        return not_supported(declaration, "dcl.constexpr");
    if (s.constexpr_) {
        /* [dcl.constexpr]/10: "A constexpr specifier used in an object
           declaration declares the object as const.  Such an object shall
           have literal type and shall be initialized." */
        const struct type record = type_table[type.unqualified()];
        if (record.kind == type_kind::class_type ||
            record.kind == type_kind::enumeration ||
            record.kind == type_kind::none)
            return not_supported(declaration, "basic.types");
        if (!initialized)
            return ill_formed_at_token(m.id,
                                       "a constexpr object shall be "
                                       "initialized",
                                       "dcl.constexpr");
        type = add_cv(type, cv_const);
    }
    if (initialized && kind != entity_kind::variable)
        /* an initializer for a typedef-name or a function: a rule not found
           yet */
        return not_supported(declaration, "dcl.init");
    entity_index declared_entity = 0;
    r = declare(declaration, m.id, kind, type, s, definition, initialized,
                declared_entity);
    last_declared = declared_entity;
    /* the declaration statement's variables (statement) */
    if (r == analysis_result::well_formed && function_state.active &&
        kind == entity_kind::variable)
        block_variables.append(declared_entity);
    if (r != analysis_result::well_formed || !initialized)
        return r;
    /* [stmt.dcl]/3: a local variable with automatic storage duration and an
       initializer, which a jump shall not bypass */
    if (entity_table[declared_entity].automatic && function_state.active) {
        jump_point point;
        point.scope = current_scope;
        point.at = m.id;
        function_state.initialized.append(point);
    }
    /* until the initializer is analysed (and if it is unsupported) the
       value of a variable that may be usable in constant expressions is
       unknown */
    {
        uint32_t cv = cv_of(type_table, type);
        if (s.constexpr_ ||
            ((cv & cv_const) && !(cv & cv_volatile) && is_integral(type)))
            entity_table[declared_entity].usable = constness::unknown;
    }
    initialized_value value;
    value.evaluation = constness::unknown;
    value.value = 0;
    qualified_type before = type;
    {
        const struct type t = type_table[type.unqualified()];
        entity_table[declared_entity].bound_unknown =
            t.kind == type_kind::array && !t.bound_known;
    }
    r = initializer(init, type, value);
    if (r != analysis_result::well_formed)
        return r;
    entity_table[declared_entity].bound_unknown = false;
    const struct type record = type_table[type.unqualified()];
    /* [basic.def]/5: "In the definition of an object, the type of that
       object shall not be an incomplete type" */
    if (record.kind == type_kind::array && !record.bound_known)
        return ill_formed_at_token(m.id,
                                   "in the definition of an object, the type "
                                   "of that object shall not be an "
                                   "incomplete type",
                                   "basic.def");
    entity &e = entity_table[declared_entity];
    e.definition = value.node;
    if (!function_state.active) {
        definition_record d;
        d.declaration = current_declaration;
        d.entity = declared_entity;
        definitions.append(d);
    }
    if (!(before == type)) {
        /* [dcl.array]/7: an array of unknown bound completed by its
           initializer */
        e.type = type;
        declared.back().type = type;
    }
    /* [dcl.constexpr]/10: "In any constexpr variable declaration, the
       full-expression of the initialization shall be a constant
       expression" */
    if (s.constexpr_) {
        if (value.evaluation == constness::not_constant)
            return ill_formed_at_token(m.id,
                                       "in any constexpr variable "
                                       "declaration, the full-expression of "
                                       "the initialization shall be a "
                                       "constant expression",
                                       "dcl.constexpr");
        if (value.evaluation == constness::unknown)
            return not_supported(declaration, "expr.const");
    }
    /* [expr.const]/3: "A variable is usable in constant expressions after
       its initializing declaration is encountered if it is a constexpr
       variable, or it is a constant-initialized variable of
       const-qualified integral or enumeration type." */
    uint32_t cv = cv_of(type_table, type);
    bool integral_const = (cv & cv_const) && !(cv & cv_volatile) &&
                          record.kind == type_kind::fundamental &&
                          is_integral(type);
    e.usable = constness::not_constant;
    if (s.constexpr_ || integral_const) {
        e.usable = value.evaluation;
        e.value = value.value;
        e.constexpr_ = s.constexpr_;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
