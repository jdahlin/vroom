/*
 * dcl.enum.cpp: [dcl.enum] Enumeration declarations.
 *
 *     enum-name:
 *         identifier
 *
 *     enum-key:
 *         enum
 *         enum class
 *         enum struct
 *
 * [dcl.enum]/2: "The enumeration type declared with an enum-key of only
 * enum is an unscoped enumeration, and its enumerators are unscoped
 * enumerators."  An unscoped enumerator is declared in the scope the
 * enumeration is in ([basic.scope.enum]); a scoped one in the
 * enumeration's own, which declared_names does not keep.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* enum-key: one token passes through; *scoped when it is enum class or
   enum struct */
void enum_key(parser &p, bool *scoped)
{
    size_t mark = p.begin_node();
    token_index first = p.position;
    p.add_token(); /* enum */
    *scoped = p.accept(standard_keyword::class_) ||
              p.accept(standard_keyword::struct_);
    if (*scoped)
        p.finish_node(syntax_kind::enum_key, mark, first);
}

/*
 *     enum-head-name:
 *         nested-name-specifier_opt identifier
 *
 * An identifier alone passes through.  *name is the identifier when there
 * is no nested-name-specifier.  Nothing when there is no identifier.
 */
node_index enum_head_name(parser &p, uint32_t *name, bool *qualified)
{
    size_t mark = p.begin_node();
    token_index first = p.position;
    *name = 0;
    *qualified = false;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = 0;
    if (p.nested_name_specifier_follows(0)) {
        qualifier = p.nested_name_specifier(&scope, &known);
        if (p.failed())
            return 0;
    }
    if (!p.at_identifier()) {
        if (qualifier)
            return p.expected("an identifier after the nested-name-specifier",
                              "dcl.enum");
        return 0;
    }
    uint32_t identifier = p.tokens.identifier(p.position);
    p.add_token();
    /* enum-head-name: nested-name-specifier_opt identifier: no
       simple-template-id names an enumeration */
    if (p.at(preprocessing_op_or_punc::LESS))
        return p.expected("no template-argument-list after an enum-name",
                          "dcl.enum");
    if (!qualifier) {
        *name = identifier;
        return 0;
    }
    *qualified = true;
    return p.finish_node(syntax_kind::enum_head_name, mark, first);
}

/*
 *     enum-base:
 *         : type-specifier-seq
 */
node_index enum_base(parser &p)
{
    size_t mark = p.begin_node();
    token_index first = p.position;
    p.add_token();
    decl_specifier_facts base;
    base.type_specifier_seq = true;
    p.decl_specifier_seq(base, true);
    if (p.failed())
        return 0;
    if (!base.node &&
        (p.at_identifier() || p.at(preprocessing_op_or_punc::COLON_COLON)))
        /* a name the parser cannot classify (declared in a class, a
           template, or by a declaration kept as tokens) */
        return p.beyond_this_step();
    if (!base.node)
        return p.expected("a type-specifier-seq after ':'", "dcl.enum");
    return p.finish_node(syntax_kind::enum_base, mark, first);
}

} // namespace

/*
 *     opaque-enum-declaration:
 *         enum-key attribute-specifier-seq_opt enum-head-name enum-base_opt ;
 *
 * What follows the enum-key decides it before parsing: an enum-head-name
 * then ';', or an enum-base that ends with ';' (a type-specifier-seq has no
 * braces).  Otherwise the enum begins a decl-specifier-seq.
 */
