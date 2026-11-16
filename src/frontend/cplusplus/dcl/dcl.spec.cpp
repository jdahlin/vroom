/*
 * dcl.spec.cpp: [dcl.spec] Specifiers, with [dcl.stc], [dcl.fct.spec],
 * [dcl.typedef], [dcl.constexpr], [dcl.constinit], [dcl.inline] (each a
 * keyword) and [dcl.type] with [dcl.type.simple], [dcl.type.elab],
 * [dcl.type.decltype], [dcl.spec.auto]: the type-specifiers.
 *
 *     decl-specifier:
 *         storage-class-specifier
 *         defining-type-specifier
 *         function-specifier
 *         friend
 *         typedef
 *         constexpr
 *         consteval
 *         constinit
 *         inline
 *
 *     decl-specifier-seq:
 *         decl-specifier attribute-specifier-seq_opt
 *         decl-specifier decl-specifier-seq
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/*
 * The name at tokens[ahead ...]: ::_opt (identifier ::)* identifier, looked
 * up as [basic.lookup.unqual] and [basic.lookup.qual] say.  *length is how
 * many tokens it has.
 */
name_class classify(parser &p, unsigned ahead, unsigned *length,
                    bool template_ids)
{
    unsigned i = ahead;
    scope_index scope = p.names.current();
    bool qualified = false;
    if (p.at(preprocessing_op_or_punc::COLON_COLON, i)) {
        scope = declared_names::global_namespace;
        qualified = true;
        i++;
    }
    uint32_t class_name = 0;
    for (;;) {
        if (!p.at_identifier(i)) {
            *length = i - ahead;
            if (p.at(preprocessing_op_or_punc::TILDE, i) && class_name)
                return name_class::constructor;
            if (qualified)
                return name_class::unknown;
            return name_class::none;
        }
        uint32_t name = p.tokens.identifier(p.position + i);
        if (class_name) {
            /* a member of a class: unknown to the parser until P5, except
               the injected-class-name naming the constructor */
            *length = i + 1 - ahead;
            if (name == class_name &&
                p.at(preprocessing_op_or_punc::LEFT_PARENTHESIS, i + 1))
                return name_class::constructor;
            return name_class::unknown;
        }
        scope_index found = 0;
        name_kind kind;
        if (qualified)
            kind = p.names.lookup_qualified(scope, name, &found);
        else
            kind = p.names.lookup_unqualified(name, &found);
        if (p.at(preprocessing_op_or_punc::COLON_COLON, i + 1)) {
            if (kind == name_kind::namespace_name) {
                scope = found;
            } else if (kind == name_kind::type_name) {
                class_name = name;
            } else {
                *length = i + 1 - ahead;
                return name_class::unknown;
            }
            qualified = true;
            i += 2;
            continue;
        }
        *length = i + 1 - ahead;
        if (template_ids && p.at(preprocessing_op_or_punc::LESS, i + 1))
            return name_class::unknown; /* a template-id: P7 */
        switch (kind) {
        case name_kind::type_name:
            return name_class::type;
        case name_kind::not_declared:
        case name_kind::template_name:
            return name_class::unknown;
        default:
            return name_class::not_a_type;
        }
    }
}

/* the decl-specifiers that are no type-specifiers: storage-class-specifier
   ([dcl.stc]), function-specifier ([dcl.fct.spec]) and the keywords of
   [dcl.spec]/1 */
bool is_non_type_decl_specifier(standard_keyword k)
{
    switch (k) {
    case standard_keyword::static_:
    case standard_keyword::thread_local_:
    case standard_keyword::extern_:
    case standard_keyword::mutable_:
    case standard_keyword::typedef_:
    case standard_keyword::constexpr_:
    case standard_keyword::consteval_:
    case standard_keyword::constinit_:
    case standard_keyword::inline_:
        return true;
    default:
        return false;
    }
}

