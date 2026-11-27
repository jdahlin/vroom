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
itanium_mangler::written_object(SyntaxElement name) const
{
    if (!name.is_token() || !name.is_identifier())
        return nullptr;
    uint32_t id = analysis.token_identifier(name.token());
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (b.identifier == id && b.specialization)
            return &b;
    }
    return nullptr;
}

/* the type an alias template's instantiation bound the identifier name to,
   for the dependent_written type being mangled (the pack's element being
   mangled for a name bound to a pack) */
const semantic_analysis::written_binding *
itanium_mangler::bound(SyntaxElement name) const
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
bool itanium_mangler::operand_type(SyntaxElement a, qualified_type &t,
                                   fundamental_type &keyword_type)
{
    t = qualified_type();
    keyword_type = fundamental_type::none;
    SyntaxElement inner = a;
    while (inner.is_node() && inner.size() == 1 &&
           inner.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
        inner = inner[0];
    if (inner.is_token()) {
        if (const auto *b = bound(inner)) {
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
bool itanium_mangler::type_operand(SyntaxElement a)
{
    qualified_type t;
    fundamental_type keyword_type = fundamental_type::none;
    if (!operand_type(a, t, keyword_type))
        return false;
    if (keyword_type != fundamental_type::none)
        return builtin_type(keyword_type);
    if (analysis.type_depends_on_template_parameter(t))
        operand_dependent = true;
    return type(t);
}

/* [abi:expressions] of a unary fold over a pack bound by an alias
   template: expanded, as clang does, for the elements: ( pattern op ... )
   is op e0 (op e1 (... en)), ( ... op pattern ) is ((e0 op e1) op ...) */
bool itanium_mangler::dependent_fold(SyntaxElement fold)
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
                text(code);
            pack_element = k;
            ok = dependent_expression(pattern);
        }
    } else {
        for (uint32_t k = 1; k < count; k++)
            text(code);
        for (uint32_t k = 0; k < count && ok; k++) {
            pack_element = k;
            ok = dependent_expression(pattern);
        }
    }
    pack_element = UINT32_MAX;
    return ok;
}

/* the types of an expanded pattern (collect_written_expansion), one after
   another; expanded false when none were recorded */
bool itanium_mangler::written_expansion(SyntaxElement a, bool &expanded)
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
   I, each argument a type (type_operand), a literal or X <expression> E,
   E; dependent whether one depends on a template parameter */
bool itanium_mangler::written_template_args(SyntaxElement id, bool &dependent)
{
    bool saved_dependent = operand_dependent;
    operand_dependent = false;
    bool value_argument = false;
    text("I");
    owned_array<SyntaxElement> written;
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        if (id[i].is(syntax_kind::template_argument_list))
            for (auto x : id[i].elements())
                written.append(x);
        else
            written.append(id[i]);
    }
    for (size_t k = 0; k < written.size(); k++) {
        SyntaxElement a = written[k];
        if (a.is(preprocessing_op_or_punc::COMMA))
            continue;
        if (a.is(syntax_kind::type_id)) {
            bool expanded = false;
            if (!written_expansion(a, expanded))
                return false;
            if (expanded)
                continue;
            if (!type_operand(a))
                return false;
            continue;
        }
        bool literal = a.is_token() && !a.is_identifier();
        if (!literal) {
            text("X");
            value_argument = true;
        }
        if (!dependent_expression(a))
            return false;
        if (!literal)
            text("E");
    }
    text("E");
    dependent = operand_dependent || value_argument;
    operand_dependent = saved_dependent || dependent;
    return true;
}

/* the qualifier Q of Q::name written in a dependent expression, a name (a
   template parameter, one an alias template bound) or a template-id
   (collect_written_casts gave its type): [abi:expressions]' <unresolved-type>
   for a template parameter (1), or the <unresolved-qualifier-level> of a
   class template's dependent specialization, its <source-name> and
   <template-args>, which E closes (2); 0 not mangled */
