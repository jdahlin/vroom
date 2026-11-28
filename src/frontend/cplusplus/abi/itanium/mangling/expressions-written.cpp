/*
 * mangling/expressions-written.cpp: [abi:expressions] of an expression as written in a dependent type, with
 * the names an alias template's instantiation bound in it (sema.h:
 * written_bindings) (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/* the binding of name to an instantiation's pointer or reference
   parameter, whose argument's object or function is mangled for it */
const semantic_analysis::written_binding *
itanium_mangler::written_object_binding(SyntaxElement name) const
{
    if (!name.is_token() || !name.is_identifier())
        return nullptr;
    uint32_t id = analysis.token_identifier(name.token());
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (b.identifier == id && b.specialization &&
            analysis.entity_table[b.specialization].kind !=
                entity_kind::enumerator &&
            analysis.entity_table[b.specialization].kind != entity_kind::pack &&
            analysis.entity_table[b.specialization].kind !=
                entity_kind::dependent_value)
            return &b;
    }
    return nullptr;
}

/* the element being expanded of the index pack name (value_pack) */
const semantic_analysis::written_binding *
itanium_mangler::written_value_binding(SyntaxElement name) const
{
    if (!name.is_token() || !name.is_identifier())
        return nullptr;
    uint32_t id = analysis.token_identifier(name.token());
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (b.identifier == id && b.specialization &&
            b.element == pack_element &&
            analysis.entity_table[b.specialization].kind ==
                entity_kind::enumerator)
            return &b;
    }
    return nullptr;
}

/* the number of elements the pattern of a pack expansion is expanded
   into, as clang does, for the index packs of the class it names; false
   when it names none (then sp <expression>) */
bool itanium_mangler::value_expansion(SyntaxElement pattern,
                                      uint32_t &count) const
{
    token_index first = 0, end = 0;
    if (pattern.is_node()) {
        first = pattern.first_token();
        end = pattern.end_token();
    } else {
        first = pattern.token();
        end = first + 1;
    }
    bool found = false;
    count = 0;
    for (token_index t = first; t < end; t++) {
        uint32_t id = analysis.token_identifier(t);
        if (!id)
            continue;
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.identifier != id || !b.specialization)
                continue;
            entity_kind kind = analysis.entity_table[b.specialization].kind;
            if (kind == entity_kind::pack) {
                found = true;
            } else if (kind == entity_kind::enumerator) {
                found = true;
                if (b.element + 1 > count)
                    count = b.element + 1;
            }
        }
    }
    return found;
}

/* the specialization the call node calls, when none of its operands
   depends (collect_written_calls) */
entity_index itanium_mangler::written_call(SyntaxElement call) const
{
    SyntaxRef r = call;
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (!b.identifier && b.specialization && b.cast == r.index().index() &&
            (b.element == UINT32_MAX || b.element == pack_element))
            return b.specialization;
    }
    return 0;
}

/* the type an alias template's instantiation bound the identifier name to,
   for the dependent_written type being mangled (the pack's element being
   mangled for a name bound to a pack) */
const semantic_analysis::written_binding *
itanium_mangler::written_binding(SyntaxElement name) const
{
    if (!name.is_token() || !name.is_identifier())
        return nullptr;
    uint32_t id = analysis.token_identifier(name.token());
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        /* (a specialization: a value, no type) */
        if (b.identifier == id && !b.specialization &&
            (b.element == UINT32_MAX || b.element == pack_element))
            return &b;
    }
    return nullptr;
}

/* the <type> of a type-id written in a dependent expression: a name an
   alias template bound, one of the template's type parameters, or another
   type-id the analysis recorded with its node (collect_written_casts) */
bool itanium_mangler::type_of_written_operand(
    SyntaxElement a, qualified_type &t, fundamental_type &keyword_type) const
{
    t = qualified_type();
    keyword_type = fundamental_type::none;
    SyntaxElement inner = a;
    while (inner.is_node() && inner.size() == 1 &&
           inner.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
        inner = inner[0];
    if (inner.is_token()) {
        if (const auto *b = written_binding(inner)) {
            t = b->type;
            return true;
        }
        /* a simple-type-specifier keyword alone (unsigned(4), int()) */
        keyword_type = simple_type_keyword_alone(inner.keyword());
        if (keyword_type != fundamental_type::none)
            return true;
        if (!inner.is_identifier())
            return false;
        entity_index pe =
            analysis.constraint_parameter(encoding_template, inner.token());
        /* a template parameter of a class template whose specialization
           the template is a member of: the argument it is bound to there
           (clang mangles the member's signature with the class's
           arguments substituted) */
        if (pe && !analysis.constraint_parameter_is_own(encoding_template, pe))
            for (entity_index sc =
                     analysis.entity_table[encoding_template].enclosing;
                 sc; sc = analysis.entity_table[sc].enclosing) {
                const entity &k = analysis.entity_table[sc];
                if (k.kind != entity_kind::class_type || !k.instantiation)
                    continue;
                entity_index held = analysis.entity_table.find(
                    sc, analysis.token_identifier(inner.token()));
                if (held &&
                    analysis.entity_table[held].kind ==
                        entity_kind::typedef_name &&
                    analysis.entity_table[held].argument_bound) {
                    t = analysis.entity_table[held].type;
                    return true;
                }
                break;
            }
        if (pe && analysis.entity_table[pe].kind == entity_kind::typedef_name &&
            record(analysis.entity_table[pe].type).kind ==
                type_kind::template_parameter) {
            t = analysis.entity_table[pe].type;
            return true;
        }
        /* another name: the type recorded with the type-id's node */
    }
    if (!a.is_node())
        return false;
    SyntaxRef ar = a;
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &w = analysis.written_bindings[written_first + i];
        /* (a called specialization's binding is no type written) */
        if (!w.identifier && !w.specialization &&
            w.cast == ar.index().index()) {
            t = w.type;
            return true;
        }
    }
    return false;
}

