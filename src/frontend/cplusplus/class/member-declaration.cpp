/*
 * member-declaration.cpp: [class.mem] Class members, the member-specification
 * of a class-specifier ([class.pre]: class-head { member-specification_opt }).
 *
 *     member-specification:
 *         member-declaration member-specification_opt
 *         access-specifier : member-specification_opt
 *
 *     member-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq_opt member-declarator-list_opt ;
 *         function-definition
 *         using-declaration
 *         using-enum-declaration
 *         static_assert-declaration
 *         template-declaration
 *         explicit-specialization
 *         deduction-guide
 *         alias-declaration
 *         opaque-enum-declaration
 *         empty-declaration
 *
 *     member-declarator-list:
 *         member-declarator
 *         member-declarator-list , member-declarator
 *
 * [class.mem]/6: "A complete-class context of a class is a function body,
 * default argument, noexcept-specifier, or default member initializer
 * within the member-specification of the class. [Note: A complete-class
 * context of a nested class is also a complete-class context of any
 * enclosing class, if the nested class is defined within the
 * member-specification of the enclosing class. — end note]"  /7: "the
 * class is regarded as complete within its complete-class contexts".  So a
 * member function's body may name a member declared after it, and what the
 * name is (a type or not) decides how the body parses.  The
 * member-specification is read twice: the first time only for the names
 * its member-declarations declare, the complete-class contexts skipped as
 * their tokens and the pass then undone; the second time in full, with
 * those names found in the complete-class contexts (declared_names'
 * complete region).  A nested class's body is skipped in its enclosing
 * class's first pass, so each member-specification is read twice, not once
 * per enclosing class.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* the token at tokens[ahead] is the integer-literal 0, spelled so: the
   pure-specifier's '0' is that token, not any literal whose value is 0 */
bool spelled_zero(parser &p, unsigned ahead)
{
    if (p.peek(ahead).literal != literal_kind::integer_literal)
        return false;
    owned_buffer spelling;
    p.tokens.spelling(p.position + ahead, spelling.get());
    return spelling.size() == 1 && spelling.data()[0] == '0';
}

bool is_access_specifier(standard_keyword k)
{
    return k == standard_keyword::private_ ||
           k == standard_keyword::protected_ || k == standard_keyword::public_;
}

} // namespace

/*
 * [lex.name]/2: "The identifiers in Table 4 have a special meaning when
 * appearing in a certain context. When referred to in the grammar, these
 * identifiers are used explicitly rather than using the identifier grammar
 * production."  Table 4: final, import, module, override.
 */
bool parser::at_word(const char *word, unsigned ahead)
{
    if (!at_identifier(ahead))
        return false;
    owned_buffer spelling;
    tokens.spelling(position + ahead, spelling.get());
    size_t n = strlen(word);
    return spelling.size() == n && !memcmp(spelling.data(), word, n);
}

/*
 *     virt-specifier:
 *         override
 *         final
 */
bool parser::at_virt_specifier(unsigned ahead)
{
    return at_word("override", ahead) || at_word("final", ahead);
}

/*
 *     virt-specifier-seq:
 *         virt-specifier
 *         virt-specifier-seq virt-specifier
 *
 * One virt-specifier passes through as its token.  N3272 (C++11); before
 * it they are [ext.clang] clang's extension, P9's.
 */
node_index parser::virt_specifier_seq()
{
    if (!at_virt_specifier())
        return 0;
    if (!tokens.files.behavior.has(language_feature::override_control))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    /* [class.mem]/14: "A virt-specifier-seq shall contain at most one of
       each virt-specifier." */
    bool seen_override = false, seen_final = false;
    while (at_virt_specifier()) {
        bool *seen = &seen_final;
        if (at_word("override"))
            seen = &seen_override;
        if (*seen)
            return violates("each virt-specifier at most once", "class.mem",
                            position);
        *seen = true;
        add_token();
    }
    if (position == first + 1)
        return 0;
    return finish_node(syntax_kind::virt_specifier_seq, mark, first);
}

