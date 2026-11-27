/*
 * mangling/template-arguments.cpp: [abi:mangle.template-arg] <template-args> and <template-arg> (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * A class template's specialization ([abi:mangle.name]):
 *
 *     <unscoped-template-name> ::= <unscoped-name>
 *     <prefix> ::= <template-prefix> <template-args>
 *
 * after the class's name, the template's name as a substitution candidate,
 * then the arguments; nothing for another class.
 */
bool itanium_mangler::class_template_args(entity_index c)
{
    const entity &x = analysis.entity_table[c];
    if ((x.kind != entity_kind::class_type &&
         x.kind != entity_kind::template_specialization) ||
        !x.instantiation)
        return true;
    add_candidate(analysis.specialized_template(c));
    return template_args(c);
}

/*
 * The type of non-type parameter i of the function template specialization
 * c, declared with the dependent type declared.  [temp.alias]/2: an alias
 * template's specialization is its type-id's type, so is_same<void,
 * void_t<T>>::value depends on no parameter; [impl.clang] clang then writes
 * the parameter's type in the specialization (enable_if_t<that, int>: i).
 * Told apart by a second mangling with each such alias as its type: where
 * that names no <template-param>, nothing else made the type dependent.
 *
 * The one place the mangler writes what it then takes back (doc/
 * PLAN-mangling.md M5): which alias specialization is in a value-dependent
 * argument, and whether its type is the only dependent part, is what
 * mangling the type finds out; a query apart from it would walk every
 * form type() and the expressions do.  The second mangling sees none of
 * the candidates before it (a <template-param> it writes is not one
 * substituted), and both are taken back.
 */
bool itanium_mangler::non_type_parameter_type(entity_index c, uint32_t i,
                                              qualified_type declared)
{
    itanium_encoder::mark before = enc.position();
    const char *refused_before = refused;
    alias_value_refused = false;
    if (type(declared))
        return true;
    if (refused_before || !alias_value_refused)
        return false;
    const char *why = refused, *where = refused_in;
    refused = nullptr;
    enc.rewind(before);
    size_t shown = enc.hide_candidates();
    uint64_t written = template_params_written;
    alias_value_trial = true;
    bool resolved = type(declared) && written == template_params_written;
    alias_value_trial = false;
    enc.rewind(before);
    enc.show_candidates(shown);
    if (!resolved) {
        refused = why;
        refused_in = where;
        return false;
    }
    refused = nullptr;
    return type(analysis.specialization_argument(c, i, 0));
}

/*
 * <template-args> of a template's specialization c.  [abi:mangle.template-arg]:
 *
 *     <template-arg> ::= <expr-primary>
 *
 * a non-type argument's value; [abi:expressions]:
 *
 *     <expr-primary> ::= L <type> <value number> E
 *
 * its parameter's type, then the value, n before a negative one's
 * magnitude; a bool's 0 or 1.
 */