/* the <type> of a type-id written in a dependent expression: a name an
   alias template bound, one of the template's type parameters, or another
   type-id the analysis recorded with its node (collect_written_casts);
   whether it depends on a template parameter in operand_dependent */
bool itanium_mangler::type_operand_of_written(SyntaxElement a)
{
    qualified_type t;
    fundamental_type keyword_type = fundamental_type::none;
    if (!type_of_written_operand(a, t, keyword_type))
        return false;
    if (keyword_type != fundamental_type::none)
        return builtin_type(keyword_type);
    if (analysis.type_depends_on_template_parameter(t))
        operand_dependent = true;
    return type(t);
}

/* [impl.clang] a cast's <type> is its result's: a cast to a reference
   type names the referenced type */
bool itanium_mangler::type_of_cast(qualified_type t)
{
    const struct type &r = record(t);
    if (r.kind == type_kind::lvalue_reference ||
        r.kind == type_kind::rvalue_reference)
        return type(r.operand);
    return type(t);
}

bool itanium_mangler::type_operand_of_cast(SyntaxElement a)
{
    qualified_type t;
    fundamental_type keyword_type = fundamental_type::none;
    if (!type_of_written_operand(a, t, keyword_type))
        return false;
    if (keyword_type != fundamental_type::none)
        return builtin_type(keyword_type);
    if (analysis.type_depends_on_template_parameter(t))
        operand_dependent = true;
    return type_of_cast(t);
}

/* [abi:expressions] of a unary fold over a pack bound by an alias
   template: expanded, as clang does, for the elements: ( pattern op ... )
   is op e0 (op e1 (... en)), ( ... op pattern ) is ((e0 op e1) op ...) */
bool itanium_mangler::fold_of_written(SyntaxElement fold)
{
    if (fold.size() != 5)
        return false;
    bool right = fold[3].is(preprocessing_op_or_punc::ELLIPSIS);
    SyntaxElement pattern = fold[1];
    SyntaxElement op = fold[2];
    if (!right) {
        if (!fold[1].is(preprocessing_op_or_punc::ELLIPSIS))
            return false;
        pattern = fold[3];
    }
    itanium_operator code = operator_written(op, 2);
    if (code == itanium_operator::none || pack_element != UINT32_MAX)
        return false;
    /* the elements of the packs bound (one pack, or packs of one size) */
    uint32_t count = 0;
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (b.element != UINT32_MAX && b.element + 1 > count)
            count = b.element + 1;
    }
    if (count < 2)
        return false;
    bool ok = true;
    if (right) {
        for (uint32_t k = 0; k < count && ok; k++) {
            if (k + 1 < count)
                enc.write(code);
            pack_element = k;
            ok = expression_of_written(pattern);
        }
    } else {
        for (uint32_t k = 1; k < count; k++)
            enc.write(code);
        for (uint32_t k = 0; k < count && ok; k++) {
            pack_element = k;
            ok = expression_of_written(pattern);
        }
    }
    pack_element = UINT32_MAX;
    return ok;
}

/* the types of an expanded pattern (collect_written_expansion), one after
   another; expanded false when none were recorded */
bool itanium_mangler::expansion_of_written(SyntaxElement a, bool &expanded)
{
    SyntaxRef ar = a;
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &w = analysis.written_bindings[written_first + i];
        if (w.identifier || w.specialization || w.element == UINT32_MAX ||
            w.cast != ar.index().index())
            continue;
        expanded = true;
        qualified_type t = w.type;
        if (analysis.type_depends_on_template_parameter(t))
            operand_dependent = true;
        if (!type(t))
            return false;
    }
    return true;
}

/* the <template-args> of template-id id written in a dependent expression:
   I, each argument a type (type_operand_of_written), a literal or X <expression> E,
   E; dependent whether one depends on a template parameter */
bool itanium_mangler::template_args_of_written(SyntaxElement id,
                                               bool &dependent)
{
    bool saved_dependent = operand_dependent;
    operand_dependent = false;
    bool value_argument = false;
    enc.write(terminal::I_template_args);
    owned_array<SyntaxElement> written;
    template_argument_elements(id, written);
    for (size_t k = 0; k < written.size(); k++) {
        SyntaxElement a = written[k];
        if (a.is(syntax_kind::type_id)) {
            bool expanded = false;
            if (!expansion_of_written(a, expanded))
                return false;
            if (expanded)
                continue;
            if (!type_operand_of_written(a))
                return false;
            continue;
        }
        bool literal =
            a.is_token() && (!a.is_identifier() || written_value_binding(a));
        if (!literal) {
            enc.write(terminal::X_template_arg_expression);
            value_argument = true;
        }
        if (!expression_of_written(a))
            return false;
        if (!literal)
            enc.write(terminal::E_end);
    }
    enc.write(terminal::E_end);
    dependent = operand_dependent || value_argument;
    operand_dependent = saved_dependent || dependent;
    return true;
}

/* the qualifier Q of Q::name written in a dependent expression, a name (a
   template parameter, one an alias template bound) or a template-id
   (collect_written_casts gave its type): [abi:expressions]'
   <unresolved-type> for a template parameter, or the
   <unresolved-qualifier-level> of a class template's dependent
   specialization, or of an alias template's as written ([impl.clang]
   libc++'s _CheckOptionalArgsCtor<_Up>::), its <source-name> and
   <template-args>, which E closes; none not mangled.  t its type */