/*
 * [dcl.type]/2: "As a general rule, at most one defining-type-specifier is
 * allowed in the complete decl-specifier-seq of a declaration or in a
 * defining-type-specifier-seq, and at most one type-specifier is allowed in
 * a type-specifier-seq. The only exceptions to this rule are the
 * following:
 *   - const can be combined with any type specifier except itself.
 *   - volatile can be combined with any type specifier except itself.
 *   - signed or unsigned can be combined with char, long, short, or int.
 *   - short or long can be combined with int.
 *   - long can be combined with double.
 *   - long can be combined with long."
 */
struct type_specifier_count {
    unsigned const_ = 0, volatile_ = 0, sign = 0, short_ = 0, long_ = 0;
    /* char, int, double, and every other defining-type-specifier */
    unsigned char_ = 0, int_ = 0, double_ = 0, other = 0;

    /* adds the specifier: false if the sequence now breaks the rule */
    bool add(standard_keyword k)
    {
        switch (k) {
        case standard_keyword::const_:
            return ++const_ == 1;
        case standard_keyword::volatile_:
            return ++volatile_ == 1;
        case standard_keyword::signed_:
        case standard_keyword::unsigned_:
            sign++;
            break;
        case standard_keyword::short_:
            short_++;
            break;
        case standard_keyword::long_:
            long_++;
            break;
        case standard_keyword::char_:
            char_++;
            break;
        case standard_keyword::int_:
            int_++;
            break;
        case standard_keyword::double_:
            double_++;
            break;
        default:
            other++;
            break;
        }
        unsigned base = char_ + int_ + double_ + other;
        if (base > 1 || sign > 1 || short_ > 1 || long_ > 2 ||
            (short_ && long_))
            return false;
        if (sign && (double_ || other))
            return false;
        if ((short_ || long_) && (char_ || other))
            return false;
        if (short_ && double_)
            return false;
        if (double_ && long_ > 1)
            return false;
        return true;
    }
};

} // namespace

VPRIV bool is_simple_type_keyword(standard_keyword k)
{
    switch (k) {
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
        return true;
    default:
        return false;
    }
}

name_class parser::classify_name(unsigned ahead, unsigned *length,
                                 bool template_ids)
{
    return classify(*this, ahead, length, template_ids);
}

bool parser::type_name_follows(unsigned ahead)
{
    unsigned length = 0;
    return classify_name(ahead, &length) == name_class::type;
}

/* whether a decl-specifier-seq starts at the current token (for
   [dcl.ambig.res]: whether a parameter-declaration-clause could start) */
bool parser::at_decl_specifier(bool unknown_names)
{
    standard_keyword k = keyword_of(peek());
    if (is_simple_type_keyword(k) || is_non_type_decl_specifier(k))
        return true;
    switch (k) {
    case standard_keyword::const_:
    case standard_keyword::volatile_:
    case standard_keyword::auto_:
    case standard_keyword::decltype_:
    case standard_keyword::class_:
    case standard_keyword::struct_:
    case standard_keyword::union_:
    case standard_keyword::enum_:
    case standard_keyword::typename_:
    case standard_keyword::extension:
        return true;
    default:
        break;
    }
    unsigned length = 0;
    name_class c = classify_name(0, &length);
    return c == name_class::type || (unknown_names && c == name_class::unknown);
}

/*
 * decl-specifier-seq, or with type_specifiers_only a
 * defining-type-specifier-seq ([dcl.type]/1):
 *
 *     defining-type-specifier-seq:
 *       defining-type-specifier attribute-specifier-seq_opt
 *       defining-type-specifier defining-type-specifier-seq
 *
 *     defining-type-specifier:
 *         type-specifier
 *         class-specifier
 *         enum-specifier
 *
 *     type-specifier:
 *       simple-type-specifier
 *       elaborated-type-specifier
 *       typename-specifier
 *       cv-qualifier
 *
 * [dcl.spec]/3: "If a type-name is encountered while parsing a
 * decl-specifier-seq, it is interpreted as part of the decl-specifier-seq
 * if and only if there is no previous defining-type-specifier other than a
 * cv-qualifier in the decl-specifier-seq."  Which combinations of
 * specifiers are allowed ([dcl.type]/2, [dcl.spec]/2) is a semantic rule,
 * not checked here.  Nothing (0, no failure) when there is no specifier.
 */
