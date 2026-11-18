/*
 * dcl.decl.cpp: [dcl.decl] Declarators.
 *
 *     init-declarator-list:
 *         init-declarator
 *         init-declarator-list , init-declarator
 *
 * [dcl.decl]/1: "A declarator declares a single variable, function, or
 * type, within a declaration."  The parse also records what the
 * declarator declares as far as the grammar needs it (declarator_facts):
 * its declarator-id, and the first derivation applied to it
 * ([dcl.meaning]/1).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [basic.scope.declarative]/1: the name the declarator declares, as what
 * the decl-specifier-seq and the declarator make it: a typedef-name, a
 * function, or another entity.  [dcl.typedef]/2: a typedef-name is a
 * synonym of its type; of a class's, the class's members are found
 * through it ([basic.lookup.qual]/1).
 */
void parser::declare_declarator(const decl_specifier_facts &specifiers,
                                const declarator_facts &facts)
{
    name_kind kind = name_kind::other;
    if (facts.first == derivation::function)
        kind = name_kind::function;
    scope_index region = 0;
    if (specifiers.typedef_) {
        kind = name_kind::type_name;
        if (facts.first == derivation::none)
            region = specifiers.type_region;
    }
    names.declare(facts.name, declared_kind(kind), region,
                  specifiers.typedef_ && specifiers.dependent);
}

/*
 *     init-declarator:
 *         declarator initializer_opt
 *         declarator requires-clause
 *
 * With first, the declarator is the last pending child already (the
 * caller parsed it to see whether the declaration is a function-definition).
 * [dcl.decl]/3: "The optional requires-clause in an init-declarator or
 * member-declarator shall be present only if the declarator declares a
 * templated function" is semantic: the grammar takes it after any.
 */
node_index parser::init_declarator(const decl_specifier_facts &specifiers,
                                   declarator_facts &facts, bool first)
{
    size_t mark;
    token_index first_token;
    if (first) {
        mark = pending.size() - 1;
        syntax_child c = pending.back();
        if (c.is_token())
            first_token = c.index();
        else
            first_token = tree[c.index()].first_token;
    } else {
        mark = begin_node();
        first_token = position;
        declarator(declarator_form::concrete, facts);
        if (failed())
            return 0;
        /* [basic.scope.pdecl]/1: the point of declaration is after the
           complete declarator, before the initializer */
        if (facts.name && !facts.qualified_id)
            declare_declarator(specifiers, facts);
    }
    /* init-declarator: declarator requires-clause ([dcl.decl]/1); that
       its declarator declares a templated function is [dcl.decl]/3's
       semantic rule */
    if (at(standard_keyword::requires_) && facts.first != derivation::function)
        return violates("a requires-clause only after a function's declarator",
                        "dcl.decl", position);
    if (at(standard_keyword::requires_)) {
        requires_clause();
        if (failed())
            return 0;
        return finish_node(syntax_kind::init_declarator, mark, first_token);
    }
    /* [class.static.data]/3: "The initializer expression in the definition
       of a static data member is in the scope of its class" */
    scope_index outer = names.current();
    if (facts.has_member_of)
        names.reenter(facts.member_of);
    node_index init = initializer();
    names.reenter(outer);
    if (!init && !failed())
        return 0; /* no initializer: the declarator passes through */
    if (failed())
        return 0;
    return finish_node(syntax_kind::init_declarator, mark, first_token);
}

/*
 *     declarator:
 *         ptr-declarator
 *         noptr-declarator parameters-and-qualifiers trailing-return-type
 *
 *     abstract-declarator:
 *         ptr-abstract-declarator
 *         noptr-abstract-declarator_opt parameters-and-qualifiers trailing-return-type
 *         abstract-pack-declarator
 *
 * The second alternatives are the first with a trailing-return-type after
 * the last parameters-and-qualifiers: the noptr part ends with them, which
 * the ptr-declarator parse finds first; the trailing-return-type is then
 * added around it.  abstract-pack-declarator is P7's.
 */
node_index parser::declarator(declarator_form form, declarator_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    ptr_declarator(form, facts);
    if (failed())
        return 0;
    if (!at(preprocessing_op_or_punc::ARROW)) {
        if (gnu_declarator_suffix_follows(facts)) /* [ext.gnu] */
            return gnu_declarator_suffix(mark, first, facts);
        return facts.node;
    }
    /* [dcl.fct]/2's second form: only after parameters-and-qualifiers that
       are the declarator's last part */
    const syntax_child last = pending.back();
    if (last.is_token() ||
        (tree[last.index()].kind != syntax_kind::noptr_declarator &&
         tree[last.index()].kind != syntax_kind::noptr_abstract_declarator &&
         tree[last.index()].kind != syntax_kind::parameters_and_qualifiers))
        return expected("parameters-and-qualifiers before '->'", "dcl.decl");
    /* [basic.scope.param]/1: the parameters' scope "ends at the end of
       that function declarator", after the trailing-return-type */
    if (facts.has_parameters)
        names.reenter(facts.parameters);
    trailing_return_type();
    if (facts.has_parameters)
        names.leave();
    if (failed())
        return 0;
    facts.trailing_return = true;
    syntax_kind kind = syntax_kind::declarator;
    if (!facts.has_id)
        kind = syntax_kind::abstract_declarator;
    facts.node = finish_node(kind, mark, first);
    if (gnu_declarator_suffix_follows(facts)) /* [ext.gnu] */
        return gnu_declarator_suffix(mark, first, facts);
    return facts.node;
}

