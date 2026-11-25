/*
 * overload-resolution.cpp: [over.match] Overload resolution, with
 * [over.match.viable] Viable functions and [over.match.best] Best viable
 * function.
 *
 * [over.match]/2: "Overload resolution selects the function to call in seven
 * distinct contexts within the language"; /3: "First, a subset of the
 * candidate functions (those that have the proper number of arguments and
 * meet certain other conditions) is selected to form a set of viable
 * functions ([over.match.viable]).  Then the best viable function is
 * selected based on the implicit conversion sequences ([over.best.ics])
 * needed to match each argument to the corresponding parameter of each
 * viable function.  If a best viable function exists and is unique,
 * overload resolution succeeds and produces it as the result.  Otherwise
 * overload resolution fails and the invocation is ill-formed."
 *
 * The contexts analysed: a call of a named function ([over.call.func], in
 * postfix-expression.cpp's call) and the initialization of an object of class
 * type by constructor ([over.match.ctor], constructor.cpp's construct), and
 * the specializations of a name's function templates ([temp.over],
 * postfix-expression.cpp's template_call), which [over.match.best]/2.4 and 2.5
 * order; 2.6 and 2.7 never decide.
 *
 * A candidate whose conversion sequence analysis cannot tell (form::unknown)
 * is taken at its best, an Exact Match: when the best viable function of the
 * others is not better than that, what is chosen is not known.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* whether syntax tree n has a decltype-specifier anywhere */
static bool uses_decltype(SyntaxRef n)
{
    if (n.is(syntax_kind::decltype_specifier))
        return true;
    for (size_t i = 0; i < n.size(); i++) {
        SyntaxElement e = n[i];
        if (!e.is_token() && uses_decltype(e))
            return true;
    }
    return false;
}

bool semantic_analysis::list_initializer_list(qualified_type t) const
{
    const struct type &x = type_table[t.unqualified()];
    return x.kind == type_kind::class_type &&
           named(x.entity, "initializer_list") &&
           std_namespace(entity_table[x.entity].enclosing);
}

/*
 * [over.ics.list]/5: "if the parameter type is std::initializer_list<X> and
 * all the elements of the initializer list can be implicitly converted to
 * X, the implicit conversion sequence is the worst conversion necessary to
 * convert an element of the list to X"; /8: for "array of N X" the same
 * for X.  An element that is not a braced-init-list or string-literal
 * never converts to an array X: brace elision belongs to the
 * initialization ([dcl.init.aggr]/15), not to the conversion sequence.
 */
bool semantic_analysis::list_sequence_possible(SyntaxRef list,
                                               qualified_type target)
{
    const struct type &t = type_table[target.unqualified()];
    qualified_type element = target;
    if (t.kind == type_kind::class_type) {
        entity_index c = t.entity;
        if (!named(c, "initializer_list") ||
            !std_namespace(entity_table[c].enclosing) ||
            !entity_table[c].instantiation)
            return true;
        const instantiation_record &inst =
            instantiations[entity_table[c].instantiation - 1];
        if (inst.argument_count < 1 ||
            template_arguments[inst.first_argument].kind !=
                template_argument::kind_t::type)
            return true;
        element = template_arguments[inst.first_argument].type;
    } else if (t.kind == type_kind::array) {
        element = t.operand;
    } else {
        return true;
    }
    if (type_table[element.unqualified()].kind != type_kind::array)
        return true;
    owned_array<SyntaxElement> clauses;
    if (list_clauses(list, clauses) != analysis_result::well_formed)
        return true;
    for (size_t i = 0; i < clauses.size(); i++) {
        if (clauses[i].is(syntax_kind::braced_init_list)) {
            if (!list_sequence_possible(clauses[i], element))
                return false;
            continue;
        }
        bool string =
            clauses[i].is_token() &&
            tokens[clauses[i].token()].kind == token_kind::literal &&
            tokens[clauses[i].token()].literal == literal_kind::string_literal;
        if (!string)
            return false;
    }
    return true;
}

/*
 * [over.ics.list]/6: "if the parameter type is 'array of N X' or 'array of
 * unknown bound of X', if there exists an implicit conversion sequence
 * from each element of the initializer list (and from {} in the former
 * case if N exceeds the number of elements in the initializer list) to X,
 * the implicit conversion sequence is the worst such implicit conversion
 * sequence"; none when the list has more than N elements
 * ([dcl.init.aggr]/9).  An element that is itself a braced-init-list, or
 * {} to a class X, is not followed (unknown).
 */
