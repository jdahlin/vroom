/*
 * dcl.meaning.cpp: [dcl.meaning] Meaning of declarators, with the
 * subclauses too small for a file of their own: [dcl.ptr], [dcl.ref],
 * [dcl.array] ([dcl.fct] is in dcl.fct.cpp).  The grammar is in
 * dcl.decl.cpp.
 *
 * [dcl.meaning]/3: "Thus, a declaration of a particular identifier has the
 * form T D where T is of the form attribute-specifier-seq_opt
 * decl-specifier-seq and D is a declarator."  Each form of D is a
 * subclause's "In a declaration T D where D has the form ...": the type D
 * gives its identifier is the one D1 gives it when T has that form's type
 * constructor applied.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [basic.type.qualifier]/3: "Cv-qualifiers applied to an array type attach
 * to the underlying element type"; [dcl.ref]/1: "Cv-qualified references
 * are ill-formed except when the cv-qualifiers are introduced through the
 * use of a typedef-name or decltype-specifier, in which case the
 * cv-qualifiers are ignored"; [dcl.fct]/7: on top of a function type "the
 * cv-qualifiers are ignored".
 */
qualified_type semantic_analysis::add_cv(qualified_type t, uint32_t cv)
{
    if (!cv)
        return t;
    const struct type record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::function:
        return t;
    case type_kind::array: {
        qualified_type element = add_cv(record.operand, cv);
        if (record.bound_known)
            return qualified_type::make(
                type_table.array_of(element, record.bound), cv_none);
        return qualified_type::make(
            type_table.array_of_unknown_bound_of(element), cv_none);
    }
    default:
        return qualified_type::make(t.unqualified(), t.cv() | cv);
    }
}

/*
 * [dcl.ref]/1: "In a declaration T D where D has either of the forms
 *     & attribute-specifier-seq_opt D1
 *     && attribute-specifier-seq_opt D1
 * ... the type of the identifier of D is derived-declarator-type-list
 * reference to T."
 */
analysis_result semantic_analysis::reference(node_index at, bool rvalue,
                                             qualified_type &type,
                                             declarator_meaning &m)
{
    const struct type referred = type_table[type.unqualified()];
    if (referred.kind == type_kind::lvalue_reference ||
        referred.kind == type_kind::rvalue_reference) {
        /* [dcl.ref]/5: "There shall be no references to references" */
        if (m.reference_made)
            return ill_formed(at, "there shall be no references to references",
                              "dcl.ref");
        /* [dcl.ref]/6: "If a typedef-name ... denotes a type TR that is a
           reference to a type T, an attempt to create the type lvalue
           reference to cv TR creates the type lvalue reference to T, while
           an attempt to create the type rvalue reference to cv TR creates
           the type TR." */
        if (!rvalue)
            type = qualified_type::make(
                type_table.lvalue_reference_to(referred.operand), cv_none);
        m.reference_made = true;
        return analysis_result::well_formed;
    }
    /* a reference to cv void: clang's error, a rule not found yet */
    if (referred.kind == type_kind::fundamental &&
        referred.fundamental == fundamental_type::void_)
        return not_supported(at, "dcl.ref");
    if (rvalue)
        type =
            qualified_type::make(type_table.rvalue_reference_to(type), cv_none);
    else
        type =
            qualified_type::make(type_table.lvalue_reference_to(type), cv_none);
    m.reference_made = true;
    return analysis_result::well_formed;
}

/*
 *     ptr-operator:
 *         * attribute-specifier-seq_opt cv-qualifier-seq_opt
 *         & attribute-specifier-seq_opt
 *         && attribute-specifier-seq_opt
 *         nested-name-specifier * attribute-specifier-seq_opt cv-qualifier-seq_opt
 *
 * [dcl.ptr]/1: "In a declaration T D where D has the form
 *     * attribute-specifier-seq_opt cv-qualifier-seq_opt D1
 * ... the type of the identifier of D is derived-declarator-type-list
 * cv-qualifier-seq pointer to T."
 */