/*
 *     ptr-declarator:
 *         noptr-declarator
 *         ptr-operator ptr-declarator
 *
 *     ptr-abstract-declarator:
 *         noptr-abstract-declarator
 *         ptr-operator ptr-abstract-declarator_opt
 *
 * [dcl.ptr]/1, [dcl.ref]/1, [dcl.mptr]/1: in T D where D is ptr-operator
 * D1, the ptr-operator applies to what D1 declares after D1's own parts:
 * it is the first derivation only when D1 has none.
 */
node_index parser::ptr_declarator(declarator_form form, declarator_facts &facts)
{
    if (!at_ptr_operator())
        return noptr_declarator(form, facts);
    size_t mark = begin_node();
    token_index first = position;
    derivation kind = derivation::none;
    ptr_operator(&kind);
    if (failed())
        return 0;
    /* [dcl.ref]/5: "There shall be no references to references": a
       reference ptr-operator whose ptr-declarator starts with another */
    if (kind == derivation::reference &&
        (at(preprocessing_op_or_punc::AMPERSAND) ||
         at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND)))
        return violates("no reference to a reference", "dcl.ref", position);
    bool inner = form != declarator_form::abstract || at_ptr_operator() ||
                 at(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
                 at(preprocessing_op_or_punc::LEFT_BRACKET);
    if (form == declarator_form::either &&
        !(at_identifier() || at(preprocessing_op_or_punc::COLON_COLON) ||
          at(preprocessing_op_or_punc::TILDE) || named_pack_follows() ||
          abstract_pack_follows() || at_ptr_operator() ||
          at(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
          at(preprocessing_op_or_punc::LEFT_BRACKET)))
        inner = false;
    if (inner) {
        ptr_declarator(form, facts);
        if (failed())
            return 0;
    }
    if (facts.first == derivation::none)
        facts.first = kind;
    syntax_kind node = syntax_kind::ptr_declarator;
    if (!facts.has_id)
        node = syntax_kind::ptr_abstract_declarator;
    facts.node = finish_node(node, mark, first);
    return facts.node;
}

/*
 *     noptr-declarator:
 *         declarator-id attribute-specifier-seq_opt
 *         noptr-declarator parameters-and-qualifiers
 *         noptr-declarator [ constant-expression_opt ] attribute-specifier-seq_opt
 *         ( ptr-declarator )
 *
 *     noptr-abstract-declarator:
 *         noptr-abstract-declarator_opt parameters-and-qualifiers
 *         noptr-abstract-declarator_opt [ constant-expression_opt ] attribute-specifier-seq_opt
 *         ( ptr-abstract-declarator )
 *
 * An attribute-specifier-seq follows only a declarator-id or an array's ']':
 * [[ anywhere else in a declarator is no attribute-specifier, and
 * [dcl.attr.grammar]/7 forbids it.
 */
/* ... before a declarator-id */
bool parser::named_pack_follows()
{
    return (at(preprocessing_op_or_punc::ELLIPSIS) &&
            (at_identifier(1) || at(preprocessing_op_or_punc::COLON_COLON, 1) ||
             at(standard_keyword::operator_, 1)));
}

/* ... with no declarator-id after it, not before ')' */
bool parser::abstract_pack_follows()
{
    return at(preprocessing_op_or_punc::ELLIPSIS) &&
           tokens.files.behavior.has(language_feature::variadic_templates) &&
           !named_pack_follows() &&
           !at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1) &&
           !at(preprocessing_op_or_punc::LEFT_BRACKET, 1);
}