int itanium_mangler::unresolved_qualifier(SyntaxElement q)
{
    qualified_type t;
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
            return 0;
    } else if (!q.is_token() || !q.is_identifier() ||
               !operand_type(q, t, keyword_type)) {
        return 0;
    }
    const struct type &r = record(t);
    /* (an alias template's template-id naming a parameter is mangled as
       written, below, as clang does) */
    if (r.kind == type_kind::template_parameter && !t.cv() &&
        !q.is(syntax_kind::simple_template_id)) {
        if (!type(t))
            return 0;
        return 1;
    }
    if (r.kind == type_kind::dependent && !t.cv() &&
        analysis.entity_table[r.entity].kind ==
            entity_kind::template_specialization &&
        analysis.entity_table[r.entity].instantiation) {
        if (!source_name(analysis.entity_table[r.entity].name) ||
            !template_args(r.entity, 0, false)) {
            return 0;
        }
        return 2;
    }
    /* [impl.clang] an alias template's dependent specialization
       (libc++'s _CheckOptionalArgsCtor<_Up>::): its name and arguments as
       written */
    if (q.is(syntax_kind::simple_template_id) && q.size() >= 3 &&
        q[0].is_identifier()) {
        bool dependent = false;
        if (source_name(analysis.token_identifier(q[0].token())) &&
            written_template_args(q, dependent))
            return 2;
    }
    return 0;
}

/* [abi:expressions] <expression> ::= sp <expression>, a pack expansion */
bool itanium_mangler::expression_list(SyntaxElement list)
{
    owned_array<SyntaxElement> items;
    for (auto item : list.items(syntax_kind::initializer_list))
        items.append(item);
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].is(preprocessing_op_or_punc::ELLIPSIS))
            continue;
        if (i + 1 < items.size() &&
            items[i + 1].is(preprocessing_op_or_punc::ELLIPSIS))
            text("sp");
        if (!dependent_expression(items[i]))
            return false;
    }
    return true;
}

bool itanium_mangler::dependent_expression(SyntaxElement expression)
{
    if (dependent_expression_of(expression))
        return true;
    refuse_expression(expression);
    return false;
}

