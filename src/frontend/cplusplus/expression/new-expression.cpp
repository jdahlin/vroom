/*
 * new-expression.cpp: [expr.new] New.
 *
 *     new-expression:
 *         ::_opt new new-placement_opt new-type-id new-initializer_opt
 *         ::_opt new new-placement_opt ( type-id ) new-initializer_opt
 *
 *     new-placement:
 *         ( expression-list )
 *
 *     new-initializer:
 *         ( expression-list_opt )
 *         braced-init-list
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * A '(' after new begins a new-placement or the ( type-id ) of the second
 * alternative: [dcl.ambig.res]/2 makes it the type-id when it can be one.
 * After a new-placement a '(' is the second alternative's.
 */
node_index parser::new_expression()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    accept(preprocessing_op_or_punc::COLON_COLON);
    add_token(); /* new */
    bool type = false;
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        type = parenthesized_type_id(false);
        if (failed())
            return 0;
        if (!type) {
            size_t placement_mark = begin_node();
            token_index placement_first = position;
            add_token();
            if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("an expression-list in the new-placement",
                                "expr.new");
            expression_list();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' to close the new-placement", "expr.new");
            finish_node(syntax_kind::new_placement, placement_mark,
                        placement_first);
            if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                add_token();
                type_id();
                if (failed())
                    return 0;
                if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                    return expected("')' after the type-id", "expr.new");
                type = true;
            }
        }
    }
    if (!type) {
        new_type_id();
        if (failed())
            return 0;
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        size_t initializer_mark = begin_node();
        token_index initializer_first = position;
        if (!parenthesized_expression_list("')' to close the new-initializer",
                                           "expr.new"))
            return 0;
        finish_node(syntax_kind::new_initializer, initializer_mark,
                    initializer_first);
    } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        /* new-initializer: braced-init-list passes through */
        if (list_initialization_missing())
            return 0;
        braced_init_list();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::new_expression, mark, first);
}

/*
 *     new-type-id:
 *         type-specifier-seq new-declarator_opt
 *
 * [expr.new]/3: "The new-type-id in a new-expression is the longest
 * possible sequence of new-declarators", so a ptr-operator or '[' after the
 * type-specifier-seq is always the new-declarator's.
 */
node_index parser::new_type_id()
{
    size_t mark = begin_node();
    token_index first = position;
    decl_specifier_facts specifiers;
    specifiers.type_specifier_seq = true;
    /* [temp.res]/5: "a type-id in a new-expression" */
    specifiers.implicit_typename =
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, true);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a type-specifier-seq after new", "expr.new");
    new_declarator();
    if (failed())
        return 0;
    return finish_node(syntax_kind::new_type_id, mark, first);
}

/*
 *     new-declarator:
 *         ptr-operator new-declarator_opt
 *         noptr-new-declarator
 *
 *     noptr-new-declarator:
 *         [ expression_opt ] attribute-specifier-seq_opt
 *         noptr-new-declarator [ constant-expression ] attribute-specifier-seq_opt
 *
 * new-declarator_opt: nothing (0, no failure) when none starts here.  The
 * bounds of a noptr-new-declarator are one node's children.  [[ after the
 * type is no bound ([dcl.attr.grammar]/7).
 *
 * Before C++11 (N2118) && is no ptr-operator, so new int && b is a
 * logical-and-expression there.
 */
node_index parser::new_declarator()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND) &&
        !tokens.files.behavior.has(language_feature::rvalue_references))
        return 0;
    if (at_ptr_operator()) {
        derivation kind = derivation::none;
        ptr_operator(&kind);
        if (failed())
            return 0;
        new_declarator();
        if (failed())
            return 0;
        return finish_node(syntax_kind::new_declarator, mark, first);
    }
    if (!at(preprocessing_op_or_punc::LEFT_BRACKET))
        return 0;
    bool first_bound = true;
    while (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
        if (at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
            return misplaced_attribute_brackets();
        add_token();
        if (first_bound) {
            if (!at(preprocessing_op_or_punc::RIGHT_BRACKET))
                expression();
        } else {
            if (at(preprocessing_op_or_punc::RIGHT_BRACKET))
                return expected("a constant-expression: only the first "
                                "bound of a new-declarator may be omitted",
                                "expr.new");
            constant_expression();
        }
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
            return expected("']' to close the array bound", "expr.new");
        attribute_specifier_seq(true);
        if (failed())
            return 0;
        first_bound = false;
    }
    return finish_node(syntax_kind::noptr_new_declarator, mark, first);
}

