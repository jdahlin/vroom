/*
 * dcl.attr.grammar.cpp: [dcl.attr.grammar] Attribute syntax and semantics,
 * with the argument rules of the standard attributes ([dcl.attr.depend] to
 * [dcl.attr.nouniqueaddr]).
 *
 *     attribute-specifier-seq:
 *         attribute-specifier-seq_opt attribute-specifier
 *
 * [dcl.attr.grammar]/5: an attribute-specifier-seq appertains to what its
 * syntactic context says; whether an attribute may apply to that is a
 * semantic rule, not checked here.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* the bracket that closes an opening one, or count */
preprocessing_op_or_punc closing_of(const token &t)
{
    if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return preprocessing_op_or_punc::RIGHT_PARENTHESIS;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET))
        return preprocessing_op_or_punc::RIGHT_BRACKET;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACE))
        return preprocessing_op_or_punc::RIGHT_BRACE;
    return preprocessing_op_or_punc::count;
}

bool is_closing(const token &t)
{
    return is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE);
}

const char *closing_spelling(preprocessing_op_or_punc op)
{
    switch (op) {
    case preprocessing_op_or_punc::RIGHT_PARENTHESIS:
        return "')'";
    case preprocessing_op_or_punc::RIGHT_BRACKET:
        return "']'";
    default:
        return "'}'";
    }
}

/*
 * The standard attributes ([dcl.attr]) and what their attribute-token
 * allows of an attribute-argument-clause.
 */
enum class standard_arguments {
    /* not one of the standard's attribute-tokens */
    not_standard,
    /* "no attribute-argument-clause shall be present" */
    none,
    /* "An attribute-argument-clause may be present and, if present, it
       shall have the form: ( string-literal )" */
    string_literal,
};

struct standard_attribute {
    const char *token;
    standard_arguments arguments;
    const char *subclause;
};

/*
 * "It shall appear at most once in each attribute-list", which each of
 * these subclauses says in n4861, is not checked: P2156R1 (C++23) removed
 * it, and implementations apply that to every edition.
 */
constexpr standard_attribute standard_attributes[] = {
    {"carries_dependency", standard_arguments::none, "dcl.attr.depend"},
    {"deprecated", standard_arguments::string_literal, "dcl.attr.deprecated"},
    {"fallthrough", standard_arguments::none, "dcl.attr.fallthrough"},
    {"likely", standard_arguments::none, "dcl.attr.likelihood"},
    {"unlikely", standard_arguments::none, "dcl.attr.likelihood"},
    {"maybe_unused", standard_arguments::none, "dcl.attr.unused"},
    {"nodiscard", standard_arguments::string_literal, "dcl.attr.nodiscard"},
    {"noreturn", standard_arguments::none, "dcl.attr.noreturn"},
    {"no_unique_address", standard_arguments::none, "dcl.attr.nouniqueaddr"},
};

const standard_attribute *find_standard_attribute(const char *s, size_t n)
{
    for (const standard_attribute &a : standard_attributes)
        if (strlen(a.token) == n && !memcmp(a.token, s, n))
            return &a;
    return nullptr;
}

} // namespace

/*
 *     attribute-specifier:
 *         [ [ attribute-using-prefix_opt attribute-list ] ]
 *         alignment-specifier
 */
bool parser::at_attribute_specifier(unsigned ahead)
{
    return (at(preprocessing_op_or_punc::LEFT_BRACKET, ahead) &&
            at(preprocessing_op_or_punc::LEFT_BRACKET, ahead + 1)) ||
           at(standard_keyword::alignas_, ahead);
}

unsigned parser::attribute_specifier_seq_end(unsigned ahead)
{
    while (at_attribute_specifier(ahead)) {
        unsigned i = ahead;
        if (at(standard_keyword::alignas_, i))
            i++;
        if (closing_of(peek(i)) == preprocessing_op_or_punc::count)
            return ahead;
        unsigned depth = 0;
        for (;; i++) {
            const token &t = peek(i);
            if (t.kind == token_kind::end_of_file)
                return ahead;
            if (closing_of(t) != preprocessing_op_or_punc::count)
                depth++;
            else if (is_closing(t) && !--depth)
                break;
        }
        ahead = i + 1;
    }
    return ahead;
}

/*
 * attribute-specifier-seq_opt.  [dcl.attr.likelihood]/1: "The
 * attribute-token likely shall not appear in an attribute-specifier-seq
 * that contains the attribute-token unlikely."
 *
 * [dcl.attr.grammar]/5: "If an attribute-specifier-seq that appertains to
 * some entity or statement contains an attribute or alignment-specifier
 * that is not allowed to apply to that entity or statement, the program is
 * ill-formed."  Where the seq appertains to a type (after a
 * decl-specifier-seq, a ptr-operator, an array's ']' or
 * parameters-and-qualifiers: [dcl.spec]/1, [dcl.ptr]/1, [dcl.array]/3,
 * [dcl.fct]/1), none of the standard's attributes may: each subclause
 * names what its attribute applies to, and no type is among them; nor may
 * an alignment-specifier ([dcl.align]/1: "a variable or ... a class data
 * member", and a class or enumeration).
 */
