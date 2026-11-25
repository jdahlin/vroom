/*
 * member-template.cpp: [temp.mem] Member templates.
 *
 * [temp.mem]/1: "A template can be declared within a class or class
 * template; such a template is called a member template.  A member
 * template can be defined within or outside its class definition or class
 * template definition."  Here a member function template defined in a
 * class of namespace scope: a function_templates record whose entity's
 * scope is the class, its name held in the class (hold_template_name);
 * a constructor template's name is held by the class's
 * constructor_templates, not bound, and its specializations are
 * candidates of a construction (construct) and of a conversion to the
 * class (class_sequence).  Its body is analysed per specialization only
 * (instantiate, instantiate_definition), the specialization a member
 * function of the class (declare_specialization).  A call x.f(args),
 * x->f(args) or, through operator(), x(args) deduces its template
 * arguments from the arguments ([temp.deduct.call]); any other use of the
 * name (unqualified in a member function, X::f, &X::f, an explicit
 * template argument list) is not analysed.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * template-head function-definition in the member-specification of class
 * c: the template declared, its definition kept for instantiation.
 */
analysis_result semantic_analysis::member_function_template(
    entity_index c, SyntaxRef n, SyntaxRef definition, uint8_t access)
{
    owned_array<SyntaxRef> parameters;
    /* (the template-head's rules are a namespace-scope template's) */
    entity_index saved_class = defining_class;
    entity_index saved_scope = current_scope;
    defining_class = 0;
    current_scope = enclosing_namespace(c);
    analysis_result head =
        template_head_parameters(n, parameters, true, true, true, true);
    current_scope = saved_scope;
    defining_class = saved_class;
    if (head != analysis_result::well_formed)
        return head;
    uint32_t lead = leading_gnu_attributes(definition);
    SyntaxTail tail(definition, lead);
    /* a constructor template's decl-specifier-seq is optional
       ([class.ctor]/1) */
    uint32_t specifiers =
        tail.size() > 0 && tail[0].is(syntax_kind::decl_specifier_seq);
    bool constructor = declares_special_member(c, tail[specifiers]);
    /* [temp.mem]/1: a conversion function template ([class.conv.fct]) */
    bool conversion = static_cast<bool>(conversion_function_id_of(definition));
    /* [dcl.decl]/4: a trailing requires-clause (trailing_constraint),
       where the template-head has none */
    uint32_t trailing = static_cast<bool>(trailing_constraint(n));
    if (trailing && head_constraint(n))
        return not_supported(n, "temp.constr");
    /* [temp.mem]/1: a member template declared in the class, defined
       outside it (member_template_definition_of_template): a
       member-declaration of a decl-specifier-seq and a declarator */
    bool defines = definition.is(syntax_kind::function_definition);
    if (!defines &&
        ((!specifiers && !constructor && !conversion) || trailing ||
         tail.size() != 2 + specifiers || tail[specifiers].is_token() ||
         !tail[specifiers + 1].is(preprocessing_op_or_punc::SEMICOLON)))
        return not_supported(n, "temp.mem");
    if (defines &&
        (tail.size() != 2 + specifiers + trailing ||
         (!specifiers && !constructor && !conversion) ||
         tail[specifiers].is_token() ||
         !tail[specifiers + 1 + trailing].is(syntax_kind::function_body)))
        return not_supported(n, "temp.mem");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(declared,
         template_parameter_scope(n, parameters, c, 0, index, scope, first));
    /* the declarator, in the template-parameters' scope (whose enclosing
       scope is the class, [basic.scope.class]) */
    specified s;
    declarator_meaning m;
    qualified_type type;
    VTRY(r,
         function_template_declarator(definition, lead, c, scope, s, m, type));
    /* an explicit-specifier's condition ([dcl.fct.spec]/2) may have
       instantiated templates, whose records came before this one: the
       parameters name the slot it takes */
    if (index != function_templates.size()) {
        index = static_cast<uint32_t>(function_templates.size());
        for (size_t i = 0; i < parameters.size(); i++) {
            entity_index e = template_parameter_entities[first + i];
            if (entity_table[e].kind != entity_kind::template_entity)
                entity_table[e].instantiation = index + 1;
        }
    }
    /* [temp.mem]/3: "A member function template shall not be virtual." */
    if (s.virtual_)
        return ill_formed_at_token(m.id,
                                   "a member function template shall not be "
                                   "virtual",
                                   "temp.mem");
    if (!m.has_id)
        return not_supported_because(n, "temp.mem", "no declarator-id");
    if (m.qualifier || m.qualified_namespace)
        return not_supported_because(n, "temp.mem",
                                     "a qualified declarator-id");
    if (m.destructor)
        return not_supported_because(n, "temp.mem", "a destructor");
    if (m.literal_operator)
        return not_supported_because(n, "temp.mem", "a literal operator");
    if (s.explicit_ && !constructor && !conversion)
        return not_supported_because(
            n, "temp.mem",
            "explicit on a member that is no constructor or "
            "conversion function");
    if (type_table[type.unqualified()].kind != type_kind::function)
        return not_supported_because(n, "temp.mem",
                                     "a declarator that is "
                                     "no function");
    if (m.allocation != allocation_function::none) {
        /* [basic.stc.dynamic.allocation]/1: a template allocation function
           has a first parameter std::size_t (a dependent one is ill-formed
           too, as is a dependent return type) and two or more parameters;
           /3 (deallocation) the same with void* first.  What is not
           exactly that is not judged here. */
        bool allocation = m.allocation == allocation_function::new_ ||
                          m.allocation == allocation_function::new_array;
        const struct type &fn = type_table[type.unqualified()];
        const qualified_type *ps = type_table.parameters(type.unqualified());
        if (fn.parameter_count < 2 || fn.variadic)
            return not_supported_because(n, "temp.mem",
                                         "an allocation or deallocation "
                                         "function template with fewer "
                                         "than two parameters");
        const struct type &result = type_table[fn.operand.unqualified()];
        const struct type &first2 = type_table[ps[0].unqualified()];
        bool fine = false;
        if (allocation) {
            fine = result.kind == type_kind::pointer && !fn.operand.cv() &&
                   !result.operand.cv() &&
                   fundamental_of(result.operand) == fundamental_type::void_ &&
                   ps[0] == qualified_type::make(
                                type_table.fundamental(
                                    fundamental_type::unsigned_long_int),
                                cv_none);
        } else {
            fine = result.kind == type_kind::fundamental &&
                   result.fundamental == fundamental_type::void_ &&
                   !fn.operand.cv() && first2.kind == type_kind::pointer &&
                   !ps[0].cv() && !first2.operand.cv() &&
                   fundamental_of(first2.operand) == fundamental_type::void_;
        }
        if (!fine)
            return not_supported_because(n, "temp.mem",
                                         "an allocation or deallocation "
                                         "function template of other form");
        /* its body is instantiated at a new-expression's use, and an error
           in it rejects neither the class that holds the template nor the
           use (instantiate() marks only a declaration that is the
           template's own: p14.cpp:60), so a definition is not analysed */
        if (defines)
            return not_supported_because(n, "temp.mem",
                                         "the definition of an allocation "
                                         "or deallocation function "
                                         "template");
        for (const parameter_meaning &p : function_parameters)
            if (p.default_argument)
                return not_supported_because(n, "temp.mem",
                                             "an allocation function "
                                             "template with a default "
                                             "argument");
        /* [class.free]/1, /3: a static member function; the declaration is
           analysed (a new-expression's overload resolution may rely on it) */
        s.static_ = true;
        allocation_declarations_analysed++;
    }
    if (s.static_ && function_cv)
        return ill_formed_at_token(m.id,
                                   "a static member function shall not be "
                                   "declared const, volatile, or const "
                                   "volatile",
                                   "class.static.mfct");
    /* C++11 [dcl.constexpr]/8: a constexpr non-static member function that
       is not a constructor is const (C++14 dropped it) */
    if (s.constexpr_ && !s.static_ && !constructor &&
        tokens.files.behavior.edition < standard_edition::cplusplus_14)
        function_cv |= cv_const;
    uint32_t gnu_flags = 0;
    token_index abi_tag = 0;
    VTRY_SET(r, function_template_attributes(n, s, m, gnu_inline_flags,
                                             "temp.mem", gnu_flags, abi_tag));
    uint32_t name = tokens.identifier(m.id);
    /* [temp.over]/1: other member function templates of the name are
       overloads; a non-template member of the name too ([temp.mem]/2):
       the first of its chain keeps the templates' held name
       (entity::member_templates), and a call weighs them together */
    entity_index prior = 0;
    if (!constructor)
        prior = entity_table.find(c, name);
    if (prior && entity_table[prior].enclosing == c &&
        entity_table[prior].kind == entity_kind::function &&
        entity_table[prior].special == special_member::none) {
        entity_index head2 = prior;
        prior = entity_table[head2].member_templates;
        if (!prior) {
            prior = make_held_name(name, c);
            entity_table[head2].member_templates = prior;
        }
    } else if (prior && entity_table[prior].enclosing != c) {
        /* [namespace.udecl]/15, [temp.mem]/2: a name a using-declaration
           brought from a base class into c; shadow it in c so c's own
           member function templates join the overload set without mutating
           the base's bindings */
        if (entity_table[prior].kind == entity_kind::function &&
            entity_table[prior].special == special_member::none) {
            entity copy = entity_table[prior];
            copy.next_overload = 0;
            copy.using_shadow_of = prior;
            entity_index shadow =
                entity_table.make(entity_kind::function, name, copy.enclosing);
            entity_index held = make_held_name(name, c);
            entity_table[held].using_shadow_of = copy.member_templates;
            copy.member_templates = held;
            entity_table[shadow] = copy;
            entity_table.bind(c, name, shadow);
            prior = held;
        } else if (member_function_template_of(prior)) {
            entity_index held = make_held_name(name, c);
            entity_table[held].using_shadow_of = prior;
            entity_table.bind(c, name, held);
            prior = held;
        } else {
            return not_supported(n, "class.mem");
        }
    } else if (prior && !member_function_template_of(prior)) {
        /* [temp.mem]/8: a conversion function template beside a
           non-template conversion function (or a member of another kind):
           the best conversion function among them ([over.match.conv]) is
           not weighed */
        return not_supported(n, "temp.mem");
    }
    if (prior) {
        /* [over.load]/2: "member function template declarations with the
           same name, the same parameter-type-list, the same trailing
           requires-clause (if any), and the same template-head cannot be
           overloaded if any of them is a static member function template
           declaration" (judged when the return types are the same too) */
        owned_array<uint32_t> group;
        function_template_overloads(prior, group);
        for (uint32_t g : group) {
            const function_template f = function_templates[g - 1];
            if (entity_table[f.entity].nonstatic_member != s.static_ ||
                entity_table[f.entity].enclosing != c)
                continue;
            if (equivalent_template(f, n, first,
                                    static_cast<uint32_t>(parameters.size()),
                                    type))
                return ill_formed_at_token(
                    m.id,
                    "member function template declarations with the same "
                    "name, parameter-type-list and template-head cannot be "
                    "overloaded if any of them is static",
                    "over.load");
        }
    }
    function_template record = new_template(
        name, c, n, index, first, static_cast<uint32_t>(parameters.size()));
    /* [dcl.fct.default]/4: the trailing parameters with a default
       argument, which a call may leave out (deduce_from_call) and its
       specializations take (instantiate) */
    for (const parameter_meaning &p : function_parameters) {
        if (!p.default_argument)
            record.default_arguments = 0;
        else
            record.default_arguments++;
    }
    record.gnu_flags = gnu_flags;
    record.abi_tag = abi_tag;
    if (defines)
        record.definition = definition;
    record.declarator = definition;
    entity_index t = record.entity;
    entity_table[t].type = type;
    entity_table[t].defined = defines;
    entity_table[t].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[t].nonstatic_member = !s.static_;
    entity_table[t].member_cv = function_cv;
    entity_table[t].access = access;
    /* [class.mfct]/1: defined in its class, inline */
    entity_table[t].inline_ = true;
    entity_table[t].constexpr_ = s.constexpr_;
    entity_table[t].immediate = s.consteval_;
    entity_table[t].operator_function = m.operator_function;
    entity_table[t].allocation = m.allocation;
    entity_table[t].conversion_function = conversion;
    entity_table[t].implemented = m.implemented;
    if (conversion) {
        entity_table[t].explicit_ = s.explicit_;
        entity_table[t].explicit_dependent = s.explicit_dependent;
    }
    if (constructor) {
        /* [class.ctor]/2: a constructor has no name to find; the class
           keeps its templates' (construct, class_sequence) */
        entity_table[t].special = special_member::constructor;
        entity_table[t].explicit_ = s.explicit_;
        entity_table[t].explicit_dependent = s.explicit_dependent;
        prior = entity_table[c].constructor_templates;
        if (!prior) {
            prior = make_held_name(name, c);
            entity_table[c].constructor_templates = prior;
        }
    }
    if (prior) {
        record.name = prior;
        function_templates.append(record);
    } else {
        hold_template_name(record, name, c);
    }
    return analysis_result::well_formed;
}

