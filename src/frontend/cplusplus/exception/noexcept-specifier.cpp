/*
 * noexcept-specifier.cpp: [except.spec] Exception specifications, as far as a
 * declarator needs them.
 *
 *     noexcept-specifier:
 *         noexcept ( constant-expression )
 *         noexcept
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::noexcept_specifier()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (accept(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        constant_expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the noexcept-specifier",
                            "except.spec");
    }
    return finish_node(syntax_kind::noexcept_specifier, mark, first);
}

/*
 * C++14's [except.spec]/1:
 *
 *     dynamic-exception-specification:
 *         throw ( type-id-list_opt )
 *
 *     type-id-list:
 *         type-id ..._opt
 *         type-id-list , type-id ..._opt
 *
 * [diff.cpp14.except]/1: C++17 removed it, keeping throw ( ) as a
 * noexcept-specifier ("Remove dynamic exception specifications", P0003R5), and
 * [diff.cpp17.except]/1: C++20 removed that too ("Remove throw()
 * exception specification"), which clang still accepts in every edition,
 * without a diagnostic.  Other dynamic-exception-specifications are
 * parsed in every edition; the analysis diagnoses them as clang does
 * (semantic_analysis::dynamic_exception_types); throw ( ... ) is
 * Microsoft's in every edition.  C++03 has no pack expansion: the
 * ... after a type-id is C++11's (variadic templates, P7's). [impl.clang]
 */
node_index parser::dynamic_exception_specification()
{
    size_t mark = begin_node();
    token_index first = position;
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool empty = at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
                 at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 2);
    /* throw ( ... ): Microsoft's, which clang parses in every edition and
       the analysis treats as any other dynamic specification */
    bool any = at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
               at(preprocessing_op_or_punc::ELLIPSIS, 2) &&
               at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 3);
    if (!b.has(language_feature::dynamic_exception_specifications) && empty) {
        /* [diff.cpp17.except]/1: throw ( ) still means noexcept(true) */
        add_token(); /* throw */
        add_token(); /* ( */
        add_token(); /* ) */
        return finish_node(syntax_kind::noexcept_specifier, mark, first);
    }
    add_token(); /* throw */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after throw", "except.spec");
    if (any)
        add_token(); /* ... */
    else if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        size_t list_mark = begin_node();
        token_index list_first = position;
        unsigned count = 0;
        for (;;) {
            type_id();
            if (failed())
                return 0;
            if (at(preprocessing_op_or_punc::ELLIPSIS))
                return beyond_this_step(); /* a pack expansion: P7 */
            count++;
            if (!accept(preprocessing_op_or_punc::COMMA))
                break;
        }
        if (count > 1)
            finish_node(syntax_kind::type_id_list, list_mark, list_first);
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the dynamic-exception-specification",
                        "except.spec");
    return finish_node(syntax_kind::dynamic_exception_specification, mark,
                       first);
}

} // namespace cplusplus