analysis_result semantic_analysis::pointer_operator(node_index op,
                                                    qualified_type &type,
                                                    declarator_meaning &m)
{
    const syntax_node &node = tree[op];
    for (uint32_t i = 0; i < node.child_count; i++) {
        syntax_child c = tree.children(op)[i];
        if (syntax_is_attribute(tree, c))
            return not_supported(op, "dcl.attr");
        if (syntax_is(tree, c, syntax_kind::nested_name_specifier))
            return not_supported(op, "dcl.mptr");
        if (syntax_is_punctuator(tokens, c, preprocessing_op_or_punc::STAR)) {
            /* [dcl.ref]/5: "There shall be ... no pointers to references." */
            type_kind k = type_table[type.unqualified()].kind;
            if (k == type_kind::lvalue_reference ||
                k == type_kind::rvalue_reference)
                return ill_formed(
                    op, "there shall be no pointers to references", "dcl.ref");
            type = qualified_type::make(type_table.pointer_to(type), cv_none);
        } else if (syntax_is_punctuator(tokens, c,
                                        preprocessing_op_or_punc::AMPERSAND)) {
            analysis_result r = reference(op, false, type, m);
            if (r != analysis_result::well_formed)
                return r;
        } else if (syntax_is_punctuator(
                       tokens, c,
                       preprocessing_op_or_punc::AMPERSAND_AMPERSAND)) {
            analysis_result r = reference(op, true, type, m);
            if (r != analysis_result::well_formed)
                return r;
        } else if (syntax_is(tree, c, syntax_kind::cv_qualifier_seq)) {
            /* [dcl.ptr]/1: the cv-qualifiers apply to the pointer */
            const syntax_node &seq = tree[c.index()];
            for (uint32_t k = 0; k < seq.child_count; k++) {
                syntax_child q = tree.children(c.index())[k];
                if (!q.is_token())
                    return not_supported(op, "dcl.ptr");
                standard_keyword kw = p.keyword_of(tokens[q.index()]);
                if (kw == standard_keyword::const_)
                    type = add_cv(type, cv_const);
                else if (kw == standard_keyword::volatile_)
                    type = add_cv(type, cv_volatile);
                else
                    return not_supported(op, "ext");
            }
        } else if (c.is_token()) {
            standard_keyword kw = p.keyword_of(tokens[c.index()]);
            if (kw == standard_keyword::const_)
                type = add_cv(type, cv_const);
            else if (kw == standard_keyword::volatile_)
                type = add_cv(type, cv_volatile);
            else
                return not_supported(op, "ext");
        } else {
            return not_supported(op, "dcl.ptr");
        }
    }
    return analysis_result::well_formed;
}

/*
 * [dcl.array]/1: "In a declaration T D where D has the form
 *     D1 [ constant-expression_opt ] attribute-specifier-seq_opt
 * ... the type of the declarator-id in D is derived-declarator-type-list
 * array of N T."  /2: without the constant-expression, "array of unknown
 * bound of T".
 */