bool itanium_mangler::template_args(entity_index c, entity_index function,
                                    bool pack_brackets, uint32_t limit,
                                    bool close)
{
    enc.write(terminal::I_template_args);
    uint32_t count = analysis.specialization_argument_count(c);
    if (limit < count)
        count = limit;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pack = analysis.specialization_pack(c, i);
        /* [impl.clang]: a function template's non-type parameter of a
           dependent type is mangled as "Tn", its type, then the argument
           (a later revision of [abi:mangle.template-arg] than the one
           quoted here); its type in the template's own terms (T_), as
           clang 17 and later mangle it */
        qualified_type declared;
        bool is_pack = false;
        qualified_type value_type;
        if (function && pack &&
            analysis.template_parameter_kind(function, i, is_pack,
                                             value_type) ==
                semantic_analysis::template_parameter_form_kind::value) {
            /* [impl.clang]: a non-type parameter pack's template-param-decl,
               "Tp" "Tn" and its type (a later revision of
               [abi:mangle.template-arg] than the one quoted here), when the
               pack is empty or its type depends on a template parameter:
               the type is then not deduced from the arguments */
            if (pack == 1 ||
                analysis.dependent_parameter_type(function, i, declared)) {
                enc.write(terminal::Tp_pack_parameter_decl);
                enc.write(terminal::Tn_non_type_parameter_decl);
                scoped_value<const char *> in_type(
                    mangling_part, "a non-type template parameter's type");
                if (!type(value_type))
                    return false;
            }
        } else if (function &&
                   analysis.dependent_parameter_type(function, i, declared)) {
            if (pack)
                return refuse("a non-type template parameter pack of a "
                              "dependent type");
            enc.write(terminal::Tn_non_type_parameter_decl);
            scoped_value<const char *> in_type(
                mangling_part, "a non-type template parameter's type");
            if (!non_type_parameter_type(c, i, declared))
                return false;
        }
        /* [impl.clang]: a type-parameter with a type-constraint, "Tk"
           and the concept's name before the argument (clang 17 and later,
           as Tn), with the concept's template arguments after the first
           (template<D<int>>: Tk1DIiE) */
        SyntaxRef q = 0;
        if (function)
            q = analysis.parameter_type_constraint(function, i);
        if (q) {
            /* (the type-constraint's concept-name, then its <,
               arguments and > when there are any) */
            bool qualified = q.size() == 2 &&
                             q[0].is(syntax_kind::nested_name_specifier) &&
                             q[1].is_identifier();
            if (pack || !q.size() || (!qualified && !q[0].is_identifier()))
                return false;
            enc.write(terminal::Tk_constrained_parameter_decl);
            if (!type_constraint(function, q))
                return false;
        }
        bool inner_pack = false;
        if (function && i < analysis.template_parameter_count(function) &&
            analysis.template_parameter_kind(function, i, inner_pack,
                                             value_type) ==
                semantic_analysis::template_parameter_form_kind::template_) {
            entity_index named = 0;
            uint32_t own_parameter = 0;
            uint32_t element = 0;
            if (pack)
                element = 1;
            if ((!pack || pack > 1) &&
                analysis.specialization_template_name(c, i, named,
                                                      own_parameter, element)) {
                entity_index decl_parameter = 0;
                if (own_parameter || !analysis.template_argument_declaration(
                                         function, i, named, decl_parameter))
                    return refuse("a template template argument that may need "
                                  "a template-param-decl");
                if (pack && decl_parameter)
                    return refuse("a pack of template template arguments with "
                                  "template-param-decls");
                if (decl_parameter)
                    template_template_param_decl(decl_parameter);
            }
        }
        if (!pack) {
            if (!template_arg(c, i, 0))
                return false;
            continue;
        }
        /* [abi:mangle.template-arg]: a template parameter pack's
           arguments,

               <template-arg> ::= J <template-arg>* E
        */
        if (pack_brackets)
            enc.write(terminal::J_template_arg_pack);
        for (uint32_t k = 1; k < pack; k++) {
            /* a template's own pack expanded (res<A...> in its own
               declaration): [abi:mangle.function-type]'s pack expansion of the
               parameter, <type> ::= Dp <type> */
            if (entity_index own = analysis.specialization_own_pack(c, i, k)) {
                /* a non-type pack: the expansion of the parameter as an expression
                   (sp), in a template argument (X ... E) */
                if (analysis.entity_table[own].kind == entity_kind::none) {
                    enc.write(terminal::X_template_arg_expression);
                    enc.write(terminal::sp_pack_expansion_expression);
                    template_param(analysis.entity_table[own].value);
                    enc.write(terminal::E_end);
                    continue;
                }
                /* a pattern written as a type-id (tup<T&&...>): the pack
                   expansion of its type, Dp <type> (collect_written_bindings) */
                if (analysis.entity_table[own].kind ==
                    entity_kind::dependent_written) {
                    const entity &w = analysis.entity_table[own];
                    bool found = false;
                    for (uint32_t b = 0; b < w.member_count && !found; b++) {
                        const auto &x =
                            analysis.written_bindings[w.first_member + b];
                        if (x.identifier || x.cast != w.value)
                            continue;
                        if (!type(x.type))
                            return false;
                        found = true;
                    }
                    if (!found)
                        return false;
                    continue;
                }
                /* (a template pack: not mangled yet) */
                if (analysis.entity_table[own].kind !=
                    entity_kind::typedef_name)
                    return false;
                if (!own_pack_expansion(analysis.entity_table[own].type))
                    return false;
                continue;
            }
            if (!template_arg(c, i, k))
                return false;
        }
        if (pack_brackets)
            enc.write(terminal::E_end);
    }
    if (close)
        enc.write(terminal::E_end);
    return true;
}