/* ---- semantic analysis ---- */

/*
 *     new-type-id:
 *         type-specifier-seq new-declarator_opt
 *
 * The ptr-operators apply as a declarator's do ([dcl.ptr]); a
 * noptr-new-declarator of one bound makes the allocated object an array
 * ([expr.new]/6), its bound the expression returned.  More bounds, an
 * omitted one and attributes are not analysed.
 */
analysis_result semantic_analysis::new_type_id(NewTypeId n,
                                               qualified_type &type,
                                               SyntaxElement &bound)
{
    if (!n.type_specifier_seq() || n.size() > 2)
        return not_supported_because(
            n, "expr.new",
            "a new type-id with no type-specifier-seq or with extra parts");
    specified s;
    VTRY(r, decl_specifier_seq(n.type_specifier_seq(), s, false));
    if (!s.has_type)
        return not_supported_because(
            n, "expr.new", "a new type-id whose specifiers name no type");
    type = s.type;
    if (n.size() == 1)
        return analysis_result::well_formed;
    SyntaxElement d = n.new_declarator();
    for (;;) {
        if (d.is(syntax_kind::noptr_new_declarator)) {
            auto bounds = d;
            if (bounds.size() != 3)
                return not_supported_because(d.index(), "expr.new",
                                             "an array new-declarator without "
                                             "exactly one bracketed bound");
            bound = d[1];
            return analysis_result::well_formed;
        }
        if (!d.is(syntax_kind::new_declarator))
            return not_supported_because(
                n, "expr.new",
                "a new type-id followed by something not a new-declarator");
        auto declarator = d;
        if (!declarator.size() || declarator.size() > 2 ||
            !declarator[0].is(syntax_kind::ptr_operator))
            return not_supported_because(
                d.index(), "expr.new",
                "a new-declarator that does not start with a ptr-operator");
        declarator_meaning ignored;
        r = pointer_operator(declarator[0], type, ignored);
        if (r != analysis_result::well_formed || declarator.size() == 1)
            return r;
        d = declarator[1];
    }
}

analysis_result semantic_analysis::allocation_functions(
    SyntaxRef n, allocation_function kind, entity_index c,
    owned_array<entity_index> &set, bool &in_class, bool class_complete,
    entity_index *templates)
{
    implicit_allocation_functions();
    uint32_t name = allocation_function_name(kind);
    in_class = false;
    require_complete(c);
    if (c && !class_complete && !entity_table[c].size &&
        allocation_names_spelled() != allocation_declarations_analysed)
        /* Before its layout, the class may declare one analysis has not
           followed. A class with a size has had its members and bases
           analysed, including in a complete-class context. */
        return not_supported_because(
            n, "class.free",
            "an allocation function lookup in a class not yet fully analysed");
    if (c) {
        lookup_result found = lookup_qualified(c, name, lookup_filter::any);
        if (templates && found.unknown && !found.ambiguous && !found.entity &&
            member_function_template_of(found.unknown_entity)) {
            /* only member function templates of the name: [class.free]/1,
               candidates by deduction ([expr.new]/21) */
            *templates = found.unknown_entity;
            in_class = true;
            return analysis_result::well_formed;
        }
        if (found.ambiguous || found.unknown)
            return not_supported_because(
                n, "class.free",
                "an ambiguous or unknown member allocation function name");
        if (found.entity) {
            if (entity_table[found.entity].kind != entity_kind::function)
                return not_supported_because(
                    n, "class.free",
                    "a member allocation function name that is not a function");
            if (entity_table[found.entity].member_templates) {
                /* [class.free]/1: its member function templates are
                   candidates too ([expr.new]/21), by deduction from the
                   arguments of the new-expression; not for a delete */
                if (!templates)
                    return not_supported_because(
                        n, "class.free",
                        "a member allocation function template where templates "
                        "are not handled");
                *templates = member_templates_of(found.entity);
            }
            overload_set(found.entity, set);
            in_class = true;
            return analysis_result::well_formed;
        }
    }
    lookup_result found = lookup_in_namespace(0, name, lookup_filter::any);
    if (found.ambiguous || found.unknown || !found.entity ||
        entity_table[found.entity].kind != entity_kind::function)
        return not_supported_because(
            n, "basic.stc.dynamic",
            "no usable global allocation or deallocation function found");
    overload_set(found.entity, set);
    return analysis_result::well_formed;
}