itanium_mangler::qualifier_level
itanium_mangler::unresolved_qualifier_form(SyntaxElement q,
                                           qualified_type &t) const
{
    fundamental_type keyword_type = fundamental_type::none;
    if (q.is(syntax_kind::simple_template_id)) {
        SyntaxRef qr = q;
        bool found = false;
        for (uint32_t i = 0; i < written_count && !found; i++) {
            const auto &w = analysis.written_bindings[written_first + i];
            if (!w.identifier && w.cast == qr.index().index()) {
                t = w.type;
                found = true;
            }
        }
        if (!found)
            return qualifier_level::none;
    } else if (!q.is_token() || !q.is_identifier() ||
               !type_of_written_operand(q, t, keyword_type)) {
        return qualifier_level::none;
    }
    const struct type &r = record(t);
    /* (an alias template's template-id naming a parameter is mangled as
       written, below, as clang does) */
    if (r.kind == type_kind::template_parameter && !t.cv() &&
        !q.is(syntax_kind::simple_template_id))
        return qualifier_level::template_param;
    if (r.kind == type_kind::dependent && !t.cv() &&
        analysis.entity_table[r.entity].kind ==
            entity_kind::template_specialization &&
        analysis.entity_table[r.entity].instantiation)
        return qualifier_level::specialization;
    if (q.is(syntax_kind::simple_template_id) && q.size() >= 3 &&
        q[0].is_identifier())
        return qualifier_level::alias_template_id;
    return qualifier_level::none;
}

/* and writes it */
bool itanium_mangler::unresolved_qualifier_level(SyntaxElement q,
                                                 qualifier_level level,
                                                 qualified_type t)
{
    if (level == qualifier_level::template_param)
        return type(t);
    if (level == qualifier_level::specialization) {
        entity_index s = record(t).entity;
        return source_name(analysis.entity_table[s].name) &&
               template_args(s, 0, false);
    }
    bool dependent = false;
    return source_name(analysis.token_identifier(q[0].token())) &&
           template_args_of_written(q, dependent);
}

/* [abi:expressions] <expression> ::= sp <expression>, a pack expansion */
bool itanium_mangler::expression_list_of_written(SyntaxElement list)
{
    owned_array<SyntaxElement> items;
    for (auto item : list.items(syntax_kind::initializer_list))
        items.append(item);
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].is(preprocessing_op_or_punc::ELLIPSIS))
            continue;
        if (i + 1 < items.size() &&
            items[i + 1].is(preprocessing_op_or_punc::ELLIPSIS)) {
            uint32_t count = 0;
            if (value_expansion(items[i], count)) {
                scoped_value<uint32_t> element(pack_element, pack_element);
                for (uint32_t k = 0; k < count; k++) {
                    pack_element = k;
                    if (!expression_of_written(items[i]))
                        return false;
                }
                continue;
            }
            enc.write(terminal::sp_pack_expansion_expression);
        }
        if (!expression_of_written(items[i]))
            return false;
    }
    return true;
}

bool itanium_mangler::expression_of_written(SyntaxElement expression)
{
    if (expression_of_written_forms(expression))
        return true;
    refuse_expression(expression);
    return false;
}

bool itanium_mangler::expression_of_written_forms(SyntaxElement expression)
{
    /* the forms in the order they are tried: a later one may match what an
       earlier one could not mangle (T(e) as a call) */
    form (itanium_mangler::*const forms[])(SyntaxElement) = {
        &itanium_mangler::fold_of_written_expression,
        &itanium_mangler::vendor_type_trait_of_written,
        &itanium_mangler::sizeof_of_written,
        &itanium_mangler::template_id_of_written,
        &itanium_mangler::cast_of_written,
        &itanium_mangler::prefix_operator_of_written,
        &itanium_mangler::operator_of_written,
        &itanium_mangler::unresolved_name_of_written,
        &itanium_mangler::postfix_of_written,
        &itanium_mangler::primary_of_written,
    };
    for (auto f : forms) {
        form r = (this->*f)(expression);
        if (r != form::none)
            return r == form::mangled;
    }
    return false;
}

/* [abi:expressions] a fold over a pack an alias template bound (fold_of_written) */
itanium_mangler::form
itanium_mangler::fold_of_written_expression(SyntaxElement expression)
{
    if (expression.is(syntax_kind::fold_expression))
        return written_form(fold_of_written(expression));
    return form::none;
}