bool parser::opaque_enum_declaration_follows()
{
    unsigned i = 1;
    if (at(standard_keyword::class_, i) || at(standard_keyword::struct_, i))
        i++;
    i = attribute_specifier_seq_end(i);
    if (at(preprocessing_op_or_punc::COLON_COLON, i))
        i++;
    while (at_identifier(i) && at(preprocessing_op_or_punc::COLON_COLON, i + 1))
        i += 2;
    if (!at_identifier(i))
        return false;
    i++;
    if (at(preprocessing_op_or_punc::SEMICOLON, i))
        return true;
    if (!at(preprocessing_op_or_punc::COLON, i))
        return false;
    unsigned depth = 0;
    for (i++;; i++) {
        const token &t = peek(i);
        if (t.kind == token_kind::end_of_file)
            return false;
        if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
            is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET))
            depth++;
        else if ((is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                  is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET)) &&
                 depth)
            depth--;
        else if (!depth && is_operator(t, preprocessing_op_or_punc::LEFT_BRACE))
            return false;
        else if (!depth && is_operator(t, preprocessing_op_or_punc::SEMICOLON))
            return true;
    }
}

/*
 * [dcl.enum]/1: "If the enum-head-name of an opaque-enum-declaration
 * contains a nested-name-specifier, the declaration shall be an explicit
 * specialization" (a template-declaration's, P7's).  [dcl.enum]/2: "An
 * opaque-enum-declaration declaring an unscoped enumeration shall not omit
 * the enum-base."
 */
node_index parser::opaque_enum_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    bool scoped = false;
    enum_key(*this, &scoped);
    attribute_specifier_seq();
    if (failed())
        return 0;
    token_index name_first = position;
    uint32_t name = 0;
    bool qualified = false;
    enum_head_name(*this, &name, &qualified);
    if (failed())
        return 0;
    if (qualified)
        return violates("an opaque-enum-declaration with a "
                        "nested-name-specifier only as an explicit "
                        "specialization",
                        "dcl.enum", name_first);
    bool base = false;
    if (at(preprocessing_op_or_punc::COLON)) {
        enum_base(*this);
        if (failed())
            return 0;
        base = true;
    }
    if (!scoped && !base)
        return violates("an enum-base in the opaque-enum-declaration of an "
                        "unscoped enumeration",
                        "dcl.enum", position);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the opaque-enum-declaration", "dcl.enum");
    if (name)
        names.declare(name, name_kind::type_name);
    return finish_node(syntax_kind::opaque_enum_declaration, mark, first);
}

/*
 *     enum-specifier:
 *         enum-head { enumerator-list_opt }
 *         enum-head { enumerator-list , }
 *
 *     enum-head:
 *         enum-key attribute-specifier-seq_opt enum-head-name_opt enum-base_opt
 *
 * and [dcl.type.elab]'s
 *
 *     elaborated-enum-specifier:
 *         enum nested-name-specifier_opt identifier
 *
 * which is what an enum in a decl-specifier-seq is without the '{' (an
 * opaque-enum-declaration is a declaration of its own).  [dcl.type]/1: a
 * type-id's type-specifier-seq has no enum-specifier.  [dcl.enum]/2: "The
 * optional enum-head-name shall not be omitted in the declaration of a
 * scoped enumeration."  [basic.scope.pdecl]/3: the enum-name is declared
 * "immediately after the identifier ... in its enum-head".
 */
node_index parser::enum_specifier_or_elaborated(decl_specifier_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    bool scoped = false;
    enum_key(*this, &scoped);
    node_index attributes = attribute_specifier_seq();
    if (failed())
        return 0;
    uint32_t name = 0;
    bool qualified = false;
    token_index name_first = position;
    enum_head_name(*this, &name, &qualified);
    if (failed())
        return 0;
    bool has_name = position > name_first;
    bool base = false;
    if (at(preprocessing_op_or_punc::COLON)) {
        enum_base(*this);
        if (failed())
            return 0;
        base = true;
    }
    /* [class.friend]/2 as clang reads it: no type is defined in a friend
       declaration */
    if (facts.friend_ && at(preprocessing_op_or_punc::LEFT_BRACE) &&
        !facts.type_specifier_seq)
        return violates("no enumeration defined in a friend declaration",
                        "class.friend", facts.friend_at);
    facts.declares_type = true;
    if (!at(preprocessing_op_or_punc::LEFT_BRACE) || facts.type_specifier_seq) {
        if (scoped || attributes || base)
            return expected("'{' to begin the enum-specifier's "
                            "enumerator-list",
                            "dcl.enum");
        if (!has_name)
            return expected("an identifier or '{' after 'enum'",
                            "dcl.type.elab");
        /* [dcl.type.elab]/3: "the enum keyword shall be used to refer to
           an enumeration"; a name not declared yet is the semantic
           analysis's error, and declared here so that the rest of the
           declaration parses */
        if (name && names.lookup_elaborated(name) != name_kind::type_name)
            names.declare(name, name_kind::type_name);
        facts.elaborated = true;
        facts.elaborated_at = first;
        return finish_node(syntax_kind::elaborated_enum_specifier, mark, first);
    }
    if (scoped && !has_name)
        return violates("an enum-head-name in the declaration of a scoped "
                        "enumeration",
                        "dcl.enum", position);
    if (pending.size() - mark > 1)
        finish_node(syntax_kind::enum_head, mark, first);
    if (name)
        names.declare(name, name_kind::type_name);
    add_token(); /* { */
    enumerator_list(scoped);
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
        return expected("'}' to end the enumerator-list", "dcl.enum");
    return finish_node(syntax_kind::enum_specifier, mark, first);
}