node_index parser::attribute_specifier_seq(bool appertains_to_type)
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_facts facts;
    unsigned count = 0;
    node_index last = 0;
    while (at_attribute_specifier()) {
        token_index at = position;
        bool both = facts.likely && facts.unlikely;
        last = attribute_specifier(facts);
        if (failed())
            return 0;
        if (facts.likely && facts.unlikely && !both)
            return violates("likely and unlikely not in the same "
                            "attribute-specifier-seq",
                            "dcl.attr.likelihood", at);
        count++;
    }
    if (appertains_to_type && facts.standard)
        return violates("this attribute does not apply to a type",
                        facts.standard_subclause, facts.standard_at);
    if (count < 2)
        return last; /* one attribute-specifier passes through */
    return finish_node(syntax_kind::attribute_specifier_seq, mark, first);
}

/*
 *     alignment-specifier:
 *         alignas ( type-id ..._opt )
 *         alignas ( constant-expression ..._opt )
 *
 *     attribute-using-prefix:
 *         using attribute-namespace :
 *
 *     attribute-list:
 *         attribute_opt
 *         attribute-list , attribute_opt
 *         attribute ...
 *         attribute-list , attribute ...
 *
 * Which of type-id and constant-expression an alignment-specifier holds is
 * [dcl.ambig.res]/2's rule: a type-id when the tokens can be one.  Its
 * ellipsis ([dcl.align]/1: "An alignment-specifier with an ellipsis is a
 * pack expansion") is P7's.
 * [dcl.attr.grammar]/2: "If an attribute-specifier contains an
 * attribute-using-prefix, the attribute-list following that
 * attribute-using-prefix shall not contain an attribute-scoped-token".
 */
node_index parser::attribute_specifier(attribute_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::alignas_)) {
        if (!facts.standard) {
            facts.standard = true;
            facts.standard_at = position;
            facts.standard_subclause = "dcl.align";
        }
        add_token();
        if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after alignas", "dcl.align");
        if (at(preprocessing_op_or_punc::ELLIPSIS, group_end(0) - 2))
            return beyond_this_step();
        if (!parenthesized_type_id(false)) {
            if (failed())
                return 0;
            add_token();
            constant_expression();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' to close the alignment-specifier",
                                "dcl.align");
        }
        return finish_node(syntax_kind::alignment_specifier, mark, first);
    }
    add_token(); /* [ */
    add_token(); /* [ */
    bool using_prefix = false;
    if (at(standard_keyword::using_)) {
        size_t prefix_mark = begin_node();
        token_index prefix_first = position;
        add_token();
        if (!at_attribute_identifier())
            return expected("an attribute-namespace after 'using'",
                            "dcl.attr.grammar");
        add_token();
        if (!accept(preprocessing_op_or_punc::COLON))
            return expected("':' after the attribute-namespace",
                            "dcl.attr.grammar");
        finish_node(syntax_kind::attribute_using_prefix, prefix_mark,
                    prefix_first);
        using_prefix = true;
    }
    size_t list_mark = begin_node();
    token_index list_first = position;
    bool list = false;
    for (;;) {
        if (at_attribute_identifier()) {
            attribute(using_prefix, facts);
            if (failed())
                return 0;
            if (at(preprocessing_op_or_punc::ELLIPSIS))
                /* [dcl.attr.grammar]/4: "In an attribute-list, an ellipsis
                   may appear only if that attribute's specification permits
                   it": none of the standard's does, nor do the attributes
                   this implementation recognizes */
                return violates("an ellipsis only after an attribute whose "
                                "specification permits it",
                                "dcl.attr.grammar", position);
        } else if (!at(preprocessing_op_or_punc::COMMA) &&
                   !at(preprocessing_op_or_punc::RIGHT_BRACKET)) {
            return expected("an attribute", "dcl.attr.grammar");
        }
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
        list = true;
    }
    if (list)
        finish_node(syntax_kind::attribute_list, list_mark, list_first);
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET) ||
        !accept(preprocessing_op_or_punc::RIGHT_BRACKET))
        return expected("']]' to end the attribute-specifier",
                        "dcl.attr.grammar");
    return finish_node(syntax_kind::attribute_specifier, mark, first);
}

/*
 * [dcl.attr.grammar]/4: "If a keyword ([lex.key]) or an alternative token
 * ([lex.digraph]) that satisfies the syntactic requirements of an
 * identifier ([lex.name]) is contained in an attribute-token, it is
 * considered an identifier."
 */