itanium_mangler::form
itanium_mangler::vendor_type_trait_of_written(SyntaxElement expression)
{
    /* [abi:expressions] a clang type trait of types, as clang mangles it:
       a vendor extended expression, u <source-name> <template-arg>* E
       (__is_same(T, U): u9__is_same <T> <U> E) */
    if (expression.is(syntax_kind::clang_type_trait_expression) &&
        expression.size() >= 4 && expression[0].is_token()) {
        owned_buffer name;
        analysis.token_spelling(expression[0].token(), name.get());
        enc.write(terminal::u_vendor_type);
        enc.source_name(name.data(), name.size());
        for (uint32_t i = 2; i + 1 < expression.size(); i++) {
            SyntaxElement a = expression[i];
            if (a.is(preprocessing_op_or_punc::COMMA))
                continue;
            if (!a.is(syntax_kind::type_id))
                return written_form(false);
            while (a.is_node() && a.size() == 1 &&
                   a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                a = a[0];
            const auto *b = written_binding(a);
            if (!b)
                return written_form(false);
            if (!type(b->type))
                return written_form(false);
        }
        enc.write(terminal::E_end);
        return written_form(true);
    }
    return form::none;
}

itanium_mangler::form
itanium_mangler::sizeof_of_written(SyntaxElement expression)
{
    /* [abi:expressions] sizeof ... ( P ), P a template parameter pack of
       the template or of the class template it is a member of:

           <expression> ::= sZ <template-param>

       [impl.clang] the parameter's position alone, T_ or T <n> _, at any
       depth */
    if (expression.is(syntax_kind::unary_expression) &&
        expression.size() == 5 && expression[0].is(standard_keyword::sizeof_) &&
        expression[1].is(preprocessing_op_or_punc::ELLIPSIS) &&
        expression[3].is_identifier()) {
        /* (a pack an alias template's instantiation bound: [impl.clang]
           the alias's parameter's position) */
        uint32_t pack_id = analysis.token_identifier(expression[3].token());
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.identifier == pack_id && b.pack_position) {
                sizeof_pack(b.pack_position - 1);
                return written_form(true);
            }
        }
        entity_index pe = analysis.constraint_parameter(encoding_template,
                                                        expression[3].token());
        if (!pe)
            return written_form(false);
        sizeof_pack(analysis.entity_table[pe].value);
        return written_form(true);
    }
    /* [abi:expressions] sizeof and alignof of a type or an expression:

           <expression> ::= st <type>
                        ::= sz <expression>
                        ::= at <type>
                        ::= az <expression>

       [impl.clang] __alignof__ a vendor extended expression, u11__alignof__
       and its operand (a type, or X <expression> E), E */
    if (expression.is(syntax_kind::unary_expression) &&
        (expression.size() == 2 || expression.size() == 4) &&
        expression[0].is_token() &&
        (expression[0].is(standard_keyword::sizeof_) ||
         expression[0].is(standard_keyword::alignof_)) &&
        !expression[1].is(preprocessing_op_or_punc::ELLIPSIS)) {
        bool size = expression[0].is(standard_keyword::sizeof_);
        owned_buffer spelling;
        analysis.token_spelling(expression[0].token(), spelling.get());
        bool vendor = !size && spelling.size() == 11 &&
                      !memcmp(spelling.data(), "__alignof__", 11);
        SyntaxElement operand = expression[1];
        bool of_type = false;
        if (expression.size() == 4) {
            if (!punctuator(expression[1],
                            preprocessing_op_or_punc::LEFT_PARENTHESIS))
                return written_form(false);
            operand = expression[2];
            of_type = operand.is(syntax_kind::type_id);
        }
        if (vendor) {
            enc.write(terminal::u_vendor_type);
            enc.source_name("__alignof__");
            if (of_type) {
                if (!type_operand_of_written(operand))
                    return written_form(false);
            } else {
                enc.write(terminal::X_template_arg_expression);
                if (!expression_of_written(operand))
                    return written_form(false);
                enc.write(terminal::E_end);
            }
            enc.write(terminal::E_end);
            return written_form(true);
        }
        if (of_type) {
            if (size)
                enc.write(terminal::st_sizeof_type);
            else
                enc.write(terminal::at_alignof_type);
            return written_form(type_operand_of_written(operand));
        }
        if (size)
            enc.write(terminal::sz_sizeof_expression);
        else
            enc.write(terminal::az_alignof_expression);
        return written_form(expression_of_written(operand));
    }
    return form::none;
}

itanium_mangler::form
itanium_mangler::template_id_of_written(SyntaxElement expression)
{
    /* [abi:expressions] f<args> named unqualified (a callee): an
       <unresolved-name>'s <simple-id>, <source-name> <template-args>,
       each argument a type or X <expression> E */
    if (expression.is(syntax_kind::simple_template_id) &&
        expression.size() >= 3 && expression[0].is_identifier()) {
        SyntaxRef id = expression;
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (!b.specialization || b.cast != id.index().index())
                continue;
            if (!expr_primary_external(b.specialization))
                return written_form(false);
            return written_form(true);
        }
        /* (a function template's name; an alias or class template's
           names a type, a functional cast: not mangled yet) */
        entity_index named = analysis.constraint_name(
            analysis.entity_table[encoding_template].enclosing,
            expression[0].token());
        if (!named || (!analysis.names_function_template(named) &&
                       !analysis.names_member_template(named) &&
                       !analysis.names_variable_template(named)))
            return written_form(false);
        if (!source_name(analysis.token_identifier(expression[0].token())))
            return written_form(false);
        /* (with no argument that depends on a template parameter the
           specialization is known, and clang names it, L _Z <encoding> E:
           not mangled yet) */
        bool dependent = false;
        if (!template_args_of_written(expression, dependent))
            return written_form(false);
        if (!dependent)
            return written_form(
                refuse("a call of a specialization its arguments "
                       "resolve (clang's L _Z <encoding> E)"));
        return written_form(true);
    }
    return form::none;
}