/*
 *     enumerator-list:
 *         enumerator-definition
 *         enumerator-list , enumerator-definition
 *
 *     enumerator-definition:
 *         enumerator
 *         enumerator = constant-expression
 *
 *     enumerator:
 *         identifier attribute-specifier-seq_opt
 *
 * A ',' before the '}' is the enum-specifier's.  [basic.scope.pdecl]/4: "The point of
 * declaration for an enumerator is immediately after its
 * enumerator-definition."
 */
node_index parser::enumerator_list(bool scoped)
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    while (!at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        size_t definition_mark = begin_node();
        token_index definition_first = position;
        if (!at_identifier())
            return expected("an enumerator", "dcl.enum");
        uint32_t name = tokens.identifier(position);
        add_token();
        if (at_attribute_specifier()) {
            attribute_specifier_seq();
            if (failed())
                return 0;
            finish_node(syntax_kind::enumerator, definition_mark,
                        definition_first);
        }
        if (accept(preprocessing_op_or_punc::EQUAL)) {
            constant_expression();
            if (failed())
                return 0;
            finish_node(syntax_kind::enumerator_definition, definition_mark,
                        definition_first);
        }
        if (!scoped)
            names.declare(name, name_kind::other);
        count++;
        if (at(preprocessing_op_or_punc::COMMA) &&
            at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        if (!at(preprocessing_op_or_punc::COMMA))
            break;
        add_token();
    }
    node_index list = 0;
    if (count > 1)
        list = finish_node(syntax_kind::enumerator_list, mark, first);
    accept(preprocessing_op_or_punc::COMMA);
    return list;
}

/*
 * [enum.udecl]:
 *
 *     using-enum-declaration:
 *         using elaborated-enum-specifier ;
 *
 * [enum.udecl]/2: it "introduces the enumerator names of the named
 * enumeration as if by a using-declaration for each enumerator": they are
 * not types, and declared_names does not keep an enumeration's
 * enumerators, so nothing is declared.
 */
node_index parser::using_enum_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* using */
    size_t specifier_mark = begin_node();
    token_index specifier_first = position;
    add_token(); /* enum */
    if (at(standard_keyword::class_) || at(standard_keyword::struct_))
        return expected("a nested-name-specifier or identifier after 'enum' "
                        "(an elaborated-enum-specifier has no 'class')",
                        "dcl.type.elab");
    scope_index scope = 0;
    bool known = false;
    nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!at_identifier())
        return expected("an enum-name", "dcl.type.elab");
    add_token();
    if (at(preprocessing_op_or_punc::LESS))
        return beyond_this_step(); /* a simple-template-id: P7 */
    finish_node(syntax_kind::elaborated_enum_specifier, specifier_mark,
                specifier_first);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the using-enum-declaration", "enum.udecl");
    return finish_node(syntax_kind::using_enum_declaration, mark, first);
}

} // namespace cplusplus