semantic_analysis::conversion_sequence
semantic_analysis::list_array_sequence(SyntaxElement list, qualified_type array,
                                       bool user_defined)
{
    conversion_sequence unknown;
    unknown.kind = conversion_sequence::form::unknown;
    const struct type t = type_table[array.unqualified()];
    if (t.variable_bound)
        return unknown;
    owned_array<SyntaxElement> clauses;
    if (list_clauses(list, clauses) != analysis_result::well_formed)
        return unknown;
    conversion_sequence bad;
    if (t.bound_known && clauses.size() > t.bound)
        return bad;
    type_kind element = type_table[t.operand.unqualified()].kind;
    bool scalar = element == type_kind::pointer ||
                  element == type_kind::enumeration ||
                  (element == type_kind::fundamental &&
                   fundamental_of(t.operand) != fundamental_type::void_);
    if (!scalar)
        return unknown;
    conversion_sequence worst;
    worst.kind = conversion_sequence::form::standard;
    for (SyntaxElement clause : clauses) {
        if (clause.is(syntax_kind::braced_init_list) ||
            clause.is(syntax_kind::designated_initializer_clause))
            return unknown;
        substitution_mark mark = mark_substitution();
        expression_meaning e;
        substituting++;
        analysis_result r = expression(clause, e);
        conversion_sequence s;
        if (r == analysis_result::well_formed && e.node)
            s = implicit_conversion_sequence(e, t.operand, user_defined);
        substituting--;
        undo_substitution(mark);
        if (r != analysis_result::well_formed || !e.node ||
            s.kind == conversion_sequence::form::unknown)
            return unknown;
        if (s.kind == conversion_sequence::form::bad)
            return bad;
        if (compare_conversions(s, worst) > 0)
            worst = s;
    }
    worst.from_list = true;
    worst.to_array = true;
    return worst;
}

void semantic_analysis::overload_set(entity_index e,
                                     owned_array<entity_index> &out) const
{
    size_t first = out.size();
    for (entity_index link = e; link; link = entity_table[link].next_overload) {
        /* a class's copy of a base's function stands for it */
        e = link;
        if (entity_table[e].using_shadow_of)
            e = entity_table[e].using_shadow_of;
        bool seen = false;
        for (entity_index other : out)
            if (other == e)
                seen = true;
        /* [namespace.udecl]/15 (C++20 /14): "When a using-declarator brings
           declarations from a base class into a derived class, member
           functions ... in the derived class override and/or hide member
           functions ... with the same name, parameter-type-list,
           cv-qualification, and ref-qualifier (if any) in a base class
           (rather than conflicting)": the chain of a derived class's name
           ends with the base's functions its using-declaration introduced,
           after the class's own, and one of them with the signature of a
           function before it in the chain is hidden */
        const entity &x = entity_table[e];
        if (x.kind == entity_kind::function &&
            entity_table[x.enclosing].kind == entity_kind::class_type)
            for (size_t i = first; i < out.size() && !seen; i++) {
                const entity &y = entity_table[out[i]];
                if (y.enclosing != x.enclosing &&
                    y.kind == entity_kind::function &&
                    y.member_cv == x.member_cv &&
                    y.conversion_function == x.conversion_function &&
                    same_parameters(type_table, y.type.unqualified(),
                                    x.type.unqualified()))
                    seen = true;
            }
        if (!seen)
            out.append(e);
    }
}

namespace
{

using sequence = semantic_analysis::conversion_sequence;

/* a candidate's state: [over.match.viable] */
enum class viability : uint8_t { not_viable, viable, unknown };

} // namespace