bool parser::at_attribute_identifier(unsigned ahead)
{
    const token &t = peek(ahead);
    if (t.kind == token_kind::identifier)
        return true;
    if (t.kind != token_kind::keyword && t.kind != token_kind::literal &&
        t.kind != token_kind::operator_or_punctuator)
        return false;
    owned_buffer spelling;
    tokens.spelling(position + ahead, spelling.get());
    const char *s = spelling.data();
    size_t n = spelling.size();
    if (!n || !is_nondigit(static_cast<unsigned char>(s[0])))
        return false;
    for (size_t i = 1; i < n; i++)
        if (!is_nondigit(static_cast<unsigned char>(s[i])) &&
            !is_digit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

/*
 *     attribute:
 *         attribute-token attribute-argument-clause_opt
 *
 *     attribute-token:
 *         identifier
 *         attribute-scoped-token
 *
 *     attribute-scoped-token:
 *         attribute-namespace :: identifier
 *
 *     attribute-namespace:
 *         identifier
 *
 *     attribute-argument-clause:
 *         ( balanced-token-seq_opt )
 *
 * [dcl.attr.grammar]/4: "The attribute-token determines additional
 * requirements on the attribute-argument-clause (if any)": the standard's
 * attribute-tokens' are checked here.  An attribute-token with an
 * attribute-using-prefix is scoped ([dcl.attr.grammar]/2), so none of them.
 */
node_index parser::attribute(bool using_prefix, attribute_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    const standard_attribute *standard = nullptr;
    if (at(preprocessing_op_or_punc::COLON_COLON, 1)) {
        if (using_prefix)
            return violates("no attribute-scoped-token after an "
                            "attribute-using-prefix",
                            "dcl.attr.grammar", position);
        add_token();
        add_token();
        if (!at_attribute_identifier())
            return expected("an identifier after '::'", "dcl.attr.grammar");
        add_token();
        finish_node(syntax_kind::attribute_scoped_token, mark, first);
    } else {
        if (!using_prefix) {
            owned_buffer spelling;
            tokens.spelling(position, spelling.get());
            standard =
                find_standard_attribute(spelling.data(), spelling.size());
            if (standard && !strcmp(standard->token, "likely"))
                facts.likely = true;
            if (standard && !strcmp(standard->token, "unlikely"))
                facts.unlikely = true;
            if (standard && !facts.standard) {
                facts.standard = true;
                facts.standard_at = position;
                facts.standard_subclause = standard->subclause;
            }
        }
        add_token();
    }
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return 0; /* the attribute-token alone passes through */
    token_index clause = position;
    size_t clause_mark = begin_node();
    add_token();
    token_index arguments = position;
    balanced_token_seq(preprocessing_op_or_punc::RIGHT_PARENTHESIS);
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the attribute-argument-clause",
                        "dcl.attr.grammar");
    finish_node(syntax_kind::attribute_argument_clause, clause_mark, clause);
    if (standard && standard->arguments == standard_arguments::none)
        return violates("no attribute-argument-clause for this attribute",
                        standard->subclause, clause);
    if (standard && standard->arguments == standard_arguments::string_literal &&
        !(tokens[arguments].literal == literal_kind::string_literal &&
          is_operator(tokens[arguments + 1],
                      preprocessing_op_or_punc::RIGHT_PARENTHESIS)))
        return violates("an attribute-argument-clause of the form ( "
                        "string-literal )",
                        standard->subclause, arguments);
    return finish_node(syntax_kind::attribute, mark, first);
}

/*
 *     balanced-token-seq:
 *         balanced-token
 *         balanced-token-seq balanced-token
 *
 *     balanced-token:
 *         ( balanced-token-seq_opt )
 *         [ balanced-token-seq_opt ]
 *         { balanced-token-seq_opt }
 *         any token other than a parenthesis, a bracket, or a brace
 *
 * Up to (not including) closing; nothing when it is there at once.  A
 * token passes through; a bracketed balanced-token is a node.
 */
node_index parser::balanced_token_seq(preprocessing_op_or_punc closing)
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    node_index last = 0;
    while (!at(closing)) {
        const token &t = peek();
        if (at_end())
            return expected(closing_spelling(closing), "dcl.attr.grammar");
        preprocessing_op_or_punc inner = closing_of(t);
        if (inner != preprocessing_op_or_punc::count) {
            size_t group_mark = begin_node();
            token_index group_first = position;
            add_token();
            balanced_token_seq(inner);
            if (failed())
                return 0;
            if (!accept(inner))
                return expected(closing_spelling(inner), "dcl.attr.grammar");
            last = finish_node(syntax_kind::balanced_token, group_mark,
                               group_first);
        } else if (is_closing(t)) {
            return expected(closing_spelling(closing), "dcl.attr.grammar");
        } else {
            add_token();
            last = 0;
        }
        count++;
    }
    if (count < 2)
        return last;
    return finish_node(syntax_kind::balanced_token_seq, mark, first);
}

/*
 * [dcl.attr.grammar]/7: "Two consecutive left square bracket tokens shall
 * appear only when introducing an attribute-specifier or within the
 * balanced-token-seq of an attribute-argument-clause."
 */
node_index parser::misplaced_attribute_brackets()
{
    return violates("'[[' only where an attribute-specifier can be",
                    "dcl.attr.grammar", position);
}

} // namespace cplusplus