/*
 * [temp.over]/1: the specializations of the member function templates
 * named held deduced from the arguments (not the object's), appended to
 * candidates; with_object, member candidates with the implied object
 * argument ([over.match.funcs]/2), else candidates of a call with no
 * object (a static member's context).  A static member function template
 * is one too: its implicit object parameter matches any object
 * ([over.match.funcs]/4, object_sequence).  The explicit template
 * arguments as written, if any ([temp.arg.explicit]), and where each
 * candidate's complete arguments start in all, as template_candidates.
 */
analysis_result semantic_analysis::member_template_candidates(
    SyntaxRef n, entity_index held, bool with_object,
    const expression_meaning *arguments, uint32_t count,
    owned_array<overload_candidate> &candidates,
    owned_array<uint32_t> *first_arguments, owned_array<template_argument> *all,
    const owned_array<SyntaxElement> *written, SyntaxRef written_at)
{
    owned_array<uint32_t> group;
    function_template_overloads(held, group);
    size_t first = candidates.size();
    analysis_result r =
        template_candidates(n, group, arguments, count, candidates,
                            first_arguments, all, written, written_at);
    for (size_t i = first; i < candidates.size(); i++)
        candidates[i].member = with_object;
    return r;
}

/*
 * [over.match.ctor]/1, [temp.over]/1: the specializations of class c's
 * constructor templates deduced from the arguments, appended to
 * candidates; for a copy-initialization only the converting ones
 * ([class.conv.ctor]/1: not explicit).
 */