namespace cplusplus
{

/*
 * [except.spec]/2: "The exception specification is either defined
 * implicitly, or defined explicitly by using a noexcept-specifier as a
 * suffix of a function declarator.  In a noexcept-specifier, the
 * constant-expression, if supplied, shall be a contextually converted
 * constant expression of type bool ([expr.const]); that constant
 * expression is the exception specification of the function type in which
 * the noexcept-specifier appears.  A ( token that follows noexcept is part
 * of the noexcept-specifier and does not commence an initializer.  The
 * noexcept-specifier noexcept without a constant-expression is equivalent
 * to the noexcept-specifier noexcept(true)."  [except.spec]/3, C++17's
 * [except.spec]/2: "The noexcept-specifier throw() is equivalent to
 * noexcept(true)"; it is one here (parser::dynamic_exception_specification).
 * /5: noexcept(true) is a non-throwing exception specification, and with
 * -fno-exceptions nothing analysis generates depends on it but the type.
 */
analysis_result semantic_analysis::noexcept_specifier(NoexceptSpecifier n,
                                                      bool member,
                                                      bool &non_throwing)
{
    non_throwing = true;
    if (n.size() == 1 || n.size() == 3)
        /* noexcept, throw ( ) */
        return analysis_result::well_formed;
    if (n.size() != 4)
        return not_supported_because(
            n, "except.spec", "a noexcept-specifier of unexpected shape");
    if (member && noexcept_names_later_member(n))
        /* [class.mem]/6: a member function's noexcept-specifier is a
           complete-class context, analysed with the class complete: not
           deferred yet, so a name that a later member could declare is
           not decided */
        return not_supported_because(
            n, "except.spec",
            "a noexcept-specifier naming a later class member");
    bool constant = false, value = false;
    VTRY(r, bool_constant_expression(n, n[2], constant, value));
    if (!constant)
        return ill_formed(n,
                          "in a noexcept-specifier, the constant-expression "
                          "shall be a contextually converted constant "
                          "expression of type bool",
                          "except.spec");
    non_throwing = value;
    return analysis_result::well_formed;
}

/* whether the noexcept-specifier n of a member function names the class
   being defined, which is incomplete until its closing } */
bool semantic_analysis::noexcept_needs_complete_class(SyntaxRef n)
{
    if (!defining_class || entity_table[defining_class].size)
        return false;
    for (token_index t = n.first_token(); t < n.end_token(); t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        lookup_result found =
            lookup_unqualified(tokens.identifier(t), lookup_filter::any);
        if (found.entity == defining_class)
            return true;
    }
    return false;
}

/*
 * [class.mem]/6: the noexcept-specifier of member functions of the class c
 * that name c, analysed with c complete: each function's type then says
 * whether it is non-throwing ([except.spec]/1).
 */
analysis_result semantic_analysis::member_noexcept_specifiers(entity_index c)
{
    analysis_result result = analysis_result::well_formed;
    owned_array<pending_noexcept> kept;
    entity_index scope = current_scope;
    if (defining_class && defining_class != c &&
        entity_table[defining_class].kind == entity_kind::class_type &&
        entity_table[c].enclosing == defining_class)
        return result;
    for (const pending_noexcept &p : pending_noexcepts) {
        if (entity_table[p.function].enclosing != c) {
            kept.append(p);
            continue;
        }
        current_scope = c;
        function_parameters.clear();
        bool non_throwing = false;
        analysis_result r =
            noexcept_specifier(p.specifier, false, non_throwing);
        current_scope = scope;
        result = worse(result, r);
        if (r != analysis_result::well_formed)
            continue;
        entity &f = entity_table[p.function];
        if (non_throwing)
            f.exceptions.allows = exception_allowance::none;
        else
            f.exceptions.allows = exception_allowance::all;
        if (!non_throwing)
            continue;
        const struct type old = type_table[f.type.unqualified()];
        owned_array<qualified_type> parameters;
        for (uint32_t i = 0; i < old.parameter_count; i++)
            parameters.append(type_table.parameters(f.type.unqualified())[i]);
        type_index changed = type_table.function(
            old.operand, parameters.data(), parameters.size(), old.variadic,
            true, old.function_cv, old.function_ref);
        entity_table[p.function].type =
            qualified_type::make(changed, f.type.cv());
    }
    pending_noexcepts.clear();
    for (const pending_noexcept &p : kept)
        pending_noexcepts.append(p);
    return result;
}

/*
 * [class.mem]/6: the noexcept-specifier of a member function is a
 * complete-class context: a name in it is found among all the members of
 * the class.  It is analysed where it is written, so it is decided only
 * when no identifier it uses is also written elsewhere in the class
 * (where a member of that name might be declared), unless the name is a
 * template parameter's or another scope's that [temp.local]/6 keeps
 * members from redeclaring.
 */
/* whether the identifier at t is followed by < or ::, a use of a name: a
   member that declares the name has it followed by something else (a
   primary template's declaration, an alias's =, a function's ( ...) */
bool semantic_analysis::names_template_or_scope(token_index t) const
{
    /* a name after = (a default argument or initializer) is a use: no
       declaration has it there */
    if (t > defining_members_first &&
        tokens[t - 1].kind == token_kind::operator_or_punctuator &&
        preprocessing_op_or_punc_primary(tokens[t - 1].op_or_punc) ==
            preprocessing_op_or_punc::EQUAL)
        return true;
    /* [temp.local]/6: the name of a template-parameter in a member
       template's head (class _And = _And, typename T>) is no member */
    if (t > defining_members_first && t + 1 < defining_members_end &&
        tokens[t - 1].kind == token_kind::keyword &&
        (syntax_parser.keyword_of(tokens[t - 1]) == standard_keyword::class_ ||
         syntax_parser.keyword_of(tokens[t - 1]) ==
             standard_keyword::typename_) &&
        tokens[t + 1].kind == token_kind::operator_or_punctuator) {
        preprocessing_op_or_punc next =
            preprocessing_op_or_punc_primary(tokens[t + 1].op_or_punc);
        if (next == preprocessing_op_or_punc::EQUAL ||
            next == preprocessing_op_or_punc::COMMA ||
            next == preprocessing_op_or_punc::GREATER)
            return true;
    }
    if (t + 1 >= defining_members_end ||
        tokens[t + 1].kind != token_kind::operator_or_punctuator)
        return false;
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(tokens[t + 1].op_or_punc);
    return op == preprocessing_op_or_punc::LESS ||
           op == preprocessing_op_or_punc::COLON_COLON;
}

bool semantic_analysis::noexcept_names_later_member(SyntaxRef n)
{
    token_index first = n.first_token(), end = n.end_token();
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        /* after ::, the name is looked up in that scope, not in ours
           ([basic.lookup.qual]/1) */
        if (t > first &&
            tokens[t - 1].kind == token_kind::operator_or_punctuator &&
            preprocessing_op_or_punc_primary(tokens[t - 1].op_or_punc) ==
                preprocessing_op_or_punc::COLON_COLON)
            continue;
        uint32_t name = tokens.identifier(t);
        bool elsewhere = false;
        for (token_index u = defining_members_first;
             !elsewhere && u < defining_members_end; u++)
            if ((u < first || u >= end) &&
                tokens[u].kind == token_kind::identifier &&
                tokens.identifier(u) == name && !names_template_or_scope(u))
                elsewhere = true;
        if (!elsewhere)
            continue;
        lookup_result found = lookup_unqualified(name, lookup_filter::any);
        if (!found.found_in || is_namespace(found.found_in))
            return true;
        if (found.found_in != defining_class)
            continue;
        /* a class scope binds the arguments of a specialization: those
           have no declaration among the members, and ([temp.local]/6) no
           member can redeclare a template parameter's name */
        if (!found.entity) {
            /* (a bound pack is unknown to lookup, [temp.variadic]) */
            bool pack = found.unknown && !found.ambiguous &&
                        !found.unknown_entity && found.unknown_because &&
                        !strcmp(found.unknown_because, "temp.variadic");
            if (pack || (!found.unknown && !found.ambiguous))
                continue;
            return true;
        }
        uint32_t d = entity_table[found.entity].first_declaration;
        /* a type alias declared before: no later member of the name
           can overload it ([class.mem]/6 sees the same alias) */
        if (d && entity_table[found.entity].kind == entity_kind::typedef_name &&
            entity_table.declaration(d).node.first_token() < first)
            continue;
        if (d &&
            entity_table.declaration(d).node.first_token() >=
                defining_members_first &&
            entity_table.declaration(d).node.first_token() <
                defining_members_end)
            return true;
    }
    return false;
}