/*
 * [abi:mangle.function-type]'s <type> ::= Dp <type> of a template's own
 * pack, a candidate after its pattern ([abi:mangling-compression])
 */
bool itanium_mangler::own_pack_expansion(qualified_type pattern)
{
    for (size_t i = 0; i < enc.candidate_count(); i++) {
        const candidate &c = enc.candidate_at(i);
        if (c.scope || c.member_class)
            continue;
        bool same = false;
        if (c.expansion)
            same = same_candidate(c.type, pattern);
        else if (!c.type.cv() && c.type.unqualified() &&
                 record(c.type).kind == type_kind::pack_expansion)
            same = same_candidate(record(c.type).operand, pattern);
        if (same) {
            enc.substitution(i);
            return true;
        }
    }
    enc.write(terminal::Dp_pack_expansion);
    if (!type(pattern))
        return false;
    candidate c;
    c.type = pattern;
    c.expansion = true;
    enc.add_candidate(c);
    return true;
}

/*
 * A template as a template argument ([abi:mangle.template-arg]):
 *
 *     <template-arg> ::= <type>
 *
 * [impl.clang] the template's name (<unscoped-name>, or a <nested-name> of
 * its namespace), a candidate as a template prefix is; seen before, its
 * <substitution>.  In a template's own declaration a template
 * template-parameter, its <template-param>, a candidate too.
 */
bool itanium_mangler::template_template_arg(entity_index t, uint32_t parameter)
{
    if (substitution(t))
        return true;
    candidate x;
    x.scope = t;
    if (parameter) {
        template_param(parameter - 1);
        enc.add_candidate(x);
        return true;
    }
    const entities &table = analysis.entity_table;
    entity_index scope = table[t].enclosing;
    if (scope && !is_std(scope)) {
        if (table[scope].kind != entity_kind::namespace_entity)
            return false;
        enc.write(terminal::N_nested_name);
        if (!prefix(scope) || !unqualified_name(t))
            return false;
        enc.write(terminal::E_end);
    } else if (!unscoped_name(t)) {
        return false;
    }
    enc.add_candidate(x);
    return true;
}

bool itanium_mangler::template_arg_of_dependent_value(entity_index d)
{
    scoped_value<uint32_t> in_value(dependent_value_depth,
                                    dependent_value_depth + 1);
    if (!analysis.entity_table[d].value) {
        /* an expression kept as written (dependent_value_entity): X
           <expression> E, the names an alias template bound in it as
           their types */
        const struct type &w = record(analysis.entity_table[d].type);
        if (w.kind != type_kind::dependent ||
            analysis.entity_table[w.entity].kind !=
                entity_kind::dependent_written)
            return false;
        SyntaxRef n = SyntaxRef(node_index(
            static_cast<uint32_t>(analysis.entity_table[w.entity].value)));
        scoped_value<uint32_t> first(
            written_first, analysis.entity_table[w.entity].first_member);
        scoped_value<uint32_t> count(
            written_count, analysis.entity_table[w.entity].member_count);
        bool done = false;
        bool pattern = false;
        /* a pack expansion whose pattern is a type: <type>, Dp */
        if (n.is(syntax_kind::type_id))
            for (uint32_t i = 0; i < written_count && !pattern; i++) {
                const auto &b = analysis.written_bindings[written_first + i];
                if (b.identifier || b.cast != SyntaxRef(n).index().index() ||
                    record(b.type).kind != type_kind::pack_expansion)
                    continue;
                pattern = true;
                done = type(b.type);
            }
        if (!pattern) {
            enc.write(terminal::X_template_arg_expression);
            done = expression_of_written(n);
            enc.write(terminal::E_end);
        }
        return done;
    }
    enc.write(terminal::X_template_arg_expression);
    if (!expression_of_dependent_value(d))
        return false;
    enc.write(terminal::E_end);
    return true;
}