analysis_result semantic_analysis::constructor_template_candidates(
    SyntaxRef n, entity_index c, const expression_meaning *arguments,
    uint32_t count, bool copy, owned_array<overload_candidate> &candidates,
    owned_array<uint32_t> *first_arguments, owned_array<template_argument> *all)
{
    owned_array<uint32_t> group;
    function_template_overloads(entity_table[c].constructor_templates, group);
    if (copy) {
        size_t kept = 0;
        for (uint32_t g : group)
            if (!entity_table[function_templates[g - 1].entity].explicit_ ||
                entity_table[function_templates[g - 1].entity]
                    .explicit_dependent)
                group[kept++] = g;
        group.resize(kept);
    }
    owned_array<template_argument> deduced;
    if (!all)
        all = &deduced;
    owned_array<uint32_t> firsts;
    if (!first_arguments)
        first_arguments = &firsts;
    size_t before = candidates.size();
    size_t first_before = first_arguments->size();
    VTRY(r, template_candidates(n, group, arguments, count, candidates,
                                first_arguments, all));
    if (copy) {
        /* [dcl.fct.spec]/2: filter out specializations whose dependent
           explicit(bool) condition evaluated to true */
        size_t kept = before;
        for (size_t i = before; i < candidates.size(); i++) {
            if (entity_table[candidates[i].function].explicit_)
                continue;
            if (kept != i) {
                candidates[kept] = candidates[i];
                (*first_arguments)[first_before + (kept - before)] =
                    (*first_arguments)[first_before + (i - before)];
            }
            kept++;
        }
        candidates.resize(kept);
        first_arguments->resize(first_before + (kept - before));
    }
    return analysis_result::well_formed;
}