/*
 * [basic.scope.param]/1: a scope of its own, made current, where the
 * parameters of function_parameters are bound (a pack's elements as one
 * bound pack): what a declarator names after its
 * parameter-declaration-clause (a noexcept-specifier, a
 * trailing-return-type) finds them.
 */
void semantic_analysis::open_parameter_scope()
{
    open_parameter_scope(function_parameters, function_empty_pack);
}

bool semantic_analysis::names_earlier_parameter(
    SyntaxRef n, const owned_array<parameter_meaning> &list)
{
    for (token_index t = n.first_token(); t < n.end_token(); t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        uint32_t name = tokens.identifier(t);
        for (const parameter_meaning &p : list) {
            if (p.pack_name == name ||
                (p.id && tokens.identifier(p.id) == name))
                return true;
        }
    }
    return false;
}

/* whether the noexcept-specifier n of a friend function defined in the class c
   names a parameter of type c, c&, c* or one of those with cv ([class.mem]/6:
   a complete-class context, which c's layout is needed for until its
   closing }) */
bool semantic_analysis::names_parameter_of_class(SyntaxRef n, entity_index c)
{
    for (token_index t = n.first_token(); t < n.end_token(); t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        uint32_t name = tokens.identifier(t);
        for (const parameter_meaning &p : function_parameters) {
            if (!p.id || tokens.identifier(p.id) != name)
                continue;
            type_index k = p.type.unqualified();
            while (type_table[k].kind == type_kind::lvalue_reference ||
                   type_table[k].kind == type_kind::rvalue_reference ||
                   type_table[k].kind == type_kind::pointer)
                k = type_table[k].operand.unqualified();
            if (type_table[k].kind == type_kind::class_type &&
                type_table[k].entity == c)
                return true;
        }
    }
    return false;
}

