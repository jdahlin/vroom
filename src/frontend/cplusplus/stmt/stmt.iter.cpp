/*
 * stmt.iter.cpp: [stmt.iter] Iteration statements, with [stmt.while],
 * [stmt.do], [stmt.for] and [stmt.ranged].
 *
 *     iteration-statement:
 *         while ( condition ) statement
 *         do statement while ( expression ) ;
 *         for ( init-statement condition_opt ; expression_opt ) statement
 *         for ( init-statement_opt for-range-declaration : for-range-initializer ) statement
 *
 *     for-range-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq declarator
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ]
 *
 *     for-range-initializer:
 *         expr-or-braced-init-list
 *
 * Which for statement it is: the range-based one has a ':' after its last
 * ';' (scan_parenthesized).  The range-based for statement is N2930's
 * (C++11), its init-statement P0614R1's (C++20); before them they are
 * [ext.clang] clang's extensions, P9's.  The structured binding of the
 * second for-range-declaration is P5's; for-range-initializer passes its
 * operand on.
 */
#include "frontend/cplusplus/sema.h"
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::iteration_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    const implementation_defined_behavior &b = tokens.files.behavior;
    standard_keyword k = keyword_of(peek());
    add_token();
    if (k == standard_keyword::do_) {
        substatement();
        if (failed())
            return 0;
        if (!accept(standard_keyword::while_))
            return expected("while after the substatement of do", "stmt.do");
        if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after while", "stmt.do");
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' after the expression", "stmt.do");
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the do statement", "stmt.do");
        return finish_node(syntax_kind::iteration_statement, mark, first);
    }
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'('", "stmt.iter");
    declarative_region statement_scope(names, scope_kind::block);
    if (k == standard_keyword::while_) {
        condition(preprocessing_op_or_punc::RIGHT_PARENTHESIS);
        if (failed())
            return 0;
    } else {
        parenthesized_parts parts = scan_parenthesized(*this);
        if (parts.range_colon) {
            if (!b.has(language_feature::range_based_for))
                return beyond_this_step();
            if (parts.semicolons) {
                if (!b.has(language_feature::range_for_init_statement))
                    return beyond_this_step();
                init_statement();
                if (failed())
                    return 0;
            }
            for_range_declaration();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::COLON))
                return expected("':' after the for-range-declaration",
                                "stmt.ranged");
            if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                if (list_initialization_missing())
                    return 0;
                braced_init_list();
            } else {
                expression();
            }
            if (failed())
                return 0;
        } else {
            init_statement();
            if (failed())
                return 0;
            if (!at(preprocessing_op_or_punc::SEMICOLON)) {
                condition(preprocessing_op_or_punc::SEMICOLON);
                if (failed())
                    return 0;
            }
            if (!accept(preprocessing_op_or_punc::SEMICOLON))
                return expected("';' after the condition", "stmt.for");
            if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                expression();
                if (failed())
                    return 0;
            }
        }
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')'", "stmt.iter");
    substatement();
    if (failed())
        return 0;
    return finish_node(syntax_kind::iteration_statement, mark, first);
}