bool itanium_mangler::dependent_expression_of(SyntaxElement expression)
{
    /* [abi:expressions]: ( type-id ) cast-expression, cv <type>
       <expression>, the type analysed with the alias's names bound */
    if (expression.is(syntax_kind::cast_expression) && expression.size() == 4) {
        SyntaxRef r = expression;
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.cast != r.index().index())
                continue;
            text("cv");
            return type(b.type) && dependent_expression(expression[3]);
        }
        return false;
    }
    if (expression.is(syntax_kind::fold_expression))
        return dependent_fold(expression);
    /* [abi:expressions] a clang type trait of types, as clang mangles it:
       a vendor extended expression, u <source-name> <template-arg>* E
       (__is_same(T, U): u9__is_same <T> <U> E) */
    if (expression.is(syntax_kind::clang_type_trait_expression) &&
        expression.size() >= 4 && expression[0].is_token()) {
        owned_buffer name;
        analysis.token_spelling(expression[0].token(), name.get());
        text("u");
        number(name.size());
        vbuffer_append(out, name.data(), name.size());
        for (uint32_t i = 2; i + 1 < expression.size(); i++) {
            SyntaxElement a = expression[i];
            if (a.is(preprocessing_op_or_punc::COMMA))
                continue;
            if (!a.is(syntax_kind::type_id))
                return false;
            while (a.is_node() && a.size() == 1 &&
                   a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                a = a[0];
            const auto *b = bound(a);
            if (!b)
                return false;
            if (!type(b->type))
                return false;
        }
        text("E");
        return true;
    }
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
                text("sZ");
                template_param(b.pack_position - 1);
                return true;
            }
        }
        entity_index pe = analysis.constraint_parameter(encoding_template,
                                                        expression[3].token());
        if (!pe)
            return false;
        text("sZ");
        template_param(analysis.entity_table[pe].value);
        return true;
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
                return false;
            operand = expression[2];
            of_type = operand.is(syntax_kind::type_id);
        }
        if (vendor) {
            text("u11__alignof__");
            if (of_type) {
                if (!type_operand(operand))
                    return false;
            } else {
                text("X");
                if (!dependent_expression(operand))
                    return false;
                text("E");
            }
            text("E");
            return true;
        }
        if (of_type) {
            if (size)
                text("st");
            else
                text("at");
            return type_operand(operand);
        }
        if (size)
            text("sz");
        else
            text("az");
        return dependent_expression(operand);
    }
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
            text("L_Z");
            if (!encoding(b.specialization))
                return false;
            text("E");
            return true;
        }
        /* (a function template's name; an alias or class template's
           names a type, a functional cast: not mangled yet) */
        entity_index named = analysis.constraint_name(
            analysis.entity_table[encoding_template].enclosing,
            expression[0].token());
        if (!named || (!analysis.names_function_template(named) &&
                       !analysis.names_member_template(named) &&
                       !analysis.names_variable_template(named)))
            return false;
        if (!source_name(analysis.token_identifier(expression[0].token())))
            return false;
        /* (with no argument that depends on a template parameter the
           specialization is known, and clang names it, L _Z <encoding> E:
           not mangled yet) */
        bool dependent = false;
        if (!written_template_args(expression, dependent))
            return false;
        if (!dependent)
            return refuse("a call of a specialization its arguments "
                          "resolve (clang's L _Z <encoding> E)");
        return true;
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
        static const struct {
            standard_keyword keyword;
            const char *code;
        } casts[] = {
            {standard_keyword::dynamic_cast_, "dc"},
            {standard_keyword::static_cast_, "sc"},
            {standard_keyword::const_cast_, "cc"},
            {standard_keyword::reinterpret_cast_, "rc"},
        };
        for (const auto &k : casts)
            if (expression[0].is(k.keyword)) {
                text(k.code);
                return type_operand(expression[2]) &&
                       dependent_expression(expression[5]);
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
         expression[0].is(syntax_kind::simple_type_specifier)) &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[3].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        size_t before = out->n;
        text("cv");
        if (type_operand(expression[0])) {
            owned_array<SyntaxElement> arguments;
            for (auto argument :
                 expression[2].items(syntax_kind::initializer_list))
                arguments.append(argument);
            if (arguments.size() != 1)
                text("_");
            if (!expression_list(expression[2]))
                return false;
            if (arguments.size() != 1)
                text("E");
            return true;
        }
        out->n = before;
    }
    /* [abi:expressions] T{}, T a class the analysis recorded:

           <expression> ::= tl <type> <braced-expression>* E
     */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 2 && expression[0].is_token() &&
        expression[0].is_identifier() &&
        expression[1].is(syntax_kind::braced_init_list) &&
        expression[1].size() == 2) {
        size_t before = out->n;
        text("tl");
        if (type_operand(expression)) {
            text("E");
            return true;
        }
        out->n = before;
    }
    /* [abi:expressions] <unary operator-name> <expression> */
    if (expression.is(syntax_kind::unary_expression) &&
        expression.size() == 2) {
        itanium_operator o =
            prefix_operator(operator_written(expression[0], 1));
        if (o != itanium_operator::none) {
            text(o);
            return dependent_expression(expression[1]);
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
        size_t before = out->n;
        text("cv");
        if (type_operand(expression[0])) {
            text("_E");
            return true;
        }
        out->n = before;
    }
    /* [abi:expressions] c ? a : b is qu <expression> <expression>
       <expression> */
    if (expression.is(syntax_kind::conditional_expression) &&
        expression.size() == 5) {
        text(itanium_operator::conditional);
        return dependent_expression(expression[0]) &&
               dependent_expression(expression[2]) &&
               dependent_expression(expression[4]);
    }
    if (expression.is(syntax_kind::expression) && expression.size() == 3 &&
        punctuator(expression[1], preprocessing_op_or_punc::COMMA)) {
        text(itanium_operator::comma);
        return dependent_expression(expression[0]) &&
               dependent_expression(expression[2]);
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
            return false;
        text(op);
        return dependent_expression(expression[0]) &&
               dependent_expression(expression[2]);
    }
    if (expression.is(syntax_kind::primary_expression) &&
        expression.size() == 3 &&
        expression[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return dependent_expression(expression[1]);

    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 3 && expression[0].is_token() &&
        expression[0].keyword() == standard_keyword::void_ &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[2].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        text("cvv_E");
        return true;
    }

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
            size_t before = out->n;
            size_t candidates_before = candidates.size();
            text("sr");
            bool named = true;
            for (uint32_t i = 0; i < last; i += 2)
                named = named &&
                        source_name(analysis.token_identifier(qual[i].token()));
            int form = 0;
            if (named)
                form = unresolved_qualifier(qual[last]);
            if (form == 1 && last)
                form = 0;
            if (form) {
                if (form == 2)
                    text("E");
                if (member.is_identifier())
                    return source_name(
                        analysis.token_identifier(member.token()));
                bool dependent = false;
                return source_name(
                           analysis.token_identifier(member[0].token())) &&
                       written_template_args(member, dependent);
            }
            out->n = before;
            candidates.resize(candidates_before);
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
                text("sr");
                return type(analysis.entity_table[pe].type) &&
                       source_name(analysis.token_identifier(member.token()));
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
                text("gs");
            text("sr");
            for (; qi < last; qi += 2) {
                if (!qual[qi].is_identifier() ||
                    !source_name(analysis.token_identifier(qual[qi].token())))
                    return false;
            }
            bool dependent = false;
            if (!source_name(
                    analysis.token_identifier(qual[last][0].token())) ||
                !written_template_args(qual[last], dependent))
                return false;
            /* (with no dependent argument the member is known, and clang
               names it, L _Z <encoding> E: not mangled yet) */
            if (!dependent)
                return refuse("a member of a specialization its arguments "
                              "resolve (clang's L _Z <encoding> E)");
            text("E");
            return source_name(analysis.token_identifier(member.token()));
        }
        if (member.is_token() && member.is_identifier()) {
            entity_index scope = 0;
            bool first_lookup = !global;
            while (qi + 1 < qual.size()) {
                if (!qual[qi].is_identifier())
                    return false;
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
                    return false;
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
                return enumerator_literal(target);
            if (target &&
                analysis.entity_table[target].kind == entity_kind::variable) {
                text("L_Z");
                if (!encoding(target))
                    return false;
                text("E");
                return true;
            }
            /* ns::f as a callee: sr <source-name of ns> E <source-name> (a
               function, or a name an inline namespace of ns holds) */
            if (scope && !global && qual.size() == 2 &&
                analysis.entity_table[scope].kind ==
                    entity_kind::namespace_entity &&
                (!target ||
                 analysis.entity_table[target].kind != entity_kind::variable)) {
                text("sr");
                if (!source_name(analysis.entity_table[scope].name))
                    return false;
                text("E");
                return source_name(analysis.token_identifier(member.token()));
            }
            return false;
        }
        if (member.is(syntax_kind::simple_template_id) && member.size() >= 3 &&
            member[0].is_token()) {
            if (global)
                text("gs");
            text("sr");
            while (qi + 1 < qual.size()) {
                if (!qual[qi].is_identifier())
                    return false;
                if (!source_name(analysis.token_identifier(qual[qi].token())))
                    return false;
                qi += 2;
            }
            text("E");
            if (!source_name(analysis.token_identifier(member[0].token())))
                return false;
            text("I");
            owned_array<SyntaxElement> written;
            for (uint32_t i = 2; i + 1 < member.size(); i++) {
                if (member[i].is(syntax_kind::template_argument_list))
                    for (auto x : member[i].elements())
                        written.append(x);
                else
                    written.append(member[i]);
            }
            for (SyntaxElement a : written) {
                if (a.is(preprocessing_op_or_punc::COMMA))
                    continue;
                if (a.is(syntax_kind::type_id)) {
                    if (!type_operand(a))
                        return false;
                    continue;
                }
                while (
                    a.is_node() && a.size() == 1 &&
                    a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                    a = a[0];
                if (!a.is_token())
                    return false;
                if (const auto *b = bound(a)) {
                    if (!type(b->type))
                        return false;
                    continue;
                }
                entity_index pe =
                    analysis.constraint_parameter(encoding_template, a.token());
                if (!pe || !type(analysis.entity_table[pe].type))
                    return false;
            }
            text("E");
            return true;
        }
        return false;
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
            text("tl");
            if (!type(b.type))
                return false;
            if (expression[1].size() > 2 && !expression_list(expression[1][1]))
                return false;
            text("E");
            return true;
        }
    }
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() >= 3) {
        auto base = expression[0];
        auto op = expression[1];
        if (expression.size() <= 4 &&
            op.is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
            expression[expression.size() - 1].is(
                preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            text(itanium_operator::call);
            uint32_t unresolved_name = 0;
            if (base.is_token() && !written_object(base) &&
                !analysis.function_parameter_position(encoding_template,
                                                      base.token()))
                unresolved_name = analysis.token_identifier(base.token());
            if (unresolved_name) {
                if (!source_name(unresolved_name))
                    return false;
            } else if (!dependent_expression(base))
                return false;
            if (expression.size() == 4 && !expression_list(expression[2]))
                return false;
            text("E");
            return true;
        }

        /* [abi:expressions] e1[e2]: ix <expression> <expression> */
        if (expression.size() == 4 &&
            op.is(preprocessing_op_or_punc::LEFT_BRACKET) &&
            expression[3].is(preprocessing_op_or_punc::RIGHT_BRACKET)) {
            text(itanium_operator::subscript);
            return dependent_expression(base) &&
                   dependent_expression(expression[2]);
        }

        bool dot = op.is(preprocessing_op_or_punc::DOT);
        /* e.template f<A>: dt <expression> <source-name> <template-args> */
        if (expression.size() == 4 &&
            expression[2].is(standard_keyword::template_) &&
            expression[3].is(syntax_kind::simple_template_id) &&
            (dot || op.is(preprocessing_op_or_punc::ARROW))) {
            auto member = expression[3];
            if (!member[0].is_identifier())
                return false;
            if (dot)
                text("dt");
            else
                text(itanium_operator::arrow);
            bool dependent = false;
            return dependent_expression(base) &&
                   source_name(analysis.token_identifier(member[0].token())) &&
                   written_template_args(member, dependent);
        }
        if (expression.size() == 3 && expression[2].is_token() &&
            (dot || op.is(preprocessing_op_or_punc::ARROW))) {
            if (dot)
                text("dt");
            else
                text(itanium_operator::arrow);
            return dependent_expression(base) &&
                   source_name(
                       analysis.token_identifier(expression[2].token()));
        }
    }

    if (!expression.is_token())
        return false;
    uint32_t parameter = analysis.function_parameter_position(
        encoding_template, expression.token());
    if (parameter) {
        text("fp");
        if (parameter > 1)
            number(parameter - 2);
        text("_");
        return true;
    }
    /* an instantiation's pointer or reference parameter: the argument,
       [abi:expressions] [ad] L _Z <encoding> E */
    if (const auto *b = written_object(expression)) {
        if (record(b->type).kind != type_kind::lvalue_reference)
            text(itanium_operator::address_of);
        text("L_Z");
        if (!encoding(b->specialization))
            return false;
        text("E");
        return true;
    }
    /* a non-type template parameter: its <template-param>
       ([abi:mangle.template-param]) */
    if (expression.is_identifier())
        if (entity_index pe = analysis.constraint_parameter(encoding_template,
                                                            expression.token()))
            return constraint_parameter(encoding_template, pe);
    if (expression.is_identifier()) {
        entity_index named = analysis.constraint_name(
            analysis.entity_table[encoding_template].enclosing,
            expression.token());
        if (named &&
            analysis.entity_table[named].kind == entity_kind::enumerator)
            return enumerator_literal(named);
    }

    /* [abi:expressions] true and false: L b 1 E, L b 0 E */
    if (expression.is(standard_keyword::true_) ||
        expression.is(standard_keyword::false_)) {
        text("L");
        builtin_type(fundamental_type::bool_);
        number(expression.is(standard_keyword::true_));
        text("E");
        return true;
    }
    /* [abi:mangling.literal]: nullptr is its type alone */
    if (expression.is(standard_keyword::nullptr_)) {
        text("L");
        builtin_type(fundamental_type::std_nullptr_t);
        text("E");
        return true;
    }
    return integer_literal(expression.token());
}

} // namespace cplusplus