node_index parser::decl_specifier_seq(decl_specifier_facts &facts,
                                      bool type_specifiers_only)
{
    size_t mark = begin_node();
    token_index first = position;
    type_specifier_count count;
    token_index other_specifier = 0;
    bool has_other_specifier = false, has_auto = false;
    for (;;) {
        standard_keyword k = keyword_of(peek());
        token_index specifier = position;
        /* before C++11 auto is a storage-class-specifier
           ([diff.cpp03.dcl.dcl]/1), so a type-name may follow it */
        bool storage_class_auto =
            k == standard_keyword::auto_ &&
            !tokens.files.behavior.has(language_feature::auto_type_specifier);
        bool type_specifier =
            !is_non_type_decl_specifier(k) && !storage_class_auto;
        if (storage_class_auto) {
            /* where only type-specifiers can be, auto is C++11's
               placeholder: [ext.clang] clang's extension of C++03, P9 */
            if (type_specifiers_only || facts.type_specifier_seq)
                return beyond_this_step();
            add_token();
        } else if (is_non_type_decl_specifier(k)) {
            if (type_specifiers_only)
                break;
            if (k == standard_keyword::typedef_) {
                facts.typedef_ = true;
                facts.typedef_at = position;
            } else {
                other_specifier = position;
                has_other_specifier = true;
            }
            /* [dcl.typedef]/1: "The typedef specifier shall not be
               combined in a decl-specifier-seq with any other kind of
               specifier except a defining-type-specifier" */
            if (facts.typedef_ && has_other_specifier) {
                token_index at = position;
                if (k != standard_keyword::typedef_)
                    at = other_specifier;
                return violates("typedef combined only with "
                                "defining-type-specifiers",
                                "dcl.typedef", at);
            }
            add_token();
        } else if (k == standard_keyword::const_ ||
                   k == standard_keyword::volatile_) {
            add_token();
        } else if (is_simple_type_keyword(k)) {
            add_token();
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::auto_) {
            /* placeholder-type-specifier: type-constraint_opt auto; the
               type-constraint is P7's */
            add_token();
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::decltype_) {
            /* decltype-specifier :: is a nested-name-specifier
               ([expr.prim.id.qual]): of a declarator's ptr-operator, or of
               a type-name that is a class's member (P5) */
            if (at(preprocessing_op_or_punc::COLON_COLON, group_end(1))) {
                if (facts.has_defining_type_specifier)
                    break;
                return beyond_this_step();
            }
            decltype_specifier();
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::enum_) {
            enum_specifier_or_elaborated(facts);
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::class_ ||
                   k == standard_keyword::struct_ ||
                   k == standard_keyword::union_) {
            elaborated_or_class_specifier(facts);
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::typename_ ||
                   k == standard_keyword::friend_ ||
                   k == standard_keyword::virtual_ ||
                   k == standard_keyword::explicit_ ||
                   k == standard_keyword::extension) {
            /* typename-specifier (P7), friend, virtual and explicit
               (members: P5), an extension's keyword (P9) */
            return beyond_this_step();
        } else if (at_identifier() ||
                   at(preprocessing_op_or_punc::COLON_COLON)) {
            if (facts.has_defining_type_specifier)
                break; /* [dcl.spec]/3 */
            unsigned length = 0;
            name_class c = classify_name(0, &length);
            if (c == name_class::type) {
                simple_type_specifier_with_name();
                if (failed())
                    return 0;
                facts.has_defining_type_specifier = true;
            } else if (c == name_class::unknown && unknown_names_are_types &&
                       !at(preprocessing_op_or_punc::LESS, length) &&
                       !at(preprocessing_op_or_punc::COLON_COLON, length)) {
                /* the reading in which the name is a type-name */
                for (unsigned i = 0; i < length; i++)
                    add_token();
                facts.has_defining_type_specifier = true;
            } else if (c == name_class::unknown) {
                /* a name the parser cannot classify where a type could be:
                   T x; with T undeclared is ill-formed, but the parser
                   cannot tell it from a type a later step declares */
                if (!facts.has_defining_type_specifier &&
                    (at_identifier(length) ||
                     at(preprocessing_op_or_punc::STAR, length) ||
                     at(preprocessing_op_or_punc::AMPERSAND, length) ||
                     at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND,
                        length) ||
                     at(preprocessing_op_or_punc::LEFT_PARENTHESIS, length) ||
                     at(preprocessing_op_or_punc::LESS, length) ||
                     at(preprocessing_op_or_punc::COLON_COLON, length) ||
                     at(standard_keyword::const_, length) ||
                     at(standard_keyword::auto_, length) ||
                     at(standard_keyword::decltype_, length) ||
                     at_attribute_specifier(length) ||
                     at(preprocessing_op_or_punc::COMMA, length) ||
                     at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, length) ||
                     at(preprocessing_op_or_punc::SEMICOLON, length)))
                    return beyond_this_step();
                /* in an expression's type-id (a cast, new T[n]), what
                   follows is read by what the name is */
                if (facts.type_specifier_seq)
                    unknown_name = true;
                break;
            } else {
                break; /* a declarator-id, or a constructor's */
            }
        } else {
            break;
        }
        facts.count++;
        if (k == standard_keyword::auto_ && !storage_class_auto)
            has_auto = true;
        if (type_specifier && !count.add(k)) {
            /* auto int x: C++03's storage-class-specifier auto
               ([diff.cpp03.dcl.dcl]/1), in a later edition [ext.clang]
               clang's extension: P9 */
            if (has_auto)
                return beyond_this_step();
            return violates("at most one defining-type-specifier, except "
                            "the combinations [dcl.type]/2 lists",
                            "dcl.type", specifier);
        }
        /* decl-specifier attribute-specifier-seq_opt: the
           attribute-specifier-seq ends the sequence ([dcl.spec]/1: it
           "appertains to the type determined by the preceding
           decl-specifiers") */
        if (at_attribute_specifier()) {
            attribute_specifier_seq(true);
            if (failed())
                return 0;
            facts.trailing_attributes = true;
            break;
        }
    }
    if (position == first)
        return 0;
    /* [dcl.type.elab]/1: "An attribute-specifier-seq shall not appear in
       an elaborated-type-specifier unless the latter is the sole
       constituent of a declaration." */
    if (facts.elaborated_attributes &&
        !(facts.count == 1 && !facts.trailing_attributes &&
          !type_specifiers_only && at(preprocessing_op_or_punc::SEMICOLON)))
        return violates("an attribute-specifier-seq in an "
                        "elaborated-type-specifier only when it is the sole "
                        "constituent of a declaration",
                        "dcl.type.elab", facts.elaborated_attributes_at);
    syntax_kind kind = syntax_kind::decl_specifier_seq;
    if (type_specifiers_only)
        kind = syntax_kind::defining_type_specifier_seq;
    if (facts.type_specifier_seq)
        kind = syntax_kind::type_specifier_seq;
    facts.node = finish_node(kind, mark, first);
    return facts.node;
}