analysis_result semantic_analysis::best_viable_function(
    SyntaxRef at, const owned_array<overload_candidate> &candidates,
    const expression_meaning *object,
    const owned_array<expression_meaning> &arguments, size_t &chosen,
    bool *none_viable, bool user_defined,
    const owned_array<SyntaxElement> *written_arguments)
{
    size_t n = arguments.size();
    /* each candidate's conversion sequences: the implicit object
       parameter's (any object when there is none), then the arguments' */
    size_t width = n + 1;
    owned_array<sequence> ics;
    owned_array<viability> state;
    for (const overload_candidate &c : candidates) {
        const struct type &f = type_table[c.type.unqualified()];
        size_t count = f.parameter_count;
        /* a member candidate's first argument is for its implicit object
           parameter ([over.match.oper]/3): its parameters are for the
           arguments after it */
        size_t first = 0;
        if (c.member)
            first = 1;
        size_t defaults = 0;
        if (c.function)
            defaults = entity_table[c.function].default_arguments;
        if (c.function && entity_table[c.function].unknown_defaults &&
            n < count + first)
            return not_supported(at, "dcl.fct.default");
        viability v = viability::viable;
        /* [over.match.viable]/2: "If there are m arguments in the list, all
           candidate functions having exactly m parameters are viable.  A
           candidate function having fewer than m parameters is viable only
           if it has an ellipsis in its parameter list.  ...  A candidate
           function having more than m parameters is viable only if all
           parameters following the mth have default arguments." */
        if (n < first || n - first + defaults < count ||
            (n - first > count && !f.variadic) ||
            (c.function && entity_table[c.function].constraint_unsatisfied))
            v = viability::not_viable;
        sequence s;
        s.kind = sequence::form::standard;
        s.any_object = true;
        if (object && c.function && v == viability::viable)
            s = object_sequence(*object, c.function);
        ics.append(s);
        size_t base = ics.size();
        ics.extend(n);
        for (size_t i = 0; i < n; i++) {
            /* [over.match.oper]/3.4: a synthesized candidate with the order
               of its two parameters reversed takes the arguments the other
               way round; the sequences are kept by argument */
            size_t k = i;
            if (c.reversed)
                k = n - 1 - i;
            const expression_meaning &argument = arguments[k];
            sequence a;
            if (v == viability::not_viable) {
                a.kind = sequence::form::bad;
            } else if (i < first) {
                /* [over.match.funcs]/5: the implied object argument */
                a = object_sequence(argument, c.function);
            } else if (i - first < count) {
                /* [over.match.viable]/3: "for F to be a viable function,
                   there shall exist for each argument an implicit
                   conversion sequence ([over.best.ics]) that converts that
                   argument to the corresponding parameter of F." */
                qualified_type target =
                    type_table.parameters(c.type.unqualified())[i - first];
                if (written_arguments &&
                    (*written_arguments)[k].is(syntax_kind::braced_init_list)) {
                    qualified_type list_target = target;
                    const struct type &tr = type_table[target.unqualified()];
                    /* (an array's cv-qualifiers are its elements',
                       [basic.type.qualifier]/3) */
                    qualified_type referred = tr.operand;
                    if (type_table[referred.unqualified()].kind ==
                        type_kind::array)
                        referred = type_table[referred.unqualified()].operand;
                    if (tr.kind == type_kind::rvalue_reference ||
                        (tr.kind == type_kind::lvalue_reference &&
                         (referred.cv() & cv_const)))
                        list_target = qualified_type::make(
                            tr.operand.unqualified(), cv_none);
                    if (type_table[list_target.unqualified()].kind ==
                        type_kind::class_type) {
                        /* [over.ics.list]: probe class list initialization.
                           The selected parameter is copy-list-initialized
                           in call_with or construct. */
                        substitution_mark mark = mark_substitution();
                        initialized_value value;
                        substituting++;
                        analysis_result r = list_initialization(
                            (*written_arguments)[k], list_target, true, value);
                        substituting--;
                        bool rolled_back = undo_substitution(mark);
                        if (r == analysis_result::well_formed &&
                            !list_sequence_possible((*written_arguments)[k],
                                                    list_target)) {
                            a.kind = sequence::form::bad;
                        } else if (r == analysis_result::well_formed) {
                            a.kind = sequence::form::user_defined;
                            a.target = list_target;
                            a.from_list = true;
                            a.to_initializer_list =
                                list_initializer_list(list_target);
                            if (semantics[value.node].kind ==
                                semantic_kind::construct)
                                a.function = semantics[value.node].b;
                        } else if (r != analysis_result::ill_formed ||
                                   !rolled_back) {
                            a.kind = sequence::form::unknown;
                            a.user_defined_if_viable = true;
                        }
                    } else if (type_table[list_target.unqualified()].kind ==
                               type_kind::array) {
                        a = list_array_sequence((*written_arguments)[k],
                                                list_target, user_defined);
                    } else if (argument.node) {
                        a = implicit_conversion_sequence(argument, target,
                                                         user_defined);
                    }
                } else if (argument.overload_name) {
                    a = overload_set_sequence(argument, target);
                } else {
                    a = implicit_conversion_sequence(argument, target,
                                                     user_defined);
                }
            } else {
                /* [over.ics.ellipsis]/1: "An ellipsis conversion sequence
                   occurs when an argument in a function call is matched
                   with the ellipsis parameter specification of the function
                   called" */
                a.kind = sequence::form::ellipsis;
            }
            ics[base + k] = a;
        }
        for (size_t i = 0; i <= n && v != viability::not_viable; i++) {
            const sequence &a = ics[ics.size() - width + i];
            if (a.kind == sequence::form::bad)
                v = viability::not_viable;
            else if (a.kind == sequence::form::unknown)
                v = viability::unknown;
        }
        state.append(v);
    }
    /* an unknown sequence at its best */
    sequence best_case;
    best_case.kind = sequence::form::standard;
    /* the best a user-defined one can be ([over.ics.rank]/2: worse than any
       standard conversion sequence) */
    sequence best_user_defined;
    best_user_defined.kind = sequence::form::user_defined;
    auto at_best = [&](size_t candidate, size_t i) -> const sequence & {
        const sequence &s = ics[candidate * width + i];
        if (s.kind == sequence::form::unknown) {
            if (s.user_defined_if_viable)
                return best_user_defined;
            return best_case;
        }
        return s;
    };
    /* [over.match.best]/2: "a viable function F1 is defined to be a better
       function than another viable function F2 if for all arguments i,
       ICSi(F1) is not a worse conversion sequence than ICSi(F2), and then
       (2.1) for some argument j, ICSj(F1) is a better conversion sequence
       than ICSj(F2)"; /1: "ICS1(F) is defined such that it is not a worse
       or better conversion sequence than ICS1(G) when F is a static member
       function" (compare_conversions) */
    bool order_unknown = false;
    auto better = [&](size_t f1, size_t f2) {
        /* [impl.clang] a string literal converted to char* ([diff.lex]/3)
           makes a function worse than any that needs no such conversion */
        bool writing1 = false, writing2 = false;
        for (size_t i = 0; i < width; i++) {
            writing1 |= ics[f1 * width + i].string_literal_to_char_pointer;
            writing2 |= ics[f2 * width + i].string_literal_to_char_pointer;
        }
        if (writing1 != writing2)
            return writing2;
        bool some = false;
        for (size_t i = 0; i < width; i++) {
            int c = compare_conversions(at_best(f1, i), at_best(f2, i));
            if (c > 0)
                return false;
            if (c < 0)
                some = true;
        }
        if (some)
            return true;
        const overload_candidate &x = candidates[f1], &y = candidates[f2];
        /* (2.4) "F1 is not a function template specialization and F2 is a
           function template specialization"; (2.5) "F1 and F2 are
           function template specializations, and the function template
           for F1 is more specialized than the template for F2 according
           to the partial ordering rules described in [temp.func.order]" */
        if (!x.template_index && y.template_index)
            return true;
        /* [temp.func.order]/3 (CWG2445): with a synthesized candidate of
           reversed parameters, "the order of the function parameters in
           its template is reversed" for the ordering */
        if (x.template_index && y.template_index &&
            x.template_index != y.template_index) {
            /* (an order analysis cannot tell is neither: the final check
               below makes the call unsupported) */
            bool unknown = false;
            /* (two member candidates' first argument is the object's,
               the same class's implicit object parameter, [temp.func.order]/3) */
            int order = function_template_order(
                x.template_index, y.template_index, n - (x.member && y.member),
                unknown, x.reversed, y.reversed);
            if (x.reversed != y.reversed && unknown) {
                order_unknown = true;
                return false;
            }
            if (!unknown && order != 0)
                return order > 0;
        }
        /* (2.10) "F1 is generated from a deduction-guide ([over.match.class.
           deduct]) and F2 is not": a deduction-guide a program declares is
           better than an implicit one of a constructor */
        if (x.template_index && y.template_index) {
            const function_template &fx =
                function_templates[x.template_index - 1];
            const function_template &fy =
                function_templates[y.template_index - 1];
            if (fx.guide_of && !fx.implicit_guide && fy.guide_of &&
                fy.implicit_guide)
                return true;
            /* (2.11) "F1 is generated from a non-template constructor and
               F2 is generated from a constructor template" */
            if (fx.implicit_guide && fy.implicit_guide &&
                !fx.guide_of_template && fy.guide_of_template)
                return true;
        }
        /* [ext:gnu_attributes] [impl.clang] a function with clang's
           enable_if (a true condition: libc++'s _LIBCPP_PREFERRED_OVERLOAD)
           is better than one without */
        if (x.function && y.function &&
            (entity_table[x.function].gnu_flags & gnu_enable_if) >
                (entity_table[y.function].gnu_flags & gnu_enable_if))
            return true;
        /* (2.6) "F1 is a constructor for a class D, F2 is a constructor
           for a base class B of D, and for all arguments the corresponding
           parameters of F1 and F2 have the same type": F2 is one D
           inherits ([namespace.udecl]/16) */
        if (x.function && y.function && !x.template_index &&
            !y.template_index &&
            entity_table[x.function].special == special_member::constructor &&
            !entity_table[x.function].inherited_constructor &&
            entity_table[y.function].inherited_constructor &&
            entity_table[x.function].enclosing ==
                entity_table[y.function].enclosing) {
            const qualified_type *p1 = type_table.parameters(
                entity_table[x.function].type.unqualified());
            const qualified_type *p2 = type_table.parameters(
                entity_table[y.function].type.unqualified());
            size_t count1 =
                type_table[entity_table[x.function].type.unqualified()]
                    .parameter_count;
            size_t count2 =
                type_table[entity_table[y.function].type.unqualified()]
                    .parameter_count;
            bool same = true;
            for (size_t i = 0; i < n && same; i++)
                if (i >= count1 || i >= count2 ||
                    p1[i].unqualified() != p2[i].unqualified())
                    same = false;
            if (same)
                return true;
        }
        /* (2.7, n4861 /2.5) "F1 and F2 are non-template functions with the
           same parameter-type-lists ([over.match.funcs]/4), and F1 is more
           constrained than F2": a function with a trailing requires-clause
           against one with none (subsumption of two requires-clauses is not
           analysed) */
        if (x.function && y.function && !x.template_index &&
            !y.template_index && entity_table[x.function].trailing_requires &&
            !entity_table[y.function].trailing_requires &&
            entity_table[x.function].nonstatic_member ==
                entity_table[y.function].nonstatic_member &&
            entity_table[x.function].member_cv ==
                entity_table[y.function].member_cv) {
            type_index t1 = entity_table[x.function].type.unqualified();
            type_index t2 = entity_table[y.function].type.unqualified();
            const qualified_type *p1 = type_table.parameters(t1);
            const qualified_type *p2 = type_table.parameters(t2);
            bool same = type_table[t1].parameter_count ==
                        type_table[t2].parameter_count;
            for (uint32_t i = 0; same && i < type_table[t1].parameter_count;
                 i++)
                if (p1[i] != p2[i])
                    same = false;
            if (same)
                return true;
        }
        /* (2.8) "F2 is a rewritten candidate ([over.match.oper]) and F1 is
           not"; (2.9) "F1 and F2 are rewritten candidates, and F2 is a
           synthesized candidate with reversed order of parameters and F1
           is not" */
        const overload_candidate &a = candidates[f1], &b = candidates[f2];
        if (b.rewritten && !a.rewritten)
            return true;
        return a.rewritten && b.rewritten && b.reversed && !a.reversed;
    };
    size_t w = candidates.size();
    bool unknown = false;
    for (size_t i = 0; i < candidates.size(); i++) {
        if (state[i] == viability::unknown)
            unknown = true;
        if (state[i] != viability::viable)
            continue;
        if (w == candidates.size() || better(i, w))
            w = i;
    }
    if (w == candidates.size()) {
        if (unknown)
            return not_supported(at, "over.best.ics");
        if (none_viable) {
            *none_viable = true;
            return analysis_result::well_formed;
        }
        return ill_formed(at,
                          "overload resolution fails when there is no viable "
                          "function, and the invocation is ill-formed",
                          "over.match");
    }
    /* [temp.func.order]/6: two specializations of unconstrained function
       templates that partial ordering finds equally specialized
       ([temp.deduct.partial]/10) are indistinguishable, so
       [over.match.best]/2's "otherwise the call is ill-formed" applies (a
       tie an analysis cannot tell, or one that constraints may break, is
       not this) */
    auto unconstrained_tie = [&](size_t f1, size_t f2) {
        const overload_candidate &x = candidates[f1], &y = candidates[f2];
        if (!x.template_index || !y.template_index ||
            x.template_index == y.template_index)
            return false;
        bool unknown = false;
        int order =
            function_template_order(x.template_index, y.template_index,
                                    n - (x.member && y.member), unknown);
        if (unknown || order != 0 || !x.function || !y.function)
            return false;
        /* the specializations take the same parameter types, so every
           argument's conversion sequence is the same: nothing else in
           [over.match.best]/2 can separate them */
        const type &tx =
            type_table[entity_table[x.function].type.unqualified()];
        const type &ty =
            type_table[entity_table[y.function].type.unqualified()];
        if (tx.parameter_count != ty.parameter_count)
            return false;
        const qualified_type *px =
            type_table.parameters(entity_table[x.function].type.unqualified());
        const qualified_type *py =
            type_table.parameters(entity_table[y.function].type.unqualified());
        if (!substituting || order_unknown) {
            for (uint32_t k = 0; k < tx.parameter_count; k++)
                if (px[k] != py[k])
                    return false;
        }
        /* a function's own return type or parameter written with decltype
           may fail substitution ([temp.deduct]/8), which the candidate
           set here does not always see: the tie is then not claimed.
           Likewise a constructor a derived class inherits
           ([over.match.best]/2.6). */
        if (entity_table[x.function].special == special_member::constructor &&
            entity_table[x.function].enclosing !=
                entity_table[y.function].enclosing)
            return false;
        const entity_index both[2] = {x.template_index, y.template_index};
        for (entity_index t : both) {
            SyntaxRef d = function_templates[t - 1].declaration;
            if (template_head_constrained(d) || trailing_constraint(d) ||
                uses_decltype(d))
                return false;
        }
        return true;
    };
    /* [over.match.best]/2: "If there is exactly one viable function that is
       a better function than all other viable functions, then it is the
       one selected by overload resolution; otherwise the call is
       ill-formed." */
    for (size_t i = 0; i < candidates.size(); i++) {
        if (i == w || state[i] != viability::viable)
            continue;
        if (candidates[i].function &&
            candidates[i].function == candidates[w].function &&
            candidates[i].rewritten == candidates[w].rewritten &&
            candidates[i].reversed == candidates[w].reversed)
            continue;
        if (!better(w, i) &&
            (candidates[w].rewritten || candidates[i].rewritten))
            /* [impl.clang] an ambiguity a reversed candidate makes is
               resolved for the non-reversed one with a warning
               (-Wambiguous-reversed-operator) */
            return not_supported(at, "over.match.oper");
        if (!better(w, i) &&
            (candidates[w].template_index || candidates[i].template_index) &&
            !unconstrained_tie(w, i))
            /* two specializations neither more specialized (or partial
               ordering analysis cannot tell): clang's ambiguity, not
               claimed */
            return not_supported(at, "temp.func.order");
        bool user_defined = false;
        for (size_t k = 0; k < width; k++)
            if (ics[w * width + k].kind ==
                    conversion_sequence::form::user_defined &&
                ics[i * width + k].kind ==
                    conversion_sequence::form::user_defined &&
                ics[w * width + k].function != ics[i * width + k].function)
                user_defined = true;
        bool to_arrays = false;
        for (size_t k = 0; k < width; k++)
            if (ics[w * width + k].to_array && ics[i * width + k].to_array)
                to_arrays = true;
        if (!better(w, i) && to_arrays)
            /* /3.1: lists to arrays of different bounds (CWG1307), not
               ranked here */
            return not_supported(at, "over.ics.rank");
        if (!better(w, i) && user_defined)
            /* /3.3: two user-defined conversion sequences of different
               functions are indistinguishable; clang's ambiguity is not
               claimed */
            return not_supported(at, "over.ics.rank");
        if (!better(w, i))
            return ill_formed(at,
                              "overload resolution fails when no viable "
                              "function is better than all other viable "
                              "functions, and the invocation is ill-formed",
                              "over.match.best");
    }
    for (size_t i = 0; i < candidates.size(); i++)
        if (state[i] == viability::unknown && !better(w, i))
            return not_supported(at, "over.best.ics");
    if (order_unknown)
        return not_supported(at, "temp.func.order");
    /* [over.best.ics]/10: "If a function that uses the ambiguous conversion
       sequence is selected as the best viable function, the call will be
       ill-formed" (clang's diagnostic names the conversions: not analysed) */
    for (size_t i = 0; i < width; i++)
        if (ics[w * width + i].ambiguous)
            return not_supported(at, "over.best.ics");
    chosen = w;
    return analysis_result::well_formed;
}

} // namespace cplusplus