/*
 * [class.ctor]/1: a constructor is declared by "a function declarator
 * ([dcl.fct]) of the form ptr-declarator ( parameter-declaration-clause )
 * noexcept-specifier_opt attribute-specifier-seq_opt where the
 * ptr-declarator consists solely of an id-expression, an optional
 * attribute-specifier-seq, and optional surrounding parentheses, and the
 * id-expression has one of the following forms: ... in a
 * member-declaration that belongs to the member-specification of a class
 * or class template but is not a friend declaration, the id-expression is
 * the injected-class-name of the immediately-enclosing entity".  So the
 * class's name before ( is the declarator-id when what the parentheses
 * hold can be a parameter-declaration-clause; S (*p) and S (x), with x
 * declared and no type, declare a member of type S.
 */
bool parser::constructor_parameters_follow(unsigned ahead)
{
    /* noptr-declarator: declarator-id attribute-specifier-seq_opt
       ([dcl.decl]/4): the attributes appertain to the constructor */
    while (at_attribute_specifier(ahead)) {
        unsigned after = 0;
        if (at(preprocessing_op_or_punc::LEFT_BRACKET, ahead))
            after = group_end(ahead);
        else
            after = group_end(ahead + 1); /* alignas ( ... ) */
        if (after <= ahead + 1)
            return false;
        ahead = after;
    }
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS, ahead))
        return false;
    if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, ahead + 1) ||
        at(preprocessing_op_or_punc::ELLIPSIS, ahead + 1) ||
        at_attribute_specifier(ahead + 1))
        return true;
    position += ahead + 1;
    bool specifier = at_decl_specifier(true);
    position -= ahead + 1;
    return specifier;
}

/*
 * class-head { member-specification_opt }, from the '{'.
 * [basic.scope.pdecl]/8: "The point of declaration for an
 * injected-class-name ([class.pre]) is immediately following the opening
 * brace of the class definition."  [class.pre]/2: "The class-name is also
 * bound in the scope of the class (template) itself; this is known as the
 * injected-class-name."
 */
node_index parser::class_body(uint32_t name, scope_index region,
                              bool specialization)
{
    /* [class.union.anon]/1: the members of an unnamed class that declares
       nothing else (an anonymous union, clang's anonymous struct) are
       names of the enclosing class, which a mem-initializer-id or a
       member function's body before it may use: read in the first pass */
    bool anonymous_members =
        !name && at(preprocessing_op_or_punc::SEMICOLON, group_end(0));
    if (collecting_members && !anonymous_members)
        /* a nested class in its enclosing class's first pass: its members
           are its own, read when the enclosing class is read in full */
        return unparsed_group();
    add_token(); /* { */
    scope_index outer = names.current();
    /* [temp.local]/1: a class template's injected-class-name is a
       template-name, or a type-name without template arguments; a
       specialization's, O<true>, is the template's too */
    name_kind injected = name_kind::type_name;
    if (name && (specialization || declared_kind(name_kind::type_name) ==
                                       name_kind::template_name))
        injected = name_kind::template_name;
    names.reenter(region);
    if (name)
        names.declare(name, injected, region);
    uint32_t enclosing_class = member_class_name;
    member_class_name = name;
    enclosing_braces++;
    scope_index complete = 0;
    {
        tentative_parse first_pass(*this);
        flag_set collecting(collecting_members, true);
        member_specification();
        complete = names.snapshot(region);
    }
    names.set_complete(region, complete);
    member_specification();
    enclosing_braces--;
    member_class_name = enclosing_class;
    names.reenter(outer);
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
        return expected("'}' to end the member-specification", "class.pre");
    return 0;
}

/*
 * member-specification_opt, to the class's '}': each member-declaration
 * recovers as a declaration does ([intro.compliance]/2.2).
 *
 *     access-specifier:
 *         private
 *         protected
 *         public
 */