/*
 * [over.call.func]/2, /3: a call of the member functions and function
 * templates named held (the held name of the templates, whose chain also
 * has the name's non-template members): through a class member access
 * x.f(args) or x->f<T>(args), of object (a glvalue of the class or of a
 * class derived from it), or by an unqualified or class-qualified name,
 * with the object of *this or none.  [temp.over]/1: each template's
 * specialization deduced from the arguments ([temp.deduct.call]) is a
 * candidate with the non-template members ([temp.mem]/2), the implied
 * object argument ([over.match.funcs]/2) matching any object of a static
 * one (/4); the best one's definition is instantiated ([temp.inst]/4) and
 * called.
 */
void semantic_analysis::introduce_member_templates(entity_index c,
                                                   entity_index head,
                                                   entity_index from)
{
    entity_index held = entity_table[head].member_templates;
    if (!held) {
        held = entity_table.make(entity_kind::none, entity_table[head].name, c);
        entity_table[held].held_by_template = true;
        entity_table[head].member_templates = held;
    }
    for (size_t i = 0, k = function_templates.size(); i < k; i++)
        if (function_templates[i].name == from &&
            !function_templates[i].unusable) {
            function_template copy = function_templates[i];
            copy.name = held;
            function_templates.append(copy);
        }
}

analysis_result semantic_analysis::member_template_call(
    SyntaxRef n, entity_index held, const expression_meaning *object,
    const owned_array<SyntaxElement> &arguments,
    owned_array<expression_meaning> &early,
    const owned_array<SyntaxElement> *written, SyntaxRef written_at,
    expression_meaning &m)
{
    if (early.empty())
        for (SyntaxElement a : arguments) {
            token_index set_at = 0;
            if (a.is(syntax_kind::braced_init_list) ||
                named_overload_set(a, set_at))
                return not_supported(n, "temp.deduct.call");
            expression_meaning e;
            VTRY(r, expression(a, e));
            early.append(e);
        }
    for (const expression_meaning &e : early)
        if (e.entity && entity_table[e.entity].kind == entity_kind::function &&
            entity_table[e.entity].next_overload)
            return not_supported(n, "temp.deduct.call");
    owned_array<overload_candidate> candidates;
    owned_array<uint32_t> first_arguments;
    owned_array<template_argument> all;
    /* [temp.mem]/2: the non-template member functions of the name (held
       is the first of them, or the templates' name alone) are candidates
       with the templates' specializations; [temp.arg.explicit]: only a
       template takes template arguments */
    entity_index templates = member_templates_of(held);
    if (!written && entity_table[held].kind == entity_kind::function) {
        owned_array<entity_index> functions;
        overload_set(held, functions);
        function_candidates(functions, object != nullptr, candidates,
                            &first_arguments);
    }
    VTRY(r, member_template_candidates(
                n, templates, object != nullptr, early.data(),
                static_cast<uint32_t>(early.size()), candidates,
                &first_arguments, &all, written, written_at));
    if (candidates.empty()) {
        /* [temp.deduct]/8: no specialization was formed; in an immediate
           context this is a substitution failure. */
        if (substituting)
            return analysis_result::ill_formed;
        return not_supported(n, "temp.deduct");
    }
    owned_array<expression_meaning> operands;
    if (object)
        operands.append(*object);
    for (const expression_meaning &e : early)
        operands.append(e);
    size_t chosen = 0;
    bool none_viable = false;
    VTRY_SET(r, best_viable_function(n, candidates, nullptr, operands, chosen,
                                     &none_viable));
    if (none_viable)
        return ill_formed(n,
                          "overload resolution fails when there is no viable "
                          "function, and the invocation is ill-formed",
                          "over.match.viable");
    const overload_candidate &k = candidates[chosen];
    entity_index f = k.function;
    if (k.template_index) {
        /* [temp.inst]/4: the specialization chosen is referenced: its
           definition is instantiated (a candidate's declaration only was) */
        const struct function_template ft =
            function_templates[k.template_index - 1];
        VTRY_SET(r, instantiate(n, ft.entity, &all[first_arguments[chosen]],
                                ft.parameter_count, f));
    }
    if (!entity_table[f].nonstatic_member)
        return static_member_call(n, f, object, arguments, early, m);
    if (!object && !current_class() && !defining_class)
        /* [expr.prim.id]/2: "An id-expression that denotes a non-static
           data member or non-static member function of a class can only
           be used: as part of a class member access [...], or to form a
           pointer to member [...]" (X::f(a) outside any class) */
        return ill_formed(n,
                          "an id-expression that denotes a non-static member "
                          "function can only be used as part of a class "
                          "member access or to form a pointer to member",
                          "expr.prim.id");
    if (!object)
        /* [class.mfct.non-static]/3: a non-static member function named
           where no object is (a static member's context, a default
           argument): ill-formed, not analysed */
        return not_supported(n, "class.mfct.non-static");
    return bound_member_call(n, f, *object, arguments, early, m);
}