/* [abi:expressions] the casts and the explicit type conversions, ( type-id ) e, the named casts, T(e), T{e}, T() */
itanium_mangler::form itanium_mangler::cast_of_written(SyntaxElement expression)
{
    /* [abi:expressions]: ( type-id ) cast-expression, cv <type>
       <expression>, the type analysed with the alias's names bound */
    if (expression.is(syntax_kind::cast_expression) && expression.size() == 4) {
        SyntaxRef r = expression;
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.cast != r.index().index())
                continue;
            enc.write(terminal::cv_cast);
            return written_form(type_of_cast(b.type) &&
                                expression_of_written(expression[3]));
        }
        return written_form(false);
    }
    /* [abi:expressions] the named casts,

           <expression> ::= dc <type> <expression>
                        ::= sc <type> <expression>
                        ::= cc <type> <expression>
                        ::= rc <type> <expression>
     */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 7 && expression[0].is_token() &&
        punctuator(expression[1], preprocessing_op_or_punc::LESS) &&
        expression[2].is(syntax_kind::type_id) &&
        expression[4].is(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        terminal named_cast = terminal::E_end;
        switch (expression[0].keyword()) {
        case standard_keyword::dynamic_cast_:
            named_cast = terminal::dc_dynamic_cast_;
            break;
        case standard_keyword::static_cast_:
            named_cast = terminal::sc_static_cast_;
            break;
        case standard_keyword::const_cast_:
            named_cast = terminal::cc_const_cast_;
            break;
        case standard_keyword::reinterpret_cast_:
            named_cast = terminal::rc_reinterpret_cast_;
            break;
        default:
            break;
        }
        if (named_cast != terminal::E_end) {
            enc.write(named_cast);
            return written_form(type_operand_of_cast(expression[2]) &&
                                expression_of_written(expression[5]));
        }
    }
    /* [abi:expressions] T(e), T(e, ...), T a type: a functional cast,

           <expression> ::= cv <type> <expression>
                        ::= cv <type> _ <expression>* E
     */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 4 &&
        ((expression[0].is_token() &&
          (expression[0].is_identifier() ||
           expression[0].keyword() != standard_keyword::none)) ||
         expression[0].is(syntax_kind::simple_template_id,
                          syntax_kind::simple_type_specifier)) &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[3].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        qualified_type operand;
        fundamental_type keyword_type = fundamental_type::none;
        if (type_of_written_operand(expression[0], operand, keyword_type)) {
            enc.write(terminal::cv_cast);
            if (!type_operand_of_written(expression[0]))
                return written_form(false);
            owned_array<SyntaxElement> arguments;
            for (auto argument :
                 expression[2].items(syntax_kind::initializer_list))
                arguments.append(argument);
            if (arguments.size() != 1)
                enc.write(terminal::underscore);
            if (!expression_list_of_written(expression[2]))
                return written_form(false);
            if (arguments.size() != 1)
                enc.write(terminal::E_end);
            return written_form(true);
        }
    }
    /* [abi:expressions] T{}, T a class the analysis recorded:

           <expression> ::= tl <type> <braced-expression>* E
     */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 2 && expression[0].is_token() &&
        expression[0].is_identifier() &&
        expression[1].is(syntax_kind::braced_init_list) &&
        expression[1].size() == 2) {
        qualified_type operand;
        fundamental_type keyword_type = fundamental_type::none;
        if (type_of_written_operand(expression, operand, keyword_type)) {
            enc.write(terminal::tl_braced_cast);
            if (!type_operand_of_written(expression))
                return written_form(false);
            enc.write(terminal::E_end);
            return written_form(true);
        }
    }
    /* T(), T an alias template's parameter or the template's own type
       parameter: cv <type> _ E */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 3 &&
        ((expression[0].is_token() &&
          (expression[0].is_identifier() ||
           expression[0].keyword() != standard_keyword::none)) ||
         expression[0].is(syntax_kind::simple_template_id,
                          syntax_kind::simple_type_specifier)) &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[2].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        qualified_type operand;
        fundamental_type keyword_type = fundamental_type::none;
        if (type_of_written_operand(expression[0], operand, keyword_type)) {
            enc.write(terminal::cv_cast);
            if (!type_operand_of_written(expression[0]))
                return written_form(false);
            enc.write(terminal::underscore);
            enc.write(terminal::E_end);
            return written_form(true);
        }
    }
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 3 && expression[0].is_token() &&
        expression[0].keyword() == standard_keyword::void_ &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[2].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        enc.write(terminal::cv_cast);
        enc.builtin_type(fundamental_type::void_);
        enc.write(terminal::underscore);
        enc.write(terminal::E_end);
        return written_form(true);
    }

    /* S<A>{x, y}: tl <type> <braced-expression>* E */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 2 &&
        expression[1].is(syntax_kind::braced_init_list)) {
        SyntaxRef whole = expression;
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.identifier || b.cast != whole.index().index())
                continue;
            enc.write(terminal::tl_braced_cast);
            if (!type(b.type))
                return written_form(false);
            if (expression[1].size() > 2 &&
                !expression_list_of_written(expression[1][1]))
                return written_form(false);
            enc.write(terminal::E_end);
            return written_form(true);
        }
    }
    return form::none;
}

itanium_mangler::form
itanium_mangler::prefix_operator_of_written(SyntaxElement expression)
{
    /* [abi:expressions] <unary operator-name> <expression> */
    if (expression.is(syntax_kind::unary_expression) &&
        expression.size() == 2) {
        itanium_operator o =
            prefix_operator(operator_written(expression[0], 1));
        if (o != itanium_operator::none) {
            enc.write(o);
            return written_form(expression_of_written(expression[1]));
        }
    }
    return form::none;
}