analysis_result semantic_analysis::array(node_index at,
                                         const syntax_child *bound,
                                         qualified_type &type)
{
    /* [dcl.array]/4: "U is called the array element type; this type shall
       not be a placeholder type, a reference type, a function type, an
       array of unknown bound, or cv void." */
    const struct type element = type_table[type.unqualified()];
    const char *rule = "the array element type shall not be a reference "
                       "type, a function type, an array of unknown bound, or "
                       "cv void";
    if (element.kind == type_kind::lvalue_reference ||
        element.kind == type_kind::rvalue_reference ||
        element.kind == type_kind::function ||
        (element.kind == type_kind::array && !element.bound_known) ||
        (element.kind == type_kind::fundamental &&
         element.fundamental == fundamental_type::void_))
        return ill_formed(at, rule, "dcl.array");
    if (!bound) {
        type = qualified_type::make(type_table.array_of_unknown_bound_of(type),
                                    cv_none);
        return analysis_result::well_formed;
    }
    /* "The constant-expression shall be a converted constant expression of
       type std::size_t" ([expr.const]/10: "A converted constant expression
       of type T is an expression, implicitly converted to type T, where the
       converted expression is a constant expression and the implicit
       conversion sequence contains only ... integral conversions other than
       narrowing conversions") */
    expression_meaning m;
    analysis_result r = expression(*bound, m);
    if (r != analysis_result::well_formed)
        return r;
    r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    const struct type of_bound = type_table[m.type.unqualified()];
    if (of_bound.kind == type_kind::enumeration ||
        of_bound.kind == type_kind::class_type ||
        of_bound.kind == type_kind::none)
        return not_supported(at, "expr.const");
    if (!is_integral(m.type))
        return ill_formed(at,
                          "the constant-expression shall be a converted "
                          "constant expression of type std::size_t",
                          "dcl.array");
    /* a bound that is not a constant expression: a variable length array
       (C99's, clang's extension), or one clang folds */
    if (m.evaluation != constness::constant)
        return not_supported(at, "expr.const");
    /* [expr.const]/1 of C++03: "An integral constant-expression can involve
       only literals, enumerators, const variables or static data members of
       integral or enumeration types initialized with constant expressions
       ... Floating literals can appear only if they are cast to integral or
       enumeration types." */
    if (!tokens.files.behavior.has(language_feature::list_initialization) &&
        m.beyond_integral_constant)
        return not_supported(at, "expr.const");
    const implementation_defined_behavior &b = tokens.files.behavior;
    fundamental_type f = fundamental_of(m.type);
    __int128 value = integer_value(b, f, m.value);
    /* a negative value narrows: clang's "array size is negative" */
    if (value < 0)
        return ill_formed(at,
                          "the constant-expression shall be a converted "
                          "constant expression of type std::size_t, whose "
                          "conversion is not narrowing",
                          "dcl.array");
    /* an array too large for the address space: clang's error, by size */
    if (value > (__int128(1) << 40))
        return not_supported(at, "dcl.array");
    /* "N shall be greater than zero": zero is clang's extension */
    if (!value) {
        r = extension_used(diagnostic_id::sema_zero_length_array,
                           tree[at].first_token, nullptr);
        if (r != analysis_result::well_formed)
            return r;
    }
    type = qualified_type::make(type_table.array_of(type, uint64_t(value)),
                                cv_none);
    return analysis_result::well_formed;
}

/*
 * [dcl.meaning]/1: "A declarator contains exactly one declarator-id; it
 * names the entity that is declared."  The declarator-id of d when it is an
 * identifier, without analysing the rest of d; 0 for an operator name, a
 * qualified-id or none.  The first identifier outside the
 * parameters-and-qualifiers, ptr-operators, attributes and initializer is
 * the declarator-id: a noptr-declarator's head comes before its suffixes,
 * so an identifier in an array bound comes after it.
 */
token_index semantic_analysis::declarator_id(syntax_child d) const
{
    if (d.is_token()) {
        if (tokens[d.index()].kind == token_kind::identifier)
            return d.index();
        return 0;
    }
    if (!syntax_is(tree, d, syntax_kind::init_declarator) &&
        !syntax_is(tree, d, syntax_kind::declarator) &&
        !syntax_is(tree, d, syntax_kind::ptr_declarator) &&
        !syntax_is(tree, d, syntax_kind::noptr_declarator))
        return 0;
    const syntax_node &node = tree[d.index()];
    for (uint32_t i = 0; i < node.child_count; i++) {
        syntax_child c = tree.children(d.index())[i];
        if (c.is_token()) {
            if (tokens[c.index()].kind == token_kind::identifier)
                return c.index();
            continue;
        }
        token_index id = declarator_id(c);
        if (id || syntax_is(tree, c, syntax_kind::qualified_id) ||
            syntax_is(tree, c, syntax_kind::unqualified_id))
            return id;
    }
    return 0;
}

/*
 * A declaration not analysed (unsupported or not parsed) still declares
 * its names, and a later declaration of one of them is a redeclaration whose
 * meaning depends on this one ([basic.link]/11, [dcl.stc]/6 ...): the names
 * found are bound to an entity of no kind, which makes such a declaration
 * unsupported too.
 */