node_index parser::noptr_declarator(declarator_form form,
                                    declarator_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    bool head = false;
    if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
        at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
        return misplaced_attribute_brackets();
    if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
        form == declarator_form::concrete)
        /* [ identifier-list ]: a structured binding ([dcl.struct.bind]): P5 */
        return beyond_this_step();
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        form == declarator_form::abstract && !parameters_follow(form) &&
        !(at_ptr_operator_at(1) ||
          at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) ||
          at(preprocessing_op_or_punc::LEFT_BRACKET, 1))) {
        /* ( ptr-abstract-declarator ) needs a ptr-abstract-declarator: a
           ( before anything else is not this declarator's (int(nullptr) in
           auto (*p)() -> int(nullptr); is an initializer) */
        return 0;
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        !(form != declarator_form::concrete && parameters_follow(form))) {
        /* ( ptr-declarator ) */
        add_token();
        ptr_declarator(form, facts);
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the declarator", "dcl.decl");
        head = true;
    } else if (form != declarator_form::abstract &&
               (at_identifier() || at(preprocessing_op_or_punc::COLON_COLON) ||
                at(preprocessing_op_or_punc::TILDE) || named_pack_follows() ||
                at(standard_keyword::operator_))) {
        declarator_id(facts);
        if (failed())
            return 0;
        head = true;
        /* declarator-id attribute-specifier-seq_opt */
        attribute_specifier_seq();
        if (failed())
            return 0;
    } else if (form != declarator_form::concrete && abstract_pack_follows()) {
        /* [dcl.fct]/1: noptr-abstract-pack-declarator: ... ([temp.variadic]
           /4: a parameter pack whose name is omitted).  A ... before ')'
           is the parameter-declaration-clause's: a pack or a C variadic
           function's ellipsis, which [dcl.fct]/? tells apart by whether the
           type names an unexpanded pack, a semantic question */
        add_token();
        facts.pack = true;
        head = true;
    } else if (form == declarator_form::concrete) {
        return expected("a declarator", "dcl.decl");
    }

    unsigned suffixes = 0;
    for (;;) {
        if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
            at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
            return misplaced_attribute_brackets();
        if (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
            add_token();
            if (!at(preprocessing_op_or_punc::RIGHT_BRACKET)) {
                constant_expression();
                if (failed())
                    return 0;
            }
            if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
                return expected("']' to close the array bound", "dcl.array");
            attribute_specifier_seq(true);
            if (failed())
                return 0;
            if (facts.first == derivation::none)
                facts.first = derivation::array;
        } else if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
                   (head || suffixes || form != declarator_form::concrete)) {
            if (form != declarator_form::concrete) {
                if (!parameters_follow(form))
                    break;
                parameters_and_qualifiers(&facts.parameters);
                if (failed())
                    return 0;
                facts.has_parameters = true;
            } else {
                /* [dcl.ambig.res]/1: "the resolution is to consider any
                   construct that could possibly be a declaration a
                   declaration": parameters, unless they do not parse; then
                   the ( is an initializer's */
                failure_state failed_with;
                bool parsed = false;
                {
                    tentative_parse attempt(*this);
                    /* a member's parameters are in its class's scope
                       ([basic.scope.class]/1), and so its body */
                    scope_index outer = names.current();
                    if (facts.has_member_of)
                        names.reenter(facts.member_of);
                    parameters_and_qualifiers(&facts.parameters);
                    names.reenter(outer);
                    if (!failed()) {
                        attempt.commit();
                        parsed = true;
                        facts.has_parameters = true;
                    } else {
                        failed_with = failure;
                    }
                }
                if (!parsed) {
                    if (failed_with.beyond_step)
                        return beyond_this_step();
                    /* ... but only when the parenthesis could hold an
                       expression-list: one that starts with a
                       decl-specifier other than a type a functional cast
                       could name (int(1), S{}) cannot */
                    if (!parenthesis_could_be_initializer()) {
                        failure = failed_with;
                        return 0;
                    }
                    break;
                }
            }
            if (facts.first == derivation::none)
                facts.first = derivation::function;
        } else {
            break;
        }
        suffixes++;
    }
    if (!head && !suffixes)
        return 0; /* noptr-abstract-declarator_opt: nothing */
    if (!suffixes && pending.size() == mark + 1) {
        /* declarator-id alone: it passes through */
        return 0;
    }
    syntax_kind node = syntax_kind::noptr_declarator;
    if (!facts.has_id)
        node = syntax_kind::noptr_abstract_declarator;
    facts.node = finish_node(node, mark, first);
    return facts.node;
}

/*
 * Whether ( at the current token could start an initializer's ( expression-list )
 * ([dcl.init]/1): not when what follows is a decl-specifier that no
 * expression starts with: a storage-class or function specifier, typedef,
 * a cv-qualifier, or a type keyword or type-name not followed by ( or {
 * ([expr.type.conv]: int(1), T{}).
 */