/*
 * [expr.new]/1: "The new-expression attempts to create an object of the
 * type-id ([dcl.name]) or new-type-id to which it is applied.  The type of
 * that object is the allocated type.  This type shall be a complete object
 * type, but not an abstract class type or array thereof".  /22: "If the
 * new-initializer is omitted, the object is default-initialized ...
 * Otherwise, the new-initializer is interpreted according to the
 * initialization rules of [dcl.init] for direct-initialization."
 *
 * Not analysed: a placeholder type (/2), an array with a new-initializer,
 * new-extended alignment (/18), the deallocation when the initialization
 * throws (/25).
 */
analysis_result semantic_analysis::new_expression(NewExpression n,
                                                  expression_meaning &m)
{
    token_index at = n.first_token();
    uint32_t count = n.size();
    uint32_t i = 0;
    /* [expr.new]/11: "If the new-expression begins with a unary ::
       operator, the allocation function's name is looked up in the global
       scope." */
    bool global = false;
    if (i < count && n[i].is(preprocessing_op_or_punc::COLON_COLON)) {
        global = true;
        i++;
    }
    i++; /* new */
    SyntaxElement placement{};
    if (i < count && n[i].is(syntax_kind::new_placement))
        placement = n[i++];
    qualified_type type;
    SyntaxElement bound{};
    analysis_result r = analysis_result::well_formed;
    /* [dcl.spec.auto]/2: a placeholder type "in the type-specifier-seq of
       the new-type-id or type-id of a new-expression": deduced from the
       new-initializer below */
    placeholder_allowed = true;
    if (i < count && n[i].is(syntax_kind::new_type_id)) {
        r = new_type_id(n[i++], type, bound);
    } else if (i + 2 < count && n[i + 1].is(syntax_kind::type_id)) {
        placeholder_allowed = true;
        r = type_id(n[i + 1], type);
        i += 3;
    } else {
        return not_supported_because(
            n, "expr.new", "a new-expression with no recognised form of type");
    }
    if (r != analysis_result::well_formed)
        return r;
    SyntaxElement init{};
    if (i < count)
        init = n[i++];
    if (i < count)
        return not_supported_because(
            n, "expr.new", "a new-expression with parts after its initializer");
    if (contains_placeholder(type)) {
        /* [dcl.type.auto.deduct]/2: "for a new-expression with a type that
           contains a placeholder type, T is the declared type of the
           new-expression and E is the new-initializer's single
           assignment-expression" ([dcl.spec.auto]/4: the initializer shall
           be one); no initializer, or an array: not analysed */
        SyntaxElement clause;
        bool direct = false, list = false;
        if (bound || !init || init.is_token() ||
            !initializer_form(init, clause, direct, list) || !direct)
            return not_supported_because(n, "dcl.type.auto.deduct",
                                         "a new with a placeholder type not "
                                         "direct-initialized, or of an array");
        expression_meaning deduced_from;
        VTRY(dr, expression(clause, deduced_from));
        qualified_type deduced, replacement;
        VTRY_SET(dr,
                 deduce_placeholder(n, type, deduced_from, names_entity(clause),
                                    deduced, replacement));
        type = deduced;
    }
    /* [expr.new]/6: an array: "the new-expression yields a pointer to the
       initial element (if any) of the array"; a type-id of array type is
       one too */
    bool array = static_cast<bool>(bound);
    uint64_t known_count = 0;
    if (!array && type_table[type.unqualified()].kind == type_kind::array) {
        const struct type &a = type_table[type.unqualified()];
        if (!a.bound_known)
            /* /8: the bound from the new-initializer */
            return not_supported_because(
                n, "expr.new", "a new of an array type of unknown bound");
        array = true;
        known_count = a.bound;
        type = a.operand;
    }
    /* [temp.inst]/2: a specialization named only is defined here (before
       the reference into type_table: completing may grow it) */
    require_complete(type);
    const struct type &allocated = type_table[type.unqualified()];
    bool object_type = allocated.kind != type_kind::lvalue_reference &&
                       allocated.kind != type_kind::rvalue_reference &&
                       allocated.kind != type_kind::function &&
                       !(allocated.kind == type_kind::fundamental &&
                         allocated.fundamental == fundamental_type::void_);
    if (!object_type)
        return ill_formed(n,
                          "the allocated type shall be a complete object type",
                          "expr.new");
    if (allocated.kind == type_kind::array)
        return not_supported_because(
            n, "expr.new", "a new whose allocated type is still an array");
    entity_index c = 0;
    if (allocated.kind == type_kind::class_type) {
        c = allocated.entity;
        if (entity_table[c].kind != entity_kind::class_type)
            return not_supported_because(
                n, "class.pre",
                "a new of a class type whose entity is not a class");
    }
    uint64_t size = size_of_type(type);
    if (!size)
        return ill_formed(n,
                          "the allocated type shall be a complete object type",
                          "expr.new");
    /* [basic.align]/3: "A type having an extended alignment requirement
       is an over-aligned type", new-extended above
       __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16) */
    uint64_t alignment = alignment_of_type(type);
    bool over_aligned = alignment > 16;
    qualified_type align_type;
    bool with_alignment = false;
    if (over_aligned &&
        tokens.files.behavior.has(language_feature::aligned_allocation)) {
        /* /19: "If the allocated type has new-extended alignment, the
           alignment is passed as std::align_val_t" */
        if (!align_val_type(align_type))
            return not_supported_because(
                n, "expr.new",
                "an over-aligned new without std::align_val_t declared");
        with_alignment = true;
    }
    qualified_type size_type = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    expression_meaning requested;
    /* an array of a class with a constructor or destructor: each element
       initialized ([expr.new]/22), and, with a cookie (array_cookie), the
       element count stored before the elements for delete[]
       ([abi:array-cookies]: it "is stored immediately before the first
       element", the allocation /15's "array allocation overhead") */
    uint64_t cookie = 0;
    expression_meaning element_count;
    if (array) {
        if (init)
            return not_supported_because(n, "expr.new",
                                         "an array new with an initializer");
        /* the class's operator delete[], [impl.clang] as clang looks it up,
           ::new or not */
        VTRY_SET(r, array_cookie(n, c, type, false, cookie));
    }
    /* [expr.new]/(array of class type): the destructor is potentially
       invoked for the elements ([class.dtor]/16, [dcl.fct.def.delete]/2): a
       deleted one is ill-formed (as clang; C++98 has no deleted
       destructors) [impl.clang] */
    if (array && c &&
        tokens.files.behavior.has(language_feature::rvalue_references) &&
        destructor_deleted(c))
        return ill_formed(n,
                          "a program that refers to a deleted function "
                          "implicitly or explicitly is ill-formed: the "
                          "destructor of the elements is deleted",
                          "class.dtor");
    bool elements =
        array && c &&
        (entity_table[c].first_constructor || destroys(c) || cookie);
    if (array && bound) {
        /* [expr.new]/9: "If the expression in a noptr-new-declarator is
           present, it is implicitly converted to std::size_t." */
        expression_meaning b;
        VTRY_SET(r, expression(bound, b));
        VTRY_SET(r, operand_value(n, b));
        const struct type &bt = type_table[b.type.unqualified()];
        bool integral = (bt.kind == type_kind::fundamental &&
                         fundamental_type_is_integral(bt.fundamental)) ||
                        bt.kind == type_kind::enumeration;
        if (!integral)
            /* a class's conversion function; a floating value, which clang
               refuses */
            return not_supported_because(n, "expr.new",
                                         "an array new bound that is not of "
                                         "integral or enumeration type");
        bool negative = false;
        if (b.evaluation == constness::constant &&
            bt.kind == type_kind::fundamental &&
            fundamental_type_is_signed(tokens.files.behavior, bt.fundamental) &&
            static_cast<int64_t>(b.value) < 0)
            negative = true;
        VTRY_SET(r, implicit_conversion(n, b, size_type, false));
        if (b.evaluation == constness::constant) {
            /* "The expression is erroneous if: the expression is of
               non-class type and its value before converting to std::size_t
               is less than zero; ... its value is such that the size of the
               allocated object would exceed the implementation-defined
               limit ... if the expression is a core constant expression,
               the program is ill-formed" */
            if (negative || (b.value && size > UINT64_MAX / 2 / b.value))
                return ill_formed(n,
                                  "the expression in a noptr-new-declarator "
                                  "is erroneous: negative, or the allocated "
                                  "object too large",
                                  "expr.new");
            requested = integer_constant(fundamental_type::unsigned_long_int,
                                         b.value * size + cookie, at);
            element_count = integer_constant(
                fundamental_type::unsigned_long_int, b.value, at);
        } else {
            requested.type = size_type;
            requested.category = value_category::prvalue;
            requested.evaluation = constness::not_constant;
            uint32_t counted = b.node;
            if (elements) {
                /* the count, held: the size's and the elements' */
                element_count = b;
                expression_meaning held = b;
                counted = expression_node(semantic_kind::array_count, held, at);
            }
            requested.node = expression_node(semantic_kind::array_size,
                                             requested, at, counted);
            semantics[requested.node].value = size;
            semantics[requested.node].d = static_cast<uint32_t>(cookie);
        }
    } else {
        if (array && known_count && size > UINT64_MAX / 2 / known_count)
            return ill_formed(n,
                              "the expression in a noptr-new-declarator is "
                              "erroneous: the allocated object too large",
                              "expr.new");
        if (array) {
            size *= known_count;
            element_count = integer_constant(
                fundamental_type::unsigned_long_int, known_count, at);
        }
        /* [expr.new]/15: "the new-expression passes the amount of space
           requested to the allocation function as the first argument of
           type std::size_t" */
        requested = integer_constant(fundamental_type::unsigned_long_int,
                                     size + cookie, at);
    }
    /* /18: "Overload resolution is performed on a function call created by
       assembling an argument list.  The first argument is the amount of
       space requested, and has type std::size_t. ... If the new-placement
       syntax is used, the initializer-clauses in its expression-list are
       the succeeding arguments." */
    owned_array<expression_meaning> arguments;
    owned_array<expression_meaning> placement_arguments;
    if (placement) {
        if (placement.size() != 3)
            return not_supported_because(
                placement.index(), "expr.new",
                "a new-placement without a parenthesized expression list");
        VTRY_SET(r,
                 expression_list_meanings(placement[1], placement_arguments));
    }
    allocation_function kind = allocation_function::new_;
    if (array)
        kind = allocation_function::new_array;
    owned_array<entity_index> set;
    bool in_class = false;
    entity_index scope = c;
    if (global)
        scope = 0;
    entity_index templates = 0;
    VTRY_SET(r, allocation_functions(n, kind, scope, set, in_class, false,
                                     &templates));
    owned_array<overload_candidate> candidates;
    owned_array<uint32_t> first_arguments;
    owned_array<template_argument> all;
    size_t chosen = 0;
    bool none_viable = false;
    /* /19: with a new-extended alignment the first lookup passes
       std::align_val_t after the size; "If no matching function is found
       ... overload resolution is performed again without it" */
    for (;;) {
        arguments.clear();
        candidates.clear();
        first_arguments.clear();
        all.clear();
        arguments.append(requested);
        if (with_alignment) {
            expression_meaning a;
            a.type = align_type;
            a.category = value_category::prvalue;
            a.evaluation = constness::constant;
            a.value = alignment;
            a.node = expression_node(semantic_kind::constant, a, at);
            arguments.append(a);
        }
        for (const expression_meaning &e : placement_arguments)
            arguments.append(e);
        for (entity_index f : set) {
            overload_candidate k;
            k.function = f;
            k.type = entity_table[f].type;
            candidates.append(k);
            first_arguments.append(0);
        }
        /* [expr.new]/21, [temp.over]/1: the specializations of the class's
           allocation function templates deduced from the arguments,
           [static] candidates without an object */
        if (templates)
            VTRY_SET(r, member_template_candidates(
                            n, templates, false, arguments.data(),
                            static_cast<uint32_t>(arguments.size()), candidates,
                            &first_arguments, &all));
        none_viable = false;
        VTRY_SET(r, best_viable_function(n, candidates, nullptr, arguments,
                                         chosen, &none_viable));
        if (!none_viable || !with_alignment)
            break;
        with_alignment = false;
    }
    if (none_viable) {
        if (allocation_names_spelled() != allocation_declarations_analysed ||
            tokens.files.behavior.microsoft_extensions)
            /* a declaration not analysed may be the one chosen; Microsoft's
               mode falls back from operator new[] to operator new (clang) */
            return not_supported_because(n, "expr.new",
                                         "a new with no viable allocation "
                                         "function, in a case not diagnosed");
        return ill_formed(n,
                          "overload resolution fails when there is no "
                          "viable function, and the invocation is "
                          "ill-formed",
                          "over.match");
    }
    entity_index function = candidates[chosen].function;
    if (candidates[chosen].template_index) {
        /* [temp.inst]/4: the specialization chosen is referenced: its
           definition is instantiated */
        const struct function_template ft =
            function_templates[candidates[chosen].template_index - 1];
        VTRY_SET(r, instantiate(n, ft.entity, &all[first_arguments[chosen]],
                                ft.parameter_count, function));
    }
    expression_meaning call;
    VTRY_SET(r, call_function(n, at, function, arguments, call));
    bool reserved = reserved_placement(function) && !array;
    if (reserved) {
        /* [new.delete.placement]/1-/3: the reserved non-allocating form
           "Returns: ptr" and "Intentionally performs no other action",
           and "The provisions of [basic.stc.dynamic] do not apply" (no
           null result): clang makes no call, the pointer is the
           allocation */
        call.node = semantics.items(semantics[call.node].first)[1];
    }
    /* the pointer to the object, which its initialization is of */
    m = expression_meaning();
    m.type = qualified_type::make(type_table.pointer_to(type), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    expression_meaning pointer = m;
    pointer.node = expression_node(semantic_kind::allocated, pointer, at);
    expression_meaning object;
    object.type = type;
    object.category = value_category::lvalue;
    object.evaluation = constness::not_constant;
    object.node =
        expression_node(semantic_kind::indirection, object, at, pointer.node);
    initialized_value value;
    owned_array<expression_meaning> none;
    if (!init) {
        /* [dcl.init]/7: "To default-initialize an object of type T means:
           (7.1) If T is a (possibly cv-qualified) class type, constructors
           are considered. ... (7.3) Otherwise, no initialization is
           performed."  /7: "If a program calls for the
           default-initialization of an object of a const-qualified type
           T, T shall be a const-default-constructible class type" */
        if (c && entity_table[c].kind == entity_kind::class_type &&
            default_constructor_deleted(c))
            return not_supported_because(n, "class.default.ctor",
                                         "a default-initialized new of a class "
                                         "with a deleted default constructor");
        if (c && entity_table[c].first_constructor) {
            r = construct(n, type, none, object.node, value);
        } else if (type.cv() & cv_const) {
            bool constructible = c && const_default_constructible(c);
            if (!constructible)
                return ill_formed(n,
                                  "the default-initialization of an object "
                                  "of a const-qualified type needs a "
                                  "const-default-constructible class type",
                                  "dcl.init");
        }
    } else if (init.is(syntax_kind::braced_init_list)) {
        r = list_initialization(init, type, true, value);
    } else if (init.is(syntax_kind::new_initializer)) {
        bool has_list =
            init.size() == 3 && init[1].is(syntax_kind::initializer_list);
        owned_array<expression_meaning> list;
        if (has_list)
            VTRY_SET(r, expression_list_meanings(init[1], list));
        if (init.size() == 2 || (has_list && list.empty())) {
            /* [dcl.init]/11: "An object whose initializer is an empty set
               of parentheses, i.e., (), shall be value-initialized."; /8:
               (8.3) "Otherwise, the object is zero-initialized." */
            if (c && entity_table[c].first_constructor) {
                VTRY_SET(r, construct(n, type, none, object.node, value));
                semantic_node &initialization = semantics[value.node];
                if (initialization.kind != semantic_kind::construct)
                    return not_supported_because(
                        n, "dcl.init",
                        "a value-initialized new whose initialization is not a "
                        "constructor call");
                /* [dcl.init]/8.2: value-initialization zeroes the object
                   before calling a non-user-provided default constructor. */
                if (entity_table[initialization.b].implicit ||
                    entity_table[c].defaulted_default_constructor)
                    initialization.op = 1;
            } else if (c && (entity_table[c].constructor_templates ||
                             abstract_class(type))) {
                return not_supported_because(
                    n, "dcl.init",
                    "a value-initialized new of a class without a constructor");
            } else {
                expression_meaning zero;
                zero.type = qualified_type::make(type.unqualified(), cv_none);
                zero.category = value_category::prvalue;
                zero.evaluation = constness::constant;
                value.node = expression_node(semantic_kind::zero, zero, at);
            }
        } else if (has_list) {
            /* [dcl.init]/17.6.2: the constructor called with the expanded
               expression-list as its arguments. */
            /* (a class whose constructors are templates only, as one
               whose implicit copy constructor is not declared:
               libc++'s __shared_ptr_emplace) */
            if (c && (entity_table[c].first_constructor ||
                      entity_table[c].constructor_templates))
                r = construct(n, type, list, object.node, value);
            else if (list.size() == 1)
                r = initialize_with(n, list[0], type, true, false, value);
            else if (c)
                return not_supported_because(
                    n, "dcl.init",
                    "a braced new initializer of several clauses for a class "
                    "with no ctor");
            else
                return ill_formed(n,
                                  "if the entity being initialized does not "
                                  "have class type, the expression-list in a "
                                  "parenthesized initializer shall be a single "
                                  "expression",
                                  "dcl.init");
        } else if (init.size() == 3) {
            r = initialize_from(n, init[1], type, true, false, value);
        } else {
            return not_supported_because(
                n, "expr.new",
                "a parenthesized new initializer of an unhandled form");
        }
    } else {
        return not_supported_because(n, "expr.new",
                                     "a new initializer of an unhandled form");
    }
    if (r != analysis_result::well_formed)
        return r;
    if (value.node) {
        semantic_node &v = semantics[value.node];
        if (v.kind == semantic_kind::construct)
            v.a = object.node;
        else if (c && v.kind != semantic_kind::initializer_list &&
                 v.kind != semantic_kind::zero &&
                 !(v.kind == semantic_kind::conversion &&
                   v.op ==
                       static_cast<uint8_t>(conversion_kind::lvalue_to_rvalue)))
            /* a prvalue of the class (a call), stored in the object it
               initializes ([dcl.init]/17.6.1); an lvalue-to-rvalue copy of
               a trivially copyable object ([dcl.init]/17.6.2, [conv.lval])
               is lowered as an assignment into the allocated object */
            return not_supported_because(n, "expr.new",
                                         "a new initializer that is neither a "
                                         "constructor call nor a plain copy");
    }
    m.node = expression_node(semantic_kind::new_object, m, at, call.node,
                             value.node);
    /* /20: "If the allocation function is a non-allocating form
       ([new.delete.placement]) that returns null, the behavior is
       undefined.  Otherwise, if the allocation function returns null,
       initialization shall not be done, the deallocation function shall
       not be called, and the value of the new-expression shall be null."
       Only the implicitly declared global functions are known never to
       return null ([basic.stc.dynamic.allocation]/4) */
    if (value.node && !entity_table[function].implicit && !reserved)
        semantics[m.node].c = 1;
    if (elements) {
        if (!entity_table[function].implicit && !reserved)
            semantics[m.node].c = 1;
        semantics[m.node].d = element_count.node;
        semantics[m.node].value = cookie;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