node_index parser::member_specification()
{
    size_t mark = begin_node();
    token_index first = position;
    while (!at_end() && !at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        if (is_access_specifier(keyword_of(peek())) &&
            at(preprocessing_op_or_punc::COLON, 1)) {
            add_token();
            add_token();
            continue;
        }
        token_index before = position;
        member_declaration();
        if (position == before)
            add_token(); /* nothing recovered: no progress otherwise */
    }
    if (position == first)
        return 0;
    return finish_node(syntax_kind::member_specification, mark, first);
}

node_index parser::member_declaration()
{
    return recover_declaration(&parser::member_declaration_alternative);
}

/*
 * Which alternative of member-declaration the tokens are, by their first
 * ones.  template-declaration, explicit-specialization and deduction-guide
 * are P7's.
 */
node_index parser::member_declaration_alternative()
{
    if (at(extension_keyword::extension)) /* [ext.gnu] */
        return gnu_extension_prefixed(&parser::member_declaration_alternative);
    /* [temp.res]/5: "a parameter-declaration in a member-declaration,
       unless that parameter-declaration appears in a default argument" */
    flag_set implicit(parameters_implicit_typename, true);
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::SEMICOLON)) {
        add_token();
        return finish_node(syntax_kind::empty_declaration, mark, first);
    }
    node_index shared = 0;
    if (shared_declaration_alternative(&shared))
        return shared;
    if (at(standard_keyword::template_))
        return template_declaration(&parser::member_declaration_alternative);
    if (deduction_guide_follows())
        return deduction_guide();
    /* [module.interface]/1: "An export-declaration shall appear only at
       namespace scope" */
    if (at(standard_keyword::export_))
        return violates("an export-declaration only at namespace scope",
                        "module.interface", position);
    /* an attribute-specifier-seq before a using-declaration: clang's
       extension, P9's */
    unsigned after_attributes = attribute_specifier_seq_end(0);
    if (after_attributes && at(standard_keyword::using_, after_attributes))
        return beyond_this_step();

    /* attribute-specifier-seq_opt decl-specifier-seq_opt
       member-declarator-list_opt ; and function-definition */
    token_index attributes_at = position;
    node_index attributes = attribute_specifier_seq();
    if (failed())
        return 0;
    decl_specifier_facts specifiers;
    specifiers.constructor_name = member_class_name;
    /* [temp.res]/5: "a decl-specifier of the decl-specifier-seq of a
       member-declaration" */
    specifiers.implicit_typename =
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    /* [class.mem]/12: "The decl-specifier-seq may be omitted in
       constructor, destructor, and conversion function declarations only":
       a declarator-id that is an identifier is the class's name */
    if (!specifiers.node && at_identifier() &&
        !at(preprocessing_op_or_punc::COLON_COLON, 1) &&
        tokens.identifier(position) != member_class_name)
        return violates("a decl-specifier-seq, omitted only by a "
                        "constructor, destructor or conversion function",
                        "class.mem", position);
    /* C++26's friend-type-specifier-list (P2893R3, friend T, U; and
       friend Ts...;): a later edition's, P9's */
    if (specifiers.friend_ && (at(preprocessing_op_or_punc::COMMA) ||
                               at(preprocessing_op_or_punc::ELLIPSIS)))
        return beyond_this_step();
    if (at(preprocessing_op_or_punc::SEMICOLON)) {
        /* [dcl.type.elab]/1: an elaborated-type-specifier alone only as
           class-key attribute-specifier-seq_opt identifier ;, or in a
           friend declaration */
        if (specifiers.elaborated && specifiers.count == 1 &&
            !specifiers.elaborated_declaring_form && !explicit_declaration)
            return violates("an elaborated-type-specifier alone only as "
                            "class-key attribute-specifier-seq_opt "
                            "identifier ;",
                            "dcl.type.elab", specifiers.elaborated_at);
        add_token();
        return finish_node(syntax_kind::member_declaration, mark, first);
    }
    size_t list_mark = begin_node();
    token_index list_first = position;
    bool definition = false;
    if (template_declarator_violates(specifiers, true))
        return 0;
    member_declarator(specifiers, list_mark, list_first, true, &definition);
    if (failed())
        return 0;
    if (definition)
        return finish_node(syntax_kind::function_definition, mark, first);
    /* [dcl.attr.grammar]/5: "If an attribute-specifier-seq appertains to a
       friend declaration ([class.friend]), that declaration shall be a
       definition."  [ext:gnu_attributes] GCC's attributes are allowed there
       (libc++'s _LIBCPP_HIDE_FROM_ABI friend noop_coroutine) */
    if (attributes && specifiers.friend_ &&
        !only_gnu_attributes(tree, attributes))
        return violates("attributes of a friend declaration only on a "
                        "definition",
                        "dcl.attr.grammar", attributes_at);
    unsigned count = 1;
    while (accept(preprocessing_op_or_punc::COMMA)) {
        if (template_declarator_violates(specifiers, false))
            return 0;
        member_declarator(specifiers, begin_node(), position, false,
                          &definition);
        if (failed())
            return 0;
        count++;
    }
    if (count > 1)
        finish_node(syntax_kind::member_declarator_list, list_mark, list_first);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the member-declaration", "class.mem");
    return finish_node(syntax_kind::member_declaration, mark, first);
}