/*
 * [dcl.type.simple]:
 *
 *     simple-type-specifier:
 *         nested-name-specifier_opt type-name
 *         nested-name-specifier template simple-template-id
 *         decltype-specifier
 *         placeholder-type-specifier
 *         nested-name-specifier_opt template-name
 *         ...
 *
 *     type-name:
 *         class-name
 *         enum-name
 *         typedef-name
 *
 * The first alternative: a type-name alone passes through as its token.
 */
node_index parser::simple_type_specifier_with_name()
{
    size_t mark = begin_node();
    token_index first = position;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!at_identifier())
        return expected("a type-name", "dcl.type.simple");
    add_token();
    if (!qualifier)
        return 0;
    return finish_node(syntax_kind::simple_type_specifier, mark, first);
}

/*
 * [dcl.type.elab]:
 *
 *     elaborated-type-specifier:
 *         class-key attribute-specifier-seq_opt nested-name-specifier_opt identifier
 *         class-key simple-template-id
 *         class-key nested-name-specifier template_opt simple-template-id
 *         elaborated-enum-specifier
 *
 * and, when a '{' follows the head, [class.pre]'s
 *
 *     class-specifier:
 *         class-head { member-specification_opt }
 *
 *     class-head:
 *         class-key attribute-specifier-seq_opt class-head-name class-virt-specifier_opt base-clause_opt
 *         class-key attribute-specifier-seq_opt base-clause_opt
 *
 * The member-specification is P5's: the braces are kept as their tokens.
 * base-clause is P5's, templates P7's; elaborated-enum-specifier is
 * dcl.enum.cpp's.  [class.name]/2: the identifier becomes a class-name in
 * the current scope; an elaborated-type-specifier that names nothing
 * declared yet declares it ([basic.scope.pdecl]/7, [dcl.type.elab]/3).
 */