void semantic_analysis::open_parameter_scope(
    const owned_array<parameter_meaning> &list, uint32_t empty_pack)
{
    /* (a member function's declarator, the class being defined: the
       class's names and its template's parameters are found past it) */
    if (is_namespace(current_scope) && defining_class)
        current_scope = defining_class;
    entity_index scope = open_scope(0, true);
    owned_array<entity_index> pack;
    uint32_t pack_name = 0;
    for (const parameter_meaning &parameter : list) {
        uint32_t name = parameter.pack_name;
        if (!name && parameter.id)
            name = tokens.identifier(parameter.id);
        if (!name)
            continue;
        entity_index e = entity_table.make(entity_kind::variable, name, scope);
        entity_table[e].type = parameter.type;
        entity_table[e].automatic = true;
        if (parameter.pack_name) {
            pack.append(e);
            pack_name = parameter.pack_name;
        } else if (!entity_table.find(scope, name)) {
            entity_table.bind(scope, name, e);
        }
    }
    if (!pack_name && empty_pack)
        make_bound_pack(scope, empty_pack, nullptr, 0);
    if (pack_name && !entity_table.find(scope, pack_name))
        make_bound_pack(scope, pack_name, pack.data(),
                        static_cast<uint32_t>(pack.size()));
}

/*
 * [basic.scope.param]/1: "A function parameter (including one appearing in
 * a lambda-declarator) or function-local predefined variable
 * ([dcl.fct.def]) introduces a name in a function parameter scope", whose
 * potential scope ends at the end of the function declarator when it is
 * not a definition's: the noexcept-specifier n names the parameters of
 * function_parameters, analysed in a scope of their own.
 */
analysis_result semantic_analysis::parameter_scope_noexcept(SyntaxRef n,
                                                            bool member,
                                                            bool &non_throwing)
{
    /* [class.mem]/6: a member function template's noexcept-specifier,
       is analysed per specialization (taken as value-dependent), whose class is
       complete by then ([temp.inst]/4) */
    if (member && n.size() == 4 && in_member_template_declarator == 1) {
        non_throwing = false;
        dependent_exception_specifications.append(n);
        return analysis_result::well_formed;
    }
    if (member && in_member_template_declarator == 2)
        member = false;
    if (n.size() != 4)
        return noexcept_specifier(n, member, non_throwing);
    entity_index outer = current_scope;
    open_parameter_scope();
    analysis_result r = analysis_result::well_formed;
    bool friend_of_class =
        friend_declarator && defining_class &&
        !entity_table[defining_class].size &&
        !names_dependent_entity(n.first_token(), n.end_token()) &&
        names_parameter_of_class(n, defining_class);
    if (friend_of_class ||
        (member && !names_dependent_entity(n.first_token(), n.end_token()) &&
         noexcept_needs_complete_class(n) &&
         !names_earlier_parameter(n, function_parameters))) {
        /* [class.mem]/6: the noexcept-specifier is a complete-class context
           and it names the class being defined (libc++'s
           noexcept(__is_nothrow_swappable_v<__tuple_leaf>)): analysed at
           the class's closing } (member_noexcept_specifiers), which gives
           the function its type; until then the specification is unknown */
        non_throwing = false;
        deferred_noexcept = n;
    } else if (names_dependent_entity(n.first_token(), n.end_token()))
    /* [temp.dep.constexpr]: value-dependent in the template's own
           declaration; each specialization's declaration is analysed with
           the arguments, and the template's own type says potentially
           throwing */
    {
        non_throwing = false;
        dependent_exception_specifications.append(n);
    } else
        r = noexcept_specifier(n, member, non_throwing);
    current_scope = outer;
    return r;
}