/* the name is declared in the for statement's scope ([basic.scope.block]/3) */
node_index parser::for_range_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    decl_specifier_facts specifiers;
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a decl-specifier-seq", "stmt.iter");
    if (structured_binding_follows()) {
        /* [dcl.struct.bind] */
        structured_binding_identifiers();
        if (failed())
            return 0;
        return finish_node(syntax_kind::for_range_declaration, mark, first);
    }
    declarator_facts facts;
    declarator(declarator_form::concrete, facts);
    if (failed())
        return 0;
    if (facts.name && !facts.qualified_id)
        names.declare(facts.name, name_kind::other);
    return finish_node(syntax_kind::for_range_declaration, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [stmt.iter]/1:
 *
 *     iteration-statement:
 *         while ( condition ) statement
 *         do statement while ( expression ) ;
 *         for ( init-statement condition_opt ; expression_opt ) statement
 *         for ( init-statement_opt for-range-declaration : for-range-initializer ) statement
 *
 * The range-based for is range_based_for's.
 */
analysis_result semantic_analysis::iteration_statement(node_index n,
                                                       uint32_t &built)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    standard_keyword k = leading_keyword(n);
    if (k == standard_keyword::none)
        return not_supported(n, "stmt.iter");
    entity_index outer = current_scope, statement_scope = 0;
    analysis_result result = analysis_result::well_formed;
    expression_meaning m;
    token_index at = children[0].index();
    uint32_t body = 0, init = 0, step_node = 0;
    if (k == standard_keyword::do_) {
        /* do statement while ( expression ) ; */
        if (node.child_count != 7)
            return not_supported(n, "stmt.do");
        function_state.loops++;
        function_state.breakable++;
        result = substatement(children[1], 0, body);
        function_state.loops--;
        function_state.breakable--;
        /* [stmt.do]/1: "The expression is contextually converted to bool;
           if that conversion is ill-formed, the program is ill-formed." */
        analysis_result r = expression(children[4], m);
        if (r == analysis_result::well_formed)
            r = contextual_bool(n, m);
        built = statement_node(semantic_kind::do_statement, at, body, m.node);
        return worse(result, r);
    }
    if (k == standard_keyword::while_) {
        if (node.child_count != 5)
            return not_supported(n, "stmt.while");
        if (syntax_is(tree, children[2], syntax_kind::condition))
            statement_scope = open_scope(0, false);
        result = condition(children[2], false, m);
    } else if (k == standard_keyword::for_) {
        if (node.child_count < 6)
            return not_supported(n, "stmt.for");
        for (uint32_t i = 0; i < node.child_count; i++)
            if (syntax_is(tree, children[i],
                          syntax_kind::for_range_declaration))
                return range_based_for(n, i, built);
        /* for ( init-statement condition_opt ; expression_opt ) statement:
           the init-statement ends with its own ; */
        syntax_child init_syntax = children[2], cond, step;
        bool has_cond = false, has_step = false;
        uint32_t i = 3;
        if (!syntax_is_punctuator(tokens, children[i],
                                  preprocessing_op_or_punc::SEMICOLON)) {
            cond = children[i++];
            has_cond = true;
        }
        if (i >= node.child_count ||
            !syntax_is_punctuator(tokens, children[i],
                                  preprocessing_op_or_punc::SEMICOLON))
            return not_supported(n, "stmt.for");
        i++;
        if (i < node.child_count &&
            !syntax_is_punctuator(
                tokens, children[i],
                preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            step = children[i++];
            has_step = true;
        }
        if (i + 2 != node.child_count)
            return not_supported(n, "stmt.for");
        if (!syntax_is(tree, init_syntax, syntax_kind::expression_statement) ||
            (has_cond && syntax_is(tree, cond, syntax_kind::condition)))
            statement_scope = open_scope(0, false);
        bool claimable = declarations_claimable;
        declarations_claimable = false;
        result = statement(init_syntax, init);
        declarations_claimable = claimable;
        if (has_cond)
            result = worse(result, condition(cond, false, m));
        if (has_step) {
            expression_meaning e;
            result = worse(result, expression(step, e));
            step_node = e.node;
        }
    } else {
        return not_supported(n, "stmt.iter");
    }
    function_state.loops++;
    function_state.breakable++;
    result = worse(result, substatement(children[node.child_count - 1],
                                        statement_scope, body));
    function_state.loops--;
    function_state.breakable--;
    current_scope = outer;
    if (k == standard_keyword::while_)
        built =
            statement_node(semantic_kind::while_statement, at, m.node, body);
    else
        built = statement_node(semantic_kind::for_statement, at, init, m.node,
                               step_node, body);
    return result;
}

/* a variable [stmt.ranged]/1 defines for exposition only (unnamed, in
   the current scope) of type type, initialized by m: its declaration
   statement, and the variable named (an lvalue) in named */
analysis_result semantic_analysis::exposition_variable(
    node_index n, token_index at, expression_meaning &m, qualified_type type,
    bool named, uint32_t &declaration, expression_meaning &variable)
{
    entity_index v = entity_table.make(entity_kind::variable, 0, current_scope);
    entity_table[v].defined = true;
    entity_table[v].automatic = true;
    initialized_value value;
    scoped_value<qualified_type> deduced(declaration_deduced, qualified_type());
    analysis_result r =
        deduced_initialization(n, m, named, false, false, v, type, value);
    if (r != analysis_result::well_formed)
        return r;
    entity_table[v].definition = value.node;
    declaration =
        statement_node(semantic_kind::declaration_statement, at, v, value.node);
    variable = expression_meaning();
    return named_entity(at, v, variable, false);
}

/* [stmt.ranged]/1: begin-expr or end-expr (name begin or end) for range,
   the variable range named: range.name() (member, of class type) or
   name(range) */
analysis_result semantic_analysis::range_call(node_index n, token_index at,
                                              const expression_meaning &range,
                                              uint32_t name, bool member,
                                              expression_meaning &m)
{
    owned_array<syntax_child> arguments;
    owned_array<expression_meaning> early;
    owned_array<entity_index> set;
    expression_meaning f = range;
    uint32_t object = 0;
    entity_index chosen = 0;
    analysis_result r = analysis_result::well_formed;
    if (member) {
        bool ambiguous = false;
        entity_index c = type_table[range.type.unqualified()].entity;
        entity_index e = class_member_lookup(c, name, ambiguous);
        if (ambiguous || entity_table[e].kind != entity_kind::function)
            /* a member template, a data member, a type */
            return not_supported(n, "stmt.ranged");
        overloaded_callee = at;
        r = member_access(at, e, f);
        overloaded_callee = 0;
        if (r != analysis_result::well_formed)
            return r;
        if (!f.node ||
            semantics[f.node].kind != semantic_kind::bound_member_function)
            return not_supported(n, "stmt.ranged");
        object = semantics[f.node].a;
        chosen = semantics[f.node].b;
        overload_set(chosen, set);
    } else {
        /* "where begin and end are looked up in the associated namespaces
           ([basic.lookup.argdep]).  [Note 1: Ordinary unqualified lookup
           ([basic.lookup.unqual]) is not performed. -- end note]" */
        owned_array<qualified_type> types;
        types.append(range.type);
        owned_array<entity_index> found;
        if (!argument_dependent_lookup(name, types, found))
            return not_supported(n, "basic.lookup.argdep");
        for (entity_index e : found)
            overload_set(e, set);
        if (set.empty())
            return ill_formed(n,
                              "begin-expr and end-expr are begin(range) and "
                              "end(range), where begin and end are looked up "
                              "in the associated namespaces",
                              "stmt.ranged");
        arguments.append(syntax_child());
        early.append(range);
        f = expression_meaning();
        overloaded_callee = at;
        r = named_entity(at, set[0], f, false);
        overloaded_callee = 0;
        if (r != analysis_result::well_formed)
            return r;
    }
    if (set.size() > 1) {
        r = resolve_call(n, arguments, early, set, object, f);
        if (r != analysis_result::well_formed)
            return r;
        if (member)
            chosen = semantics[f.node].b;
    }
    return call_with(n, f, f.type, object, chosen, arguments, early, m);
}

/*
 * [stmt.ranged]/1: "The range-based for statement
 *
 *     for ( init-statement_opt for-range-declaration : for-range-initializer ) statement
 *
 * is equivalent to
 *
 *     {
 *         init-statement_opt
 *         auto &&range = for-range-initializer ;
 *         auto begin = begin-expr ;
 *         auto end = end-expr ;
 *         for ( ; begin != end; ++begin ) {
 *             for-range-declaration = * begin ;
 *             statement
 *         }
 *     }"
 *
 * built as that: the block, its declarations and the for statement, of
 * the variables range, begin and end unnamed.  declaration is the index
 * of the for-range-declaration among n's children.
 */
analysis_result semantic_analysis::range_based_for(node_index n,
                                                   uint32_t declaration,
                                                   uint32_t &built)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (declaration + 5 != node.child_count)
        return not_supported(n, "stmt.ranged");
    syntax_child range_declaration = children[declaration];
    syntax_child initializer = children[declaration + 2];
    token_index at = children[declaration + 1].index();
    entity_index outer = current_scope;
    entity_index statement_scope = open_scope(0, false);
    owned_array<uint32_t> items;
    analysis_result result = analysis_result::well_formed;
    /* the init-statement */
    if (declaration == 3) {
        bool claimable = declarations_claimable;
        declarations_claimable = false;
        uint32_t init = 0;
        result = statement(children[2], init);
        declarations_claimable = claimable;
        if (result != analysis_result::well_formed) {
            current_scope = outer;
            return result;
        }
        items.append(init);
    } else if (declaration != 2) {
        current_scope = outer;
        return not_supported(n, "stmt.ranged");
    }
    auto failed = [&](analysis_result r) {
        current_scope = outer;
        return r;
    };
    if (syntax_is(tree, initializer, syntax_kind::braced_init_list))
        /* auto && deduced from a braced-init-list: std::initializer_list
           ([dcl.type.auto.deduct]/4) */
        return failed(not_supported(n, "stmt.ranged"));
    /* auto &&range = for-range-initializer ; "if the for-range-initializer
       is an expression, it is regarded as if it were surrounded by
       parentheses" */
    expression_meaning m;
    analysis_result r = expression(initializer, m);
    if (r != analysis_result::well_formed)
        return failed(r);
    qualified_type forwarding = reference_to(placeholder_type(false), true);
    uint32_t declared = 0;
    expression_meaning range;
    r = exposition_variable(n, at, m, forwarding, false, declared, range);
    if (r != analysis_result::well_formed)
        return failed(r);
    items.append(declared);
    /* begin-expr and end-expr */
    expression_meaning first = range, last = range;
    const struct type record = type_table[range.type.unqualified()];
    if (record.kind == type_kind::array) {
        /* "if the for-range-initializer is an expression of array type R,
           begin-expr and end-expr are range and range + N, respectively,
           where N is the array bound.  If R is an array of unknown bound
           or an array of incomplete type, the program is ill-formed" */
        if (!record.bound_known || !size_of_type(record.operand))
            return failed(ill_formed(n,
                                     "if R is an array of unknown bound or an "
                                     "array of incomplete type, the program "
                                     "is ill-formed",
                                     "stmt.ranged"));
        expression_meaning bound = integer_constant(
            fundamental_type::unsigned_long_int, record.bound, at);
        expression_meaning base = range;
        r = builtin_binary(n, at, preprocessing_op_or_punc::PLUS, base, bound,
                           last);
    } else {
        uint32_t begin_name = tokens.pp.identifier("begin", 5) + 1;
        uint32_t end_name = tokens.pp.identifier("end", 3) + 1;
        bool member = false;
        if (record.kind == type_kind::class_type) {
            /* /1.2: "if the for-range-initializer is an expression of class
               type C, the unqualified-ids begin and end are looked up in
               the scope of C as if by class member access lookup
               ([basic.lookup.classref]), and if both find at least one
               declaration, begin-expr and end-expr are range.begin() and
               range.end(), respectively;" */
            entity_index c = record.entity;
            if (entity_table[c].kind != entity_kind::class_type ||
                !entity_table[c].size)
                return failed(not_supported(n, "stmt.ranged"));
            bool ambiguous = false;
            member = class_member_lookup(c, begin_name, ambiguous) &&
                     class_member_lookup(c, end_name, ambiguous);
            if (ambiguous)
                return failed(not_supported(n, "class.member.lookup"));
        }
        /* /1.3: "otherwise, begin-expr and end-expr are begin(range) and
           end(range), respectively, where begin and end are looked up in
           the associated namespaces ([basic.lookup.argdep])." (range_call:
           of a type with none, int *, nothing is found) */
        r = range_call(n, at, range, begin_name, member, first);
        if (r == analysis_result::well_formed)
            r = range_call(n, at, range, end_name, member, last);
    }
    if (r != analysis_result::well_formed)
        return failed(r);
    /* auto begin = begin-expr ; auto end = end-expr ; */
    expression_meaning begin, end;
    r = exposition_variable(n, at, first, placeholder_type(false), false,
                            declared, begin);
    if (r != analysis_result::well_formed)
        return failed(r);
    items.append(declared);
    r = exposition_variable(n, at, last, placeholder_type(false), false,
                            declared, end);
    if (r != analysis_result::well_formed)
        return failed(r);
    items.append(declared);
    /* begin != end */
    expression_meaning left = begin, right = end, condition;
    r = binary_of(n, at, preprocessing_op_or_punc::EXCLAMATION_EQUAL,
                  syntax_child(), left, syntax_child(), right, condition);
    if (r == analysis_result::well_formed)
        r = contextual_bool(n, condition);
    if (r != analysis_result::well_formed)
        return failed(r);
    /* ++begin */
    expression_meaning step = begin;
    r = increment_of(n, at, syntax_child(), false, false, step);
    if (r != analysis_result::well_formed)
        return failed(r);
    /* for-range-declaration = * begin ; */
    expression_meaning element = begin;
    r = indirection_of(n, at, syntax_child(), element);
    if (r != analysis_result::well_formed)
        return failed(r);
    uint32_t variable = 0;
    r = for_range_declaration(range_declaration, element, variable);
    if (r != analysis_result::well_formed)
        return failed(r);
    uint32_t body = 0;
    function_state.loops++;
    function_state.breakable++;
    result =
        substatement(children[node.child_count - 1], statement_scope, body);
    function_state.loops--;
    function_state.breakable--;
    current_scope = outer;
    uint32_t pair[2] = {variable, body};
    uint32_t loop_body = statement_node(semantic_kind::block, at);
    semantics.set_list(loop_body, pair, 2);
    items.append(statement_node(semantic_kind::for_statement, at, 0,
                                condition.node, step.node, loop_body));
    built = statement_node(semantic_kind::block, first_token(children[0]));
    semantics.set_list(built, items.data(), items.size());
    return result;
}

