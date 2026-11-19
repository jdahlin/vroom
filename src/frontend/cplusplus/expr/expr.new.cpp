/*
 * expr.new.cpp: [expr.new] New.
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
    auto node = syntax(n);
    if (!node.size() || node.size() > 2 ||
        !node[0].is(syntax_kind::type_specifier_seq))
        return not_supported(n, "expr.new");
    specified s;
    analysis_result r = decl_specifier_seq(node[0].index(), s, false);
    if (r != analysis_result::well_formed)
        return r;
    if (!s.has_type)
        return not_supported(n, "expr.new");
    type = s.type;
    if (node.size() == 1)
        return analysis_result::well_formed;
    SyntaxElement d = node[1];
    for (;;) {
        if (syntax(d).is(syntax_kind::noptr_new_declarator)) {
            auto bounds = syntax(d);
            if (bounds.size() != 3)
                return not_supported(d.index(), "expr.new");
            bound = syntax(d)[1];
            return analysis_result::well_formed;
        }
        if (!syntax(d).is(syntax_kind::new_declarator))
            return not_supported(n, "expr.new");
        auto declarator = syntax(d);
        if (!declarator.size() || declarator.size() > 2 ||
            !declarator[0].is(syntax_kind::ptr_operator))
            return not_supported(d.index(), "expr.new");
        declarator_meaning ignored;
        r = pointer_operator(declarator[0].index(), type, ignored);
        if (r != analysis_result::well_formed || declarator.size() == 1)
            return r;
        d = declarator[1];
    }
}

analysis_result semantic_analysis::allocation_functions(
    node_index n, allocation_function kind, entity_index c,
    owned_array<entity_index> &set, bool &in_class, bool class_complete)
{
    implicit_allocation_functions();
    uint32_t name = allocation_function_name(kind);
    in_class = false;
    if (c && !class_complete &&
        allocation_names_spelled() != allocation_declarations_analysed)
        /* the class may declare one analysis did not follow */
        return not_supported(n, "class.free");
    if (c) {
        lookup_result found = lookup_qualified(c, name, lookup_filter::any);
        if (found.ambiguous || found.unknown)
            return not_supported(n, "class.free");
        if (found.entity) {
            if (entity_table[found.entity].kind != entity_kind::function)
                return not_supported(n, "class.free");
            overload_set(found.entity, set);
            in_class = true;
            return analysis_result::well_formed;
        }
    }
    lookup_result found = lookup_in_namespace(0, name, lookup_filter::any);
    if (found.ambiguous || found.unknown || !found.entity ||
        entity_table[found.entity].kind != entity_kind::function)
        return not_supported(n, "basic.stc.dynamic");
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
    auto node = syntax(n);
    token_index at = node.first_token();
    uint32_t count = node.size();
    uint32_t i = 0;
    /* [expr.new]/11: "If the new-expression begins with a unary ::
       operator, the allocation function's name is looked up in the global
       scope." */
    bool global = false;
    if (i < count && node[i].is(preprocessing_op_or_punc::COLON_COLON)) {
        global = true;
        i++;
    }
    i++; /* new */
    SyntaxElement placement{};
    if (i < count && node[i].is(syntax_kind::new_placement))
        placement = node[i++];
    qualified_type type;
    SyntaxElement bound{};
    analysis_result r = analysis_result::well_formed;
    if (i < count && node[i].is(syntax_kind::new_type_id)) {
        r = new_type_id(node[i++], type, bound);
    } else if (i + 2 < count && node[i + 1].is(syntax_kind::type_id)) {
        r = type_id(node[i + 1], type);
        i += 3;
    } else {
        return not_supported(n, "expr.new");
    }
    if (r != analysis_result::well_formed)
        return r;
    SyntaxElement init{};
    if (i < count)
        init = node[i++];
    if (i < count)
        return not_supported(n, "expr.new");
    /* [expr.new]/6: an array: "the new-expression yields a pointer to the
       initial element (if any) of the array"; a type-id of array type is
       one too */
    bool array = static_cast<bool>(bound);
    uint64_t known_count = 0;
    if (!array && type_table[type.unqualified()].kind == type_kind::array) {
        const struct type &a = type_table[type.unqualified()];
        if (!a.bound_known)
            /* /8: the bound from the new-initializer */
            return not_supported(n, "expr.new");
        array = true;
        known_count = a.bound;
        type = a.operand;
    }
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
        return not_supported(n, "expr.new");
    entity_index c = 0;
    if (allocated.kind == type_kind::class_type) {
        c = allocated.entity;
        if (entity_table[c].kind != entity_kind::class_type)
            return not_supported(n, "class.pre");
    }
    uint64_t size = size_of_type(type);
    if (!size)
        return ill_formed(n,
                          "the allocated type shall be a complete object type",
                          "expr.new");
    /* [basic.align]/3: "A type having an extended alignment requirement
       is an over-aligned type", new-extended above
       __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16) */
    if (alignment_of_type(type) > 16)
        return not_supported(n, "expr.new");
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
            return not_supported(n, "expr.new");
        /* the class's operator delete[], as clang looks it up, ::new or
           not */
        r = array_cookie(n, c, type, false, cookie);
        if (r != analysis_result::well_formed)
            return r;
    }
    bool elements =
        array && c &&
        (entity_table[c].first_constructor || destroys(c) || cookie);
    if (array && bound) {
        /* [expr.new]/9: "If the expression in a noptr-new-declarator is
           present, it is implicitly converted to std::size_t." */
        expression_meaning b;
        r = expression(bound, b);
        if (r != analysis_result::well_formed)
            return r;
        r = operand_value(n, b);
        if (r != analysis_result::well_formed)
            return r;
        const struct type &bt = type_table[b.type.unqualified()];
        bool integral = (bt.kind == type_kind::fundamental &&
                         fundamental_type_is_integral(bt.fundamental)) ||
                        bt.kind == type_kind::enumeration;
        if (!integral)
            /* a class's conversion function; a floating value, which clang
               refuses */
            return not_supported(n, "expr.new");
        bool negative = false;
        if (b.evaluation == constness::constant &&
            bt.kind == type_kind::fundamental &&
            fundamental_type_is_signed(tokens.files.behavior, bt.fundamental) &&
            static_cast<int64_t>(b.value) < 0)
            negative = true;
        r = implicit_conversion(n, b, size_type, false);
        if (r != analysis_result::well_formed)
            return r;
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
    arguments.append(requested);
    if (placement) {
        auto p = syntax(placement);
        if (p.size() != 3)
            return not_supported(placement.index(), "expr.new");
        r = expression_list_meanings(syntax(placement)[1], arguments);
        if (r != analysis_result::well_formed)
            return r;
    }
    allocation_function kind = allocation_function::new_;
    if (array)
        kind = allocation_function::new_array;
    owned_array<entity_index> set;
    bool in_class = false;
    entity_index scope = c;
    if (global)
        scope = 0;
    r = allocation_functions(n, kind, scope, set, in_class);
    if (r != analysis_result::well_formed)
        return r;
    owned_array<overload_candidate> candidates;
    for (entity_index f : set) {
        overload_candidate k;
        k.function = f;
        k.type = entity_table[f].type;
        candidates.append(k);
    }
    size_t chosen = 0;
    bool none_viable = false;
    r = best_viable_function(n, candidates, nullptr, arguments, chosen,
                             &none_viable);
    if (r != analysis_result::well_formed)
        return r;
    if (none_viable) {
        if (allocation_names_spelled() != allocation_declarations_analysed ||
            tokens.files.behavior.microsoft_extensions)
            /* a declaration not analysed may be the one chosen; Microsoft's
               mode falls back from operator new[] to operator new (clang) */
            return not_supported(n, "expr.new");
        return ill_formed(n,
                          "overload resolution fails when there is no "
                          "viable function, and the invocation is "
                          "ill-formed",
                          "over.match");
    }
    entity_index function = set[chosen];
    expression_meaning call;
    r = call_function(n, at, function, arguments, call);
    if (r != analysis_result::well_formed)
        return r;
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
            return not_supported(n, "class.default.ctor");
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
    } else if (syntax(init).is(syntax_kind::braced_init_list)) {
        r = list_initialization(init.index(), type, true, value);
    } else if (syntax(init).is(syntax_kind::new_initializer)) {
        auto k = syntax(init);
        if (k.size() == 2) {
            /* [dcl.init]/11: "An object whose initializer is an empty set
               of parentheses, i.e., (), shall be value-initialized."; /8:
               (8.3) "Otherwise, the object is zero-initialized." */
            bool user_provided = false;
            if (c)
                for (entity_index q = entity_table[c].first_constructor; q;
                     q = entity_table[q].next_constructor)
                    if (!entity_table[q].implicit &&
                        !type_table[entity_table[q].type.unqualified()]
                             .parameter_count)
                        user_provided = true;
            if (c && user_provided) {
                /* (8.1): "if T is a (possibly cv-qualified) class type with
                   ... a default constructor that is user-provided ..., then
                   the object is default-initialized" */
                r = construct(n, type, none, object.node, value);
            } else if (c) {
                /* (8.2): zero-initialized, then default-initialized when
                   the default constructor is non-trivial; a default
                   constructor with default arguments */
                return not_supported(n, "dcl.init");
            } else {
                expression_meaning zero;
                zero.type = qualified_type::make(type.unqualified(), cv_none);
                zero.category = value_category::prvalue;
                zero.evaluation = constness::constant;
                value.node = expression_node(semantic_kind::zero, zero, at);
            }
        } else if (k.size() == 3 && k[1].is(syntax_kind::initializer_list)) {
            /* [dcl.init]/17.6.2: the constructor called with the
               expression-list as its arguments */
            if (has_pack_expansion(k[1]))
                return not_supported(n, "temp.variadic");
            if (!c)
                return ill_formed(n,
                                  "if the entity being initialized does not "
                                  "have class type, the expression-list in "
                                  "a parenthesized initializer shall be a "
                                  "single expression",
                                  "dcl.init");
            if (!entity_table[c].first_constructor)
                return not_supported(n, "dcl.init");
            owned_array<expression_meaning> list;
            r = expression_list_meanings(k[1], list);
            if (r != analysis_result::well_formed)
                return r;
            r = construct(n, type, list, object.node, value);
        } else if (k.size() == 3) {
            r = initialize_from(n, k[1], type, true, false, value);
        } else {
            return not_supported(n, "expr.new");
        }
    } else {
        return not_supported(n, "expr.new");
    }
    if (r != analysis_result::well_formed)
        return r;
    if (value.node) {
        semantic_node &v = semantics[value.node];
        if (v.kind == semantic_kind::construct)
            v.a = object.node;
        else if (c && v.kind != semantic_kind::initializer_list)
            /* a prvalue of the class (a call), stored in the object it
               initializes ([dcl.init]/17.6.1) */
            return not_supported(n, "expr.new");
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
    if (value.node && !entity_table[function].implicit)
        semantics[m.node].c = 1;
    if (elements) {
        if (!entity_table[function].implicit)
            semantics[m.node].c = 1;
        semantics[m.node].d = element_count.node;
        semantics[m.node].value = cookie;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