void semantic_analysis::hold_names(node_index declaration)
{
    owned_array<token_index> ids;
    const syntax_node &node = tree[declaration];
    if (node.kind == syntax_kind::linkage_specification) {
        /* extern "C" { ... }: the declarations in it are the scope's */
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(declaration)[i];
            if (c.is_token())
                continue;
            if (syntax_is(tree, c, syntax_kind::declaration_seq)) {
                for (uint32_t k = 0; k < tree[c.index()].child_count; k++)
                    if (!tree.children(c.index())[k].is_token())
                        hold_names(tree.children(c.index())[k].index());
                continue;
            }
            hold_names(c.index());
        }
        return;
    }
    bool by_declarator = parsed_completely(declaration) &&
                         (node.kind == syntax_kind::simple_declaration ||
                          node.kind == syntax_kind::function_definition ||
                          node.kind == syntax_kind::condition);
    if (!by_declarator) {
        /* the declarators are not known (a declaration not parsed, or of
           a kind analysis has no rules for: a template, a
           using-declaration, ...): every identifier outside parentheses,
           brackets and braces, which holds more names than the declaration
           declares and so only makes more declarations unsupported */
        int depth = 0;
        for (token_index t = node.first_token; t < node.end_token; t++) {
            const token &tok = tokens[t];
            if (tok.kind == token_kind::identifier && !depth)
                ids.append(t);
            if (tok.kind != token_kind::operator_or_punctuator)
                continue;
            switch (preprocessing_op_or_punc_primary(tok.op_or_punc)) {
            case preprocessing_op_or_punc::LEFT_BRACE:
            case preprocessing_op_or_punc::LEFT_BRACKET:
            case preprocessing_op_or_punc::LEFT_PARENTHESIS:
                depth++;
                break;
            case preprocessing_op_or_punc::RIGHT_BRACE:
            case preprocessing_op_or_punc::RIGHT_BRACKET:
            case preprocessing_op_or_punc::RIGHT_PARENTHESIS:
                depth--;
                break;
            default:
                break;
            }
        }
    } else if (node.kind == syntax_kind::condition) {
        if (node.child_count > 1)
            ids.append(declarator_id(tree.children(declaration)[1]));
    } else {
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(declaration)[i];
            if (syntax_is(tree, c, syntax_kind::init_declarator_list)) {
                const syntax_node &list = tree[c.index()];
                for (uint32_t k = 0; k < list.child_count; k++)
                    ids.append(declarator_id(tree.children(c.index())[k]));
            } else {
                ids.append(declarator_id(c));
            }
        }
    }
    for (token_index id : ids) {
        if (!id)
            continue;
        uint32_t name = tokens.identifier(id);
        entity_index found = entity_table.find(current_scope, name);
        if (found) {
            /* a declaration of the name that analysis could not follow
               (an overload, a redeclaration with default arguments ...):
               what the name denotes is not known from here on */
            if (!declared_by(found, declaration))
                entity_table[found].kind = entity_kind::none;
            continue;
        }
        entity_index e =
            entity_table.make(entity_kind::none, name, current_scope);
        entity_table.bind(current_scope, name, e);
    }
}

/* whether the node is one of the entity's declarations */
bool semantic_analysis::declared_by(entity_index e, node_index node) const
{
    for (uint32_t d = entity_table[e].first_declaration; d;
         d = entity_table.declaration(d).next)
        if (entity_table.declaration(d).node == node)
            return true;
    return false;
}

/*
 * The type the declarator d gives its declarator-id, from the type T of
 * the decl-specifier-seq (type on entry).
 *
 * In noptr-declarator's left-recursive productions the last suffix is the
 * outermost D, so the suffixes apply from the last to the first: in
 * int a[2][3], D is D1[3] with D1 = a[2], and a is "array of 2 array of 3
 * int".
 */