/*
 * [stmt.ranged]/1: "for-range-declaration = * begin ;", the element
 * analysed as element; the declaration statement built.
 *
 *     for-range-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq declarator
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ]
 */
analysis_result semantic_analysis::for_range_declaration(
    syntax_child d, const expression_meaning &element, uint32_t &built)
{
    node_index at = node_of(d);
    const syntax_node &node = tree[at];
    const syntax_child *children = tree.children(at);
    if (node.child_count != 2 ||
        !syntax_is(tree, children[0], syntax_kind::decl_specifier_seq) ||
        tree[at].first_token != first_token(children[0]))
        /* an attribute-specifier-seq, or a structured binding
           ([dcl.struct.bind]) */
        return not_supported(at, "stmt.ranged");
    specified s;
    placeholder_allowed = true;
    scoped_value<qualified_type> deduced(declaration_deduced, qualified_type());
    scoped_value<bool> group(placeholder_group, false);
    analysis_result r = decl_specifier_seq(children[0].index(), s, false);
    if (r != analysis_result::well_formed)
        return r;
    /* [stmt.ranged]/2: "In the decl-specifier-seq of a for-range-declaration,
       each decl-specifier shall be either a type-specifier or constexpr.
       The decl-specifier-seq shall not define a class or enumeration." */
    if (s.typedef_ || s.static_ || s.extern_ || s.inline_ || s.virtual_ ||
        s.friend_ || s.explicit_)
        return ill_formed(at,
                          "in the decl-specifier-seq of a "
                          "for-range-declaration, each decl-specifier shall "
                          "be either a type-specifier or constexpr",
                          "stmt.ranged");
    {
        /* "The decl-specifier-seq shall not define a class or
           enumeration." */
        node_index seq = children[0].index();
        for (uint32_t i = 0; i < tree[seq].child_count; i++) {
            syntax_child c = tree.children(seq)[i];
            if (syntax_is(tree, c, syntax_kind::class_specifier) ||
                syntax_is(tree, c, syntax_kind::enum_specifier))
                return ill_formed(at,
                                  "the decl-specifier-seq shall not define a "
                                  "class or enumeration",
                                  "stmt.ranged");
        }
    }
    size_t first = block_variables.size();
    bool claimable = declarations_claimable;
    declarations_claimable = false;
    r = declare_with_initializer(at, children[1], syntax_child(), true, s,
                                 false, &element);
    declarations_claimable = claimable;
    if (r != analysis_result::well_formed) {
        block_variables.resize(first);
        hold_names(at);
        return r;
    }
    built = declaration_statements(first, first_token(d));
    return analysis_result::well_formed;
}

} // namespace cplusplus