bool parser::parenthesis_could_be_initializer()
{
    /* ( ) is no initializer: the expression-list is not optional */
    if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1))
        return false;
    const token &t = peek(1);
    standard_keyword k = keyword_of(t);
    switch (k) {
    case standard_keyword::static_:
    case standard_keyword::extern_:
    case standard_keyword::thread_local_:
    case standard_keyword::mutable_:
    case standard_keyword::typedef_:
    case standard_keyword::inline_:
    case standard_keyword::constexpr_:
    case standard_keyword::consteval_:
    case standard_keyword::constinit_:
    case standard_keyword::const_:
    case standard_keyword::volatile_:
    case standard_keyword::register_:
    case standard_keyword::friend_:
    case standard_keyword::virtual_:
    case standard_keyword::explicit_:
        return false;
    case standard_keyword::char_:
    case standard_keyword::char8_t_:
    case standard_keyword::char16_t_:
    case standard_keyword::char32_t_:
    case standard_keyword::wchar_t_:
    case standard_keyword::bool_:
    case standard_keyword::short_:
    case standard_keyword::int_:
    case standard_keyword::long_:
    case standard_keyword::signed_:
    case standard_keyword::unsigned_:
    case standard_keyword::float_:
    case standard_keyword::double_:
    case standard_keyword::void_:
        return at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 2) ||
               at(preprocessing_op_or_punc::LEFT_BRACE, 2);
    default:
        break;
    }
    if (type_name_follows(1) && !nested_name_specifier_follows(1))
        return at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 2) ||
               at(preprocessing_op_or_punc::LEFT_BRACE, 2);
    return true;
}

/*
 *     ptr-operator:
 *         * attribute-specifier-seq_opt cv-qualifier-seq_opt
 *         & attribute-specifier-seq_opt
 *         && attribute-specifier-seq_opt
 *         nested-name-specifier * attribute-specifier-seq_opt cv-qualifier-seq_opt
 */
bool parser::at_ptr_operator_at(unsigned ahead)
{
    position += ahead;
    bool found = at_ptr_operator();
    position -= ahead;
    return found;
}

bool parser::at_ptr_operator()
{
    if (at(preprocessing_op_or_punc::STAR) ||
        at(preprocessing_op_or_punc::AMPERSAND) ||
        at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND))
        return true;
    /* nested-name-specifier * : the name's '::'s, then '*' */
    unsigned i = 0;
    if (at(preprocessing_op_or_punc::COLON_COLON)) {
        i++;
    } else if (at(standard_keyword::decltype_)) {
        /* decltype-specifier :: */
        i = group_end(1);
        if (!at(preprocessing_op_or_punc::COLON_COLON, i))
            return false;
        i++;
    }
    bool any = i > 0;
    while (at_identifier(i) &&
           at(preprocessing_op_or_punc::COLON_COLON, i + 1)) {
        i += 2;
        any = true;
    }
    return any && at(preprocessing_op_or_punc::STAR, i);
}

node_index parser::ptr_operator(derivation *kind)
{
    size_t mark = begin_node();
    token_index first = position;
    bool qualifiers = true;
    if (at(preprocessing_op_or_punc::STAR)) {
        *kind = derivation::pointer;
        add_token();
    } else if (at(preprocessing_op_or_punc::AMPERSAND) ||
               at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND)) {
        *kind = derivation::reference;
        add_token();
        qualifiers = false;
    } else {
        scope_index scope = 0;
        bool known = false;
        nested_name_specifier(&scope, &known);
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::STAR))
            return expected("'*' after the nested-name-specifier", "dcl.mptr");
        *kind = derivation::member_pointer;
    }
    attribute_specifier_seq(true);
    if (failed())
        return 0;
    if (qualifiers)
        cv_qualifier_seq();
    return finish_node(syntax_kind::ptr_operator, mark, first);
}

/*
 *     cv-qualifier-seq:
 *         cv-qualifier cv-qualifier-seq_opt
 *
 *     cv-qualifier:
 *         const
 *         volatile
 *
 * [ext:gnu_restrict] C99's restrict in C++ (GCC manual 7.3, Restricting
 * Pointer Aliasing):
 *
 *     cv-qualifier:
 *         __restrict
 *         __restrict__
 *
 * [ext:clang_nullability] (clang Language Extensions, Nullability):
 *
 *     cv-qualifier:
 *         _Nonnull
 *         _Nullable
 *         _Nullable_result
 *         _Null_unspecified
 */
node_index parser::cv_qualifier_seq()
{
    size_t mark = begin_node();
    token_index first = position;
    while (accept(standard_keyword::const_) ||
           accept(standard_keyword::volatile_) ||
           accept(extension_keyword::restrict) ||
           accept(extension_keyword::nullability))
        ;
    if (position == first)
        return 0;
    if (position == first + 1)
        return 0; /* one cv-qualifier passes through */
    return finish_node(syntax_kind::cv_qualifier_seq, mark, first);
}

/*
 *     trailing-return-type:
 *         -> type-id
 */
node_index parser::trailing_return_type()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    {
        /* [temp.res]/5: a type-id-only context */
        flag_set only(type_id_only, true);
        type_id();
    }
    if (failed())
        return 0;
    return finish_node(syntax_kind::trailing_return_type, mark, first);
}

} // namespace cplusplus