bool itanium_mangler::template_arg(entity_index c, uint32_t i, uint32_t element)
{
    if (template_arg_of(c, i, element))
        return true;
    return refuse("a template argument of a form it has no rule for");
}

bool itanium_mangler::template_arg_of(entity_index c, uint32_t i,
                                      uint32_t element)
{
    uint64_t v = 0;
    bool negative = false;
    uint32_t parameter = 0;
    entity_index named = 0;
    if (analysis.specialization_template_name(c, i, named, parameter, element))
        return template_template_arg(named, parameter);
    bool reference = false;
    if (analysis.specialization_address(c, i, named, reference, element)) {
        /* [abi:expressions]: an object or function,

               <expr-primary> ::= L _Z <encoding> E
               <expression> ::= <unary operator-name> <expression>

           its address (a pointer's argument) the second, ad
           ([abi:mangling-operator]'s & as a unary operator) before the
           first, as X <expression> E ([abi:mangle.template-arg]) */
        /* [impl.clang] an array's first element (int b[3] as an int*
           argument, [conv.array]): the address of the subobject, X ad so
           <element type> L _Z <encoding> E E E ([abi:expressions]' sub-object
           expression, offset 0 left out) */
        const struct type &object = record(analysis.entity_table[named].type);
        const struct type &parameter_type =
            record(analysis.specialization_argument(c, i, element));
        bool element_of =
            !reference && object.kind == type_kind::array &&
            parameter_type.kind == type_kind::pointer &&
            record(parameter_type.operand).kind != type_kind::array;
        if (!reference) {
            enc.write(terminal::X_template_arg_expression);
            enc.write(itanium_operator::ad_address_of);
        }
        if (element_of) {
            enc.write(terminal::so_subobject);
            if (!type(object.operand))
                return false;
        }
        if (!expr_primary_external(named))
            return false;
        if (element_of)
            enc.write(terminal::E_end);
        if (!reference)
            enc.write(terminal::E_end);
        return true;
    }
    if (analysis.specialization_dependent_value(c, i, element))
        return template_arg_of_dependent_value(
            analysis.specialization_entity(c, i, element));
    if (!analysis.specialization_value(c, i, v, negative, parameter, element))
        return type(analysis.specialization_argument(c, i, element));
    if (parameter) {
        /* [abi:mangle.template-arg]: a parameter's name, as an
           expression:

               <template-arg> ::= X <expression> E
        */
        enc.write(terminal::X_template_arg_expression);
        template_param(parameter - 1);
        enc.write(terminal::E_end);
        return true;
    }
    qualified_type of = analysis.specialization_argument(c, i, element);
    if (record(of).kind == type_kind::pointer_to_member && v) {
        /* [abi:mangle.template-arg] a pointer to member &C::m (its value
           the member, member_pointer_argument): the address of the
           member's external name, X ad L _Z <encoding> E E; a null one is
           the <expr-primary> below, L <type> 0 E */
        enc.write(terminal::X_template_arg_expression);
        enc.write(itanium_operator::ad_address_of);
        if (!expr_primary_external(static_cast<entity_index>(v)))
            return false;
        enc.write(terminal::E_end);
        return true;
    }
    if (!expr_primary_value(of, negative, v))
        return false;
    return true;
}

} // namespace cplusplus

namespace cplusplus
{

/* [impl.clang] the template-param-decl of a template template-parameter
   whose parameters are types (a later revision of [abi:mangle.template-arg]
   than the one quoted here): Tt, Ty for each parameter (Tp before a
   pack's), E */
void itanium_mangler::template_template_param_decl(entity_index parameter)
{
    enc.write(terminal::Tt_template_parameter_decl);
    uint32_t count = analysis.template_parameter_count(parameter);
    for (uint32_t k = 0; k < count; k++) {
        bool pack = false;
        qualified_type value_type;
        analysis.template_parameter_kind(parameter, k, pack, value_type);
        if (pack)
            enc.write(terminal::Tp_pack_parameter_decl);
        enc.write(terminal::Ty_type_parameter_decl);
    }
    enc.write(terminal::E_end);
}

} // namespace cplusplus