/*
 *     member-declarator:
 *         declarator virt-specifier-seq_opt pure-specifier_opt
 *         declarator requires-clause
 *         declarator brace-or-equal-initializer_opt
 *         identifier_opt attribute-specifier-seq_opt : constant-expression brace-or-equal-initializer_opt
 *
 *     pure-specifier:
 *         = 0
 *
 * and, for the first declarator, function-definition ([dcl.fct.def.general]):
 *
 *     function-definition:
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator virt-specifier-seq_opt function-body
 *
 * *definition: it was a function-definition, whose parts are left pending
 * for the caller's node.  [class.bit]/1: in a bit-field's member-declarator
 * "The constant-expression ... is parsed as the longest sequence of tokens
 * that could syntactically form a constant-expression" ([class.mem]/10:
 * true ? 8 : a = 42 has no brace-or-equal-initializer).  The
 * requires-clause is P7's.  [basic.scope.pdecl]/1: a member is declared
 * after its declarator, before its initializer.
 */
node_index parser::member_declarator(const decl_specifier_facts &specifiers,
                                     size_t mark, token_index first,
                                     bool first_one, bool *definition)
{
    unsigned after_name = 0;
    if (at_identifier()) {
        after_name = attribute_specifier_seq_end(1);
        if (!after_name)
            after_name = 1;
    }
    if (at(preprocessing_op_or_punc::COLON, after_name)) {
        /* a bit-field */
        if (after_name) {
            if (!specifiers.friend_)
                names.declare(tokens.identifier(position), name_kind::other);
            add_token();
            attribute_specifier_seq();
            if (failed())
                return 0;
        }
        add_token(); /* : */
        constant_expression();
        if (failed())
            return 0;
        if (at(preprocessing_op_or_punc::EQUAL) ||
            at(preprocessing_op_or_punc::LEFT_BRACE)) {
            /* P0683R1 (C++20); before it [ext.clang] clang's extension */
            if (!tokens.files.behavior.has(
                    language_feature::bit_field_default_member_initializers))
                return beyond_this_step();
            if (collecting_members) {
                if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                    unparsed_group();
                } else {
                    add_token();
                    static const preprocessing_op_or_punc stops[] = {
                        preprocessing_op_or_punc::COMMA,
                        preprocessing_op_or_punc::SEMICOLON};
                    unparsed_until(stops, 2);
                }
            } else {
                complete_class_context context(names);
                initializer();
            }
            if (failed())
                return 0;
        }
        return finish_node(syntax_kind::member_declarator, mark, first);
    }

    declarator_facts facts;
    {
        flag_set context(member_declarator_context, true);
        declarator(declarator_form::concrete, facts);
    }
    if (failed())
        return 0;
    /* a friend declaration declares no member ([class.friend]/1: "A friend
       of a class is a function or class that is given permission to name
       the private and protected members of the class") */
    /* a constructor's declarator-id is the injected-class-name, which
       it does not redeclare ([class.ctor]/1) */
    if (facts.name && !facts.qualified_id && !specifiers.friend_ &&
        facts.name != member_class_name)
        declare_declarator(specifiers, facts);
    bool function = facts.first == derivation::function;
    bool virt = false;
    /* [class.mem]/1: member-declarator: declarator requires-clause */
    /* a requires-clause after a member that is no function: clang's
       error is semantic, [dcl.decl]/3 */
    if (at(standard_keyword::requires_)) {
        requires_clause();
        if (failed())
            return 0;
    } else if (function && at_virt_specifier()) {
        virt_specifier_seq();
        if (failed())
            return 0;
        virt = true;
    }
    if (function && first_one &&
        (at(preprocessing_op_or_punc::LEFT_BRACE) ||
         at(preprocessing_op_or_punc::COLON) || at(standard_keyword::try_) ||
         (at(preprocessing_op_or_punc::EQUAL) &&
          (at(standard_keyword::default_, 1) ||
           at(standard_keyword::delete_, 1))))) {
        if (specifiers.typedef_)
            return violates("no typedef in a function-definition",
                            "dcl.typedef", specifiers.typedef_at);
        function_body(facts, true);
        if (failed())
            return 0;
        *definition = true;
        return 0;
    }
    bool more = virt;
    if (at(preprocessing_op_or_punc::EQUAL) && function &&
        spelled_zero(*this, 1)) {
        size_t pure_mark = begin_node();
        token_index pure_first = position;
        add_token();
        add_token();
        finish_node(syntax_kind::pure_specifier, pure_mark, pure_first);
        more = true;
        /* a function body after the pure-specifier is Microsoft's
           extension, P9's */
        const implementation_defined_behavior &b = tokens.files.behavior;
        if (at(preprocessing_op_or_punc::LEFT_BRACE) &&
            (b.microsoft_extensions || b.microsoft_compatibility))
            return beyond_this_step();
    } else if (!virt && (at(preprocessing_op_or_punc::EQUAL) ||
                         at(preprocessing_op_or_punc::LEFT_BRACE))) {
        /* brace-or-equal-initializer: a default member initializer
           ([class.mem]/9), N2756 (C++11); before it, [class.static.data]/4
           an in-class initializer of a static data member, and of a
           non-static one [ext.clang] clang's extension, P9's */
        if (!specifiers.static_ &&
            !tokens.files.behavior.has(
                language_feature::default_member_initializers))
            return beyond_this_step();
        /* [dcl.array]/7: "An array bound may also be omitted when an
           object (but not a non-static data member) of array type is
           initialized": declarator-id [ ] */
        if (!specifiers.static_ && facts.first == derivation::array &&
            facts.has_id &&
            is_operator(tokens[facts.id_last + 1],
                        preprocessing_op_or_punc::LEFT_BRACKET) &&
            is_operator(tokens[facts.id_last + 2],
                        preprocessing_op_or_punc::RIGHT_BRACKET))
            return violates("a non-static data member's array bound",
                            "dcl.array", facts.id_last + 1);
        if (collecting_members) {
            if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                unparsed_group();
            } else {
                add_token();
                static const preprocessing_op_or_punc stops[] = {
                    preprocessing_op_or_punc::COMMA,
                    preprocessing_op_or_punc::SEMICOLON};
                unparsed_until(stops, 2);
            }
        } else {
            complete_class_context context(names);
            initializer();
        }
        if (failed())
            return 0;
        more = true;
    }
    if (!more)
        return 0; /* the declarator alone passes through */
    return finish_node(syntax_kind::member_declarator, mark, first);
}

} // namespace cplusplus