/*
 * [except.spec]/1: the value of the dynamic-exception-specification n, whose
 * analysed types are types: throw ( ... ) allows every exception (the
 * ellipsis is a token of n's own), throw ( ) none, and otherwise the set of
 * the types, kept in exception_types sorted, each type once, so that two
 * specifications naming the same set ([except.spec]/4's "compatible") have
 * equal lists.
 */
exception_specification
semantic_analysis::dynamic_exception_value(SyntaxRef n,
                                           owned_array<uint32_t> &types)
{
    exception_specification value;
    value.written = true;
    value.allows = exception_allowance::none;
    for (auto c : n.elements())
        if (c.is(preprocessing_op_or_punc::ELLIPSIS))
            value.allows = exception_allowance::all;
    if (value.allows == exception_allowance::all || types.empty())
        return value;
    uint32_t *list = types.data();
    size_t count = types.size();
    for (size_t i = 1; i < count; i++)
        for (size_t j = i; j > 0 && list[j - 1] > list[j]; j--) {
            uint32_t moved = list[j];
            list[j] = list[j - 1];
            list[j - 1] = moved;
        }
    value.allows = exception_allowance::types;
    value.first_type = static_cast<uint32_t>(exception_types.size());
    size_t distinct = 0;
    for (size_t i = 0; i < count; i++)
        if (i == 0 || list[i] != list[i - 1]) {
            exception_types.append(list[i]);
            distinct++;
        }
    if (distinct > UINT16_MAX) {
        /* (more types than a specification records: not compared) */
        value.allows = exception_allowance::unknown;
        return value;
    }
    value.type_count = static_cast<uint16_t>(distinct);
    return value;
}

/*
 * [except.spec]/1: a function declared without an exception-specification
 * has its implicit one: /8 a destructor's and /12 a deallocation function's
 * are non-throwing (implicitly_non_throwing, which sees the declarator-id),
 * as is a function whose type says so (a typedef-name's noexcept function
 * type, [except.spec]/2) or [ext:gnu_attributes] one declared nothrow
 * (clang's: compatible with noexcept); /13 any other potentially-throwing.
 */
exception_specification
semantic_analysis::resolved_exceptions(exception_specification exceptions,
                                       qualified_type type,
                                       uint32_t gnu_flags) const
{
    if (exceptions.allows != exception_allowance::implicit)
        return exceptions;
    const struct type &f = type_table[type.unqualified()];
    exceptions.allows = exception_allowance::all;
    if ((f.kind == type_kind::function && f.non_throwing) ||
        (gnu_flags & gnu_nothrow))
        exceptions.allows = exception_allowance::none;
    return exceptions;
}

/* [except.spec]/4's "compatible": both allow every exception, both none, or
   both the same set of types */
bool semantic_analysis::same_exceptions(exception_specification a,
                                        exception_specification b) const
{
    if (a.allows != b.allows)
        return false;
    if (a.allows != exception_allowance::types)
        return true;
    if (a.type_count != b.type_count)
        return false;
    for (uint32_t i = 0; i < a.type_count; i++)
        if (exception_types[a.first_type + i] !=
            exception_types[b.first_type + i])
            return false;
    return true;
}

/* whether the function template t's declaration has a noexcept-specifier
   left value-dependent */
bool semantic_analysis::dependent_exception_specification(entity_index t) const
{
    for (const function_template &f : function_templates) {
        if (f.entity != t || !f.declaration)
            continue;
        auto d = f.declaration;
        for (SyntaxRef x : dependent_exception_specifications)
            if (x.first_token() >= d.first_token() &&
                x.end_token() <= d.end_token())
                return true;
    }
    return false;
}

} // namespace cplusplus