analysis_result semantic_analysis::declarator(node_index declaration,
                                              syntax_child d,
                                              qualified_type &type,
                                              declarator_meaning &m)
{
    if (d.is_token()) {
        if (tokens[d.index()].kind != token_kind::identifier)
            return not_supported(declaration, "dcl.decl");
        m.has_id = true;
        m.id = d.index();
        return analysis_result::well_formed;
    }
    node_index n = d.index();
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    switch (node.kind) {
    case syntax_kind::ptr_declarator:
    case syntax_kind::ptr_abstract_declarator: {
        /* ptr-operator ptr-declarator */
        if (!node.child_count ||
            !syntax_is(tree, children[0], syntax_kind::ptr_operator))
            return not_supported(n, "dcl.decl");
        analysis_result r = pointer_operator(children[0].index(), type, m);
        if (r != analysis_result::well_formed || node.child_count == 1)
            return r;
        if (node.child_count != 2)
            return not_supported(n, "dcl.decl");
        return declarator(declaration, children[1], type, m);
    }
    case syntax_kind::parameters_and_qualifiers:
        /* an abstract declarator that is only its parameters */
        return function(n, type);
    case syntax_kind::noptr_declarator:
    case syntax_kind::noptr_abstract_declarator:
        break;
    case syntax_kind::qualified_id:
        /* a member of a namespace or a class declared again
           ([dcl.meaning]/1) */
        return not_supported(n, "dcl.meaning");
    case syntax_kind::declarator:
        /* a trailing-return-type */
        return not_supported(n, "dcl.fct");
    default:
        return not_supported(n);
    }
    /* the head: ( ptr-declarator ) or the declarator-id, or none in an
       abstract declarator; then the suffixes */
    uint32_t i = 0;
    bool has_head = false;
    syntax_child head = {};
    if (node.child_count >= 3 &&
        syntax_is_punctuator(tokens, children[0],
                             preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        if (!syntax_is_punctuator(tokens, children[2],
                                  preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return not_supported(n, "dcl.decl");
        has_head = true;
        head = children[1];
        i = 3;
    } else if (node.child_count && children[0].is_token() &&
               tokens[children[0].index()].kind == token_kind::identifier) {
        has_head = true;
        head = children[0];
        i = 1;
    } else if (node.child_count &&
               (syntax_is(tree, children[0], syntax_kind::qualified_id) ||
                syntax_is(tree, children[0], syntax_kind::unqualified_id))) {
        return not_supported(children[0].index(), "dcl.meaning");
    }
    struct suffix {
        bool function;
        uint32_t child;
        bool has_bound;
    };
    owned_array<suffix> suffixes;
    while (i < node.child_count) {
        if (syntax_is_attribute(tree, children[i]))
            return not_supported(n, "dcl.attr");
        if (syntax_is(tree, children[i],
                      syntax_kind::parameters_and_qualifiers)) {
            suffixes.append({true, i, false});
            i++;
        } else if (syntax_is_punctuator(
                       tokens, children[i],
                       preprocessing_op_or_punc::LEFT_BRACKET)) {
            bool has_bound =
                i + 1 < node.child_count &&
                !syntax_is_punctuator(tokens, children[i + 1],
                                      preprocessing_op_or_punc::RIGHT_BRACKET);
            suffixes.append({false, i + 1, has_bound});
            i += 2;
            if (has_bound)
                i++;
        } else {
            return not_supported(n, "dcl.decl");
        }
    }
    for (size_t k = suffixes.size(); k-- > 0;) {
        const suffix &x = suffixes[k];
        analysis_result r;
        if (x.function)
            r = function(children[x.child].index(), type);
        else if (x.has_bound)
            r = array(n, &children[x.child], type);
        else
            r = array(n, nullptr, type);
        if (r != analysis_result::well_formed)
            return r;
        /* a reference the suffix's operand was is no longer the
           declarator's outermost type */
        m.reference_made = false;
    }
    if (!has_head)
        return analysis_result::well_formed;
    return declarator(declaration, head, type, m);
}

} // namespace cplusplus