node_index parser::elaborated_or_class_specifier(decl_specifier_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* class-key */
    token_index attributes_at = position;
    node_index attributes = attribute_specifier_seq();
    if (failed())
        return 0;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = 0;
    if (nested_name_specifier_follows(0)) {
        qualifier = nested_name_specifier(&scope, &known);
        if (failed())
            return 0;
    }
    uint32_t name = 0;
    if (at_identifier()) {
        name = tokens.identifier(position);
        add_token();
        if (at(preprocessing_op_or_punc::LESS))
            return beyond_this_step(); /* simple-template-id: P7 */
    } else if (qualifier) {
        return expected("an identifier after the nested-name-specifier",
                        "dcl.type.elab");
    }
    if (at_identifier() && at(preprocessing_op_or_punc::LEFT_BRACE, 1)) {
        /* class-virt-specifier: final ([class.pre]); an identifier with a
           special meaning ([lex.name]/2 Table 4), not checked here */
        add_token();
    } else if (at(preprocessing_op_or_punc::COLON)) {
        return beyond_this_step(); /* base-clause: P5 */
    }
    /* [dcl.type]/1: a type-specifier-seq (a type-id's) has no
       class-specifier: a '{' after its elaborated-type-specifier is the
       next production's */
    bool body =
        at(preprocessing_op_or_punc::LEFT_BRACE) && !facts.type_specifier_seq;
    if (body) {
        unparsed_group();
        if (failed())
            return 0;
    } else if (!name) {
        return expected("an identifier or '{'", "dcl.type.elab");
    }
    facts.declares_type = true;
    if (!body) {
        facts.elaborated = true;
        facts.elaborated_at = first;
        facts.elaborated_declaring_form = !qualifier;
        if (attributes) {
            facts.elaborated_attributes = true;
            facts.elaborated_attributes_at = attributes_at;
        }
    }
    if (name && !qualifier) {
        bool declared = false;
        if (!body)
            declared = names.lookup_unqualified(name) == name_kind::type_name;
        if (body || !declared)
            names.declare(name, name_kind::type_name);
    }
    syntax_kind kind = syntax_kind::elaborated_type_specifier;
    if (body)
        kind = syntax_kind::class_specifier;
    return finish_node(kind, mark, first);
}

/*
 * [dcl.type.decltype]:
 *
 *     decltype-specifier:
 *       decltype ( expression )
 *
 * [dcl.spec.auto]:
 *
 *     placeholder-type-specifier:
 *       type-constraint_opt auto
 *       type-constraint_opt decltype ( auto )
 *
 * [dcl.type.decltype]/1: "For an expression E, the type denoted by
 * decltype(E) is defined as follows": whether E is an unparenthesized
 * id-expression or class member access is what the tree says (a
 * parenthesized one is a primary-expression node).
 */
node_index parser::decltype_specifier()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after decltype", "dcl.type.decltype");
    syntax_kind kind = syntax_kind::decltype_specifier;
    if (at(standard_keyword::auto_, 1) &&
        at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 2)) {
        kind = syntax_kind::placeholder_type_specifier;
        add_token();
        add_token();
        add_token();
    } else {
        add_token();
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the operand of decltype",
                            "dcl.type.decltype");
    }
    return finish_node(kind, mark, first);
}

} // namespace cplusplus