/* [abi:expressions] the conditional, comma and binary operators, and parentheses, which are not mangled */
itanium_mangler::form
itanium_mangler::operator_of_written(SyntaxElement expression)
{
    /* [abi:expressions] c ? a : b is qu <expression> <expression>
       <expression> */
    if (expression.is(syntax_kind::conditional_expression) &&
        expression.size() == 5) {
        enc.write(itanium_operator::qu_conditional);
        return written_form(expression_of_written(expression[0]) &&
                            expression_of_written(expression[2]) &&
                            expression_of_written(expression[4]));
    }
    if (expression.is(syntax_kind::expression) && expression.size() == 3 &&
        punctuator(expression[1], preprocessing_op_or_punc::COMMA)) {
        enc.write(itanium_operator::cm_comma);
        return written_form(expression_of_written(expression[0]) &&
                            expression_of_written(expression[2]));
    }
    if (expression.size() == 3 &&
        (expression.kind() == syntax_kind::additive_expression ||
         expression.kind() == syntax_kind::multiplicative_expression ||
         expression.kind() == syntax_kind::shift_expression ||
         expression.kind() == syntax_kind::relational_expression ||
         expression.kind() == syntax_kind::equality_expression ||
         expression.kind() == syntax_kind::and_expression ||
         expression.kind() == syntax_kind::exclusive_or_expression ||
         expression.kind() == syntax_kind::inclusive_or_expression ||
         expression.kind() == syntax_kind::logical_and_expression ||
         expression.kind() == syntax_kind::logical_or_expression)) {
        itanium_operator op = operator_written(expression[1], 2);
        if (op == itanium_operator::none)
            return written_form(false);
        enc.write(op);
        return written_form(expression_of_written(expression[0]) &&
                            expression_of_written(expression[2]));
    }
    if (expression.is(syntax_kind::primary_expression) &&
        expression.size() == 3 &&
        expression[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return written_form(expression_of_written(expression[1]));

    return form::none;
}

itanium_mangler::form
itanium_mangler::unresolved_name_of_written(SyntaxElement expression)
{
    /* (Q::template f<args>: the template keyword changes nothing mangled) */
    if (expression.is(syntax_kind::qualified_id) &&
        (expression.size() == 2 ||
         (expression.size() == 3 &&
          expression[1].is(standard_keyword::template_))) &&
        expression[0].is(syntax_kind::nested_name_specifier)) {
        SyntaxElement qual = expression[0];
        SyntaxElement member = expression[expression.size() - 1];
        bool global = qual.size() >= 1 &&
                      qual[0].is(preprocessing_op_or_punc::COLON_COLON);
        uint32_t qi = 0;
        if (global)
            qi = 1;
        /* [abi:expressions] Q::name and Q::f<args>, Q a template
           parameter, a name an alias template bound or a dependent
           specialization of a class template:

               <unresolved-name> ::= sr <unresolved-type> <base-unresolved-name>
               <unresolved-name> ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
               <base-unresolved-name> ::= <simple-id>
         */
        /* (ns::A<args>::name too: the namespaces' <source-name>s are the
           first <unresolved-qualifier-level>s) */
        uint32_t last = 0;
        if (qual.size() >= 2)
            last = qual.size() - 2;
        bool namespaces = qual.size() % 2 == 0;
        for (uint32_t i = 0; i < last; i += 2)
            namespaces = namespaces && qual[i].is_identifier();
        if (!global && namespaces && qual.size() >= 2 &&
            (member.is_identifier() ||
             (member.is(syntax_kind::simple_template_id) &&
              member.size() >= 3 && member[0].is_identifier())) &&
            ((qual[last].is_identifier() && !last) ||
             qual[last].is(syntax_kind::simple_template_id))) {
            bool named = true;
            for (uint32_t i = 0; i < last; i += 2)
                named = named && analysis.token_identifier(qual[i].token());
            qualified_type level_type;
            qualifier_level level = qualifier_level::none;
            if (named)
                level = unresolved_qualifier_form(qual[last], level_type);
            if (level == qualifier_level::template_param && last)
                level = qualifier_level::none;
            if (level != qualifier_level::none) {
                enc.write(terminal::sr_unresolved_scope);
                for (uint32_t i = 0; i < last; i += 2)
                    source_name(analysis.token_identifier(qual[i].token()));
                if (!unresolved_qualifier_level(qual[last], level, level_type))
                    return written_form(false);
                if (level != qualifier_level::template_param)
                    enc.write(terminal::E_end);
                if (member.is_identifier())
                    return written_form(
                        source_name(analysis.token_identifier(member.token())));
                bool dependent = false;
                return written_form(
                    source_name(analysis.token_identifier(member[0].token())) &&
                    template_args_of_written(member, dependent));
            }
        }
        /* [abi:expressions] T::name, T a template type parameter, its
           type a <template-param>:

               <unresolved-name> ::= sr <unresolved-type> <base-unresolved-name>
         */
        if (member.is_token() && member.is_identifier() && !global &&
            qual.size() == 2 && qual[0].is_identifier()) {
            entity_index pe = analysis.constraint_parameter(encoding_template,
                                                            qual[0].token());
            if (pe &&
                analysis.entity_table[pe].kind == entity_kind::typedef_name &&
                analysis.type_table[analysis.entity_table[pe]
                                        .type.unqualified()]
                        .kind == type_kind::template_parameter) {
                enc.write(terminal::sr_unresolved_scope);
                return written_form(
                    type(analysis.entity_table[pe].type) &&
                    source_name(analysis.token_identifier(member.token())));
            }
        }
        /* ns::C<args>::name: each namespace as written (std, not its
           inline namespace) a qualifier level, then the specialization:

               <unresolved-name> ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
         */
        if (member.is_token() && member.is_identifier() && last > qi &&
            qual[last].is(syntax_kind::simple_template_id) &&
            qual[last].size() >= 3 && qual[last][0].is_identifier()) {
            if (global)
                enc.write(terminal::gs_global_scope);
            enc.write(terminal::sr_unresolved_scope);
            for (; qi < last; qi += 2) {
                if (!qual[qi].is_identifier() ||
                    !source_name(analysis.token_identifier(qual[qi].token())))
                    return written_form(false);
            }
            bool dependent = false;
            if (!source_name(
                    analysis.token_identifier(qual[last][0].token())) ||
                !template_args_of_written(qual[last], dependent))
                return written_form(false);
            /* (with no dependent argument the member is known, and clang
               names it, L _Z <encoding> E: not mangled yet) */
            if (!dependent)
                return written_form(
                    refuse("a member of a specialization its arguments "
                           "resolve (clang's L _Z <encoding> E)"));
            enc.write(terminal::E_end);
            return written_form(
                source_name(analysis.token_identifier(member.token())));
        }
        if (member.is_token() && member.is_identifier()) {
            entity_index scope = 0;
            bool first_lookup = !global;
            while (qi + 1 < qual.size()) {
                if (!qual[qi].is_identifier())
                    return written_form(false);
                uint32_t id = analysis.token_identifier(qual[qi].token());
                entity_index next = 0;
                if (first_lookup) {
                    entity_index s =
                        analysis.entity_table[encoding_template].enclosing;
                    for (;; s = analysis.entity_table[s].enclosing) {
                        next = analysis.entity_table.find(s, id);
                        if (next || !s)
                            break;
                    }
                    first_lookup = false;
                } else {
                    next = analysis.entity_table.find(scope, id);
                }
                if (!next)
                    return written_form(false);
                scope = next;
                qi += 2;
            }
            entity_index target = analysis.entity_table.find(
                scope, analysis.token_identifier(member.token()));
            /* ([namespace.unnamed]/1: a member of the namespace's unnamed
               namespace is found as the namespace's, json's
               ::nlohmann::to_json) */
            if (!target && analysis.entity_table[scope].kind ==
                               entity_kind::namespace_entity)
                target = analysis.namespace_member(
                    scope, analysis.token_identifier(member.token()));
            if (target &&
                analysis.entity_table[target].kind == entity_kind::enumerator)
                return written_form(expr_primary_of_enumerator(target));
            if (target &&
                analysis.entity_table[target].kind == entity_kind::variable) {
                if (!expr_primary_external(target))
                    return written_form(false);
                return written_form(true);
            }
            /* ns::f as a callee: sr <source-name of ns> E <source-name> (a
               function, or a name an inline namespace of ns holds) */
            if (scope && !global && qual.size() == 2 &&
                analysis.entity_table[scope].kind ==
                    entity_kind::namespace_entity &&
                (!target ||
                 analysis.entity_table[target].kind != entity_kind::variable)) {
                enc.write(terminal::sr_unresolved_scope);
                if (!source_name(analysis.entity_table[scope].name))
                    return written_form(false);
                enc.write(terminal::E_end);
                return written_form(
                    source_name(analysis.token_identifier(member.token())));
            }
            return written_form(false);
        }
        if (member.is(syntax_kind::simple_template_id) && member.size() >= 3 &&
            member[0].is_token()) {
            if (global)
                enc.write(terminal::gs_global_scope);
            enc.write(terminal::sr_unresolved_scope);
            while (qi + 1 < qual.size()) {
                if (!qual[qi].is_identifier())
                    return written_form(false);
                if (!source_name(analysis.token_identifier(qual[qi].token())))
                    return written_form(false);
                qi += 2;
            }
            enc.write(terminal::E_end);
            if (!source_name(analysis.token_identifier(member[0].token())))
                return written_form(false);
            enc.write(terminal::I_template_args);
            owned_array<SyntaxElement> written;
            template_argument_elements(member, written);
            for (SyntaxElement a : written) {
                if (a.is(syntax_kind::type_id)) {
                    if (!type_operand_of_written(a))
                        return written_form(false);
                    continue;
                }
                while (
                    a.is_node() && a.size() == 1 &&
                    a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                    a = a[0];
                if (!a.is_token())
                    return written_form(false);
                if (written_value_binding(a)) {
                    if (!expression_of_written(a))
                        return written_form(false);
                    continue;
                }
                if (const auto *b = written_binding(a)) {
                    if (!type(b->type))
                        return written_form(false);
                    continue;
                }
                entity_index pe =
                    analysis.constraint_parameter(encoding_template, a.token());
                if (!pe)
                    return written_form(false);
                if (leveled_template) {
                    if (!template_param_of_constraint(leveled_template, pe))
                        return written_form(false);
                } else if (!type(analysis.entity_table[pe].type)) {
                    return written_form(false);
                }
            }
            enc.write(terminal::E_end);
            return written_form(true);
        }
        return written_form(false);
    }

    return form::none;
}

/* [abi:expressions] a call, a subscript and a member access */
itanium_mangler::form
itanium_mangler::postfix_of_written(SyntaxElement expression)
{
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() >= 3) {
        auto base = expression[0];
        auto op = expression[1];
        if (expression.size() <= 4 &&
            op.is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
            expression[expression.size() - 1].is(
                preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            enc.write(itanium_operator::cl_call);
            entity_index resolved = written_call(expression);
            uint32_t unresolved_name = 0;
            if (!resolved && base.is_token() && !written_object_binding(base) &&
                !analysis.function_parameter_position(encoding_template,
                                                      base.token()))
                unresolved_name = analysis.token_identifier(base.token());
            if (resolved) {
                if (!expr_primary_external(resolved))
                    return written_form(false);
            } else if (unresolved_name) {
                if (!source_name(unresolved_name))
                    return written_form(false);
            } else if (!expression_of_written(base))
                return written_form(false);
            if (expression.size() == 4 &&
                !expression_list_of_written(expression[2]))
                return written_form(false);
            enc.write(terminal::E_end);
            return written_form(true);
        }

        /* [abi:expressions] e1[e2]: ix <expression> <expression> */
        if (expression.size() == 4 &&
            op.is(preprocessing_op_or_punc::LEFT_BRACKET) &&
            expression[3].is(preprocessing_op_or_punc::RIGHT_BRACKET)) {
            enc.write(itanium_operator::ix_subscript);
            return written_form(expression_of_written(base) &&
                                expression_of_written(expression[2]));
        }

        bool dot = op.is(preprocessing_op_or_punc::DOT);
        /* e.template f<A>: dt <expression> <source-name> <template-args> */
        if (expression.size() == 4 &&
            expression[2].is(standard_keyword::template_) &&
            expression[3].is(syntax_kind::simple_template_id) &&
            (dot || op.is(preprocessing_op_or_punc::ARROW))) {
            auto member = expression[3];
            if (!member[0].is_identifier())
                return written_form(false);
            if (dot)
                enc.write(terminal::dt_member_access);
            else
                enc.write(itanium_operator::pt_arrow);
            bool dependent = false;
            return written_form(
                expression_of_written(base) &&
                source_name(analysis.token_identifier(member[0].token())) &&
                template_args_of_written(member, dependent));
        }
        /* e.operator->: dt <expression> on <operator-name>, an operator
           token that names one operator whatever its operands */
        if (expression.size() == 3 &&
            expression[2].is(syntax_kind::operator_function_id) &&
            expression[2].size() == 2 &&
            (dot || op.is(preprocessing_op_or_punc::ARROW))) {
            itanium_operator named = operator_written(expression[2][1], 2);
            if (named == itanium_operator::none ||
                operator_written(expression[2][1], 1) != itanium_operator::none)
                return written_form(false);
            if (dot)
                enc.write(terminal::dt_member_access);
            else
                enc.write(itanium_operator::pt_arrow);
            if (!expression_of_written(base))
                return written_form(false);
            enc.write(terminal::on_operator_function_id);
            enc.write(named);
            return written_form(true);
        }
        if (expression.size() == 3 && expression[2].is_token() &&
            (dot || op.is(preprocessing_op_or_punc::ARROW))) {
            if (dot)
                enc.write(terminal::dt_member_access);
            else
                enc.write(itanium_operator::pt_arrow);
            return written_form(
                expression_of_written(base) &&
                source_name(analysis.token_identifier(expression[2].token())));
        }
    }

    return form::none;
}

/* [abi:expressions] an <expression> that is one token: a function parameter, a template parameter, an object or enumerator named, a literal */
itanium_mangler::form
itanium_mangler::primary_of_written(SyntaxElement expression)
{
    if (!expression.is_token())
        return written_form(false);
    if (requirement_parameters && expression.is_identifier()) {
        uint32_t id = analysis.token_identifier(expression.token());
        for (uint32_t i = 0; i < requirement_parameters->size(); i++) {
            if ((*requirement_parameters)[i].name != id)
                continue;
            function_param(i + 1, (*requirement_parameters)[i].cv,
                           in_head_constraint);
            return written_form(true);
        }
    }
    uint32_t parameter = analysis.function_parameter_position(
        encoding_template, expression.token());
    if (parameter) {
        function_param(parameter);
        return written_form(true);
    }
    /* an instantiation's pointer or reference parameter: the argument,
       [abi:expressions] [ad] L _Z <encoding> E */
    if (const auto *b = written_object_binding(expression)) {
        if (record(b->type).kind != type_kind::lvalue_reference)
            enc.write(itanium_operator::ad_address_of);
        if (!expr_primary_external(b->specialization))
            return written_form(false);
        return written_form(true);
    }
    /* a name an alias template bound to a value-dependent argument */
    if (expression.is_identifier()) {
        uint32_t id = analysis.token_identifier(expression.token());
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.identifier == id && b.specialization &&
                analysis.entity_table[b.specialization].kind ==
                    entity_kind::dependent_value)
                return written_form(
                    expression_of_bound_value(b.specialization));
        }
    }
    /* an element of the class's index pack: its value */
    if (const auto *b = written_value_binding(expression))
        return written_form(expr_primary_of_enumerator(b->specialization));
    /* [expr.prim.id] a non-static data member named in a member's
       signature, (*this).name: dt de fpT <source-name> */
    if (expression.is_identifier()) {
        entity_index member = analysis.entity_table.find(
            analysis.entity_table[encoding_template].enclosing,
            analysis.token_identifier(expression.token()));
        if (member && analysis.entity_table[member].nonstatic_member &&
            analysis.entity_table[member].kind == entity_kind::variable) {
            enc.write(terminal::dt_member_access);
            enc.write(itanium_operator::de_dereference);
            enc.write(terminal::fpT_this_parameter);
            return written_form(
                source_name(analysis.token_identifier(expression.token())));
        }
    }
    /* a non-type template parameter: its <template-param>
       ([abi:mangle.template-param]) */
    if (expression.is_identifier())
        if (entity_index pe = analysis.constraint_parameter(encoding_template,
                                                            expression.token()))
            return written_form(
                template_param_of_constraint(encoding_template, pe));
    if (expression.is_identifier()) {
        entity_index named = analysis.constraint_name(
            analysis.entity_table[encoding_template].enclosing,
            expression.token());
        if (named &&
            analysis.entity_table[named].kind == entity_kind::enumerator)
            return written_form(expr_primary_of_enumerator(named));
    }

    /* [abi:expressions] true and false: L b 1 E, L b 0 E */
    if (expression.is(standard_keyword::true_) ||
        expression.is(standard_keyword::false_)) {
        expr_primary_value(fundamental_type::bool_, false,
                           expression.is(standard_keyword::true_));
        return written_form(true);
    }
    /* [abi:mangling.literal]: nullptr is its type alone */
    if (expression.is(standard_keyword::nullptr_)) {
        expr_primary_nullptr();
        return written_form(true);
    }
    return written_form(expr_primary_of_integer_literal(expression.token()));
}

} // namespace cplusplus