/*
 * [expr.call]/1 and [expr.ref]/1: a static member function f called,
 * through an object expression "evaluated" and then discarded
 * ([over.match.funcs]/4), or by name alone: a comma's left operand.
 */
analysis_result semantic_analysis::static_member_call(
    SyntaxRef n, entity_index f, const expression_meaning *object,
    const owned_array<SyntaxElement> &arguments,
    owned_array<expression_meaning> &early, expression_meaning &m)
{
    token_index at = n.first_token();
    entity_index object_class = 0;
    if (object)
        object_class = type_table[object->type.unqualified()].entity;
    if (!accessible(f, object_class))
        /* [class.access]/4: "access control is applied to the function
           selected by overload resolution" */
        return ill_formed(n,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
    if (object && object->category == value_category::prvalue &&
        entity_table[object_class].destructor)
        /* the temporary's destruction ([class.temporary]/4) */
        return not_supported(n, "class.temporary");
    VTRY(r, deleted_use(at, f));
    const entity x = entity_table[f];
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    callee.node = expression_node(semantic_kind::entity, callee, at, f);
    if (object)
        callee.node = expression_node(semantic_kind::comma, callee, at,
                                      object->node, callee.node);
    return call_with(n, callee, entity_table[f].type, 0, 0, arguments, early,
                     m);
}

/* whether an expression may name *this here: the function's own this, a
   lambda's enclosing function's */
bool semantic_analysis::has_this_object()
{
    if (lambda_context *l = current_lambda()) {
        if (!l->enclosing_this.unqualified())
            return false;
        if (l->default_capture != capture_default::none)
            return true;
        for (size_t i = l->first_capture; i < lambda_captures.size(); i++)
            if (!lambda_captures[i].variable &&
                !lambda_captures[i].init_capture)
                return true;
        return false;
    }
    return function_state.this_entity != 0;
}

/* member function f called on object (of its class or a class derived
   from it) with the arguments: [expr.call]/2's class member access */
analysis_result semantic_analysis::bound_member_call(
    SyntaxRef n, entity_index f, const expression_meaning &object,
    const owned_array<SyntaxElement> &arguments,
    owned_array<expression_meaning> &early, expression_meaning &m)
{
    token_index at = n.first_token();
    expression_meaning e = object;
    entity_index object_class = type_table[e.type.unqualified()].entity;
    const entity x = entity_table[f];
    if (!accessible(f, object_class))
        /* [class.access]/4: "access control is applied to the function
           selected by overload resolution" */
        return ill_formed(n,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
    if (x.enclosing != object_class) {
        VTRY(r, to_base(at, x.enclosing, e));
    }
    if (aggregate_prvalue(e) &&
        (!at_block_scope() || entity_table[x.enclosing].destructor ||
         x.enclosing != object_class))
        return not_supported(n, "class.temporary");
    VTRY(r, deleted_use(at, f));
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    expression_meaning address;
    address.type = qualified_type::make(type_table.pointer_to(e.type), cv_none);
    address.category = value_category::prvalue;
    address.evaluation = constness::not_constant;
    address.node = expression_node(semantic_kind::address, address, at, e.node);
    return call_with(n, callee, x.type, address.node, f, arguments, early, m);
}

} // namespace cplusplus
