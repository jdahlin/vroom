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
#include "frontend/cplusplus/sema.h"

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
    /* [basic.scope.enum]/1: the enumerators' region */
    scope_index region = names.enter(scope_kind::enumeration);
    names.leave();
    if (name)
        names.declare(name, name_kind::type_name, region);
    add_token(); /* { */
    enumerator_list(scoped, region);
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
node_index parser::enumerator_list(bool scoped, scope_index region)
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
        names.declare_in(region, name, name_kind::other);
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

/* ---- semantic analysis ---------------------------------------------- */

namespace cplusplus
{

namespace
{

/* the bits a value needs: as an unsigned number (0 for 0), or as a
   two's complement one, sign bit included */
unsigned active_bits(__int128 v)
{
    unsigned n = 0;
    for (unsigned __int128 u = static_cast<unsigned __int128>(v); u; u >>= 1)
        n++;
    return n;
}

unsigned signed_bits(__int128 v)
{
    return active_bits(~v) + 1;
}

} // namespace

/*
 * [dcl.enum]/1: "An enumeration is a distinct type ([basic.compound]) with
 * named constants.  Its name becomes an enum-name within its scope."
 *
 *     enum-specifier:
 *         enum-head { enumerator-list_opt }
 *         enum-head { enumerator-list , }
 *
 * What analysis has rules for: an enum-head without attributes or a
 * nested-name-specifier, in any scope; the enumeration is defined there
 * once.  The underlying type of an enumeration whose underlying type is
 * not fixed is clang's ([dcl.enum]/7, "implementation-defined"): unsigned
 * int, unsigned long or unsigned long long when no enumerator is negative,
 * else int, long or long long, the first that holds every value, and the
 * promoted type of such an enumeration ([conv.prom]/4) is int when every
 * value fits in it.
 */
analysis_result semantic_analysis::enum_specifier(SyntaxRef n, type_index &type)
{
    type = 0;
    analysis_result r = enum_specifier_rules(n, type);
    /* a definition analysis gave up in (or found ill-formed) leaves an
       enumeration with only some of its enumerators: no underlying type,
       so a use of it is unsupported too ([expr.prim.id.qual]'s "shall be
       followed by the name of an enumerator" would be a false error) */
    if (r != analysis_result::well_formed && type &&
        type_table[type].kind == type_kind::enumeration) {
        entity &x = entity_table[type_table[type].entity];
        if (!x.defined)
            x.underlying = fundamental_type::none;
    }
    return r;
}

analysis_result semantic_analysis::enum_specifier_rules(SyntaxRef n,
                                                        type_index &type)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    auto node = n;
    owned_array<SyntaxElement> head;
    uint32_t i = 0;
    /* [dcl.enum]/3: "An opaque-enum-declaration is either a redeclaration
       of an enumeration in the current scope or a declaration of a new
       enumeration": its head and no enumerator-list */
    bool opaque = node.is(syntax_kind::opaque_enum_declaration);
    if (node.size() && node[0].is(syntax_kind::enum_head)) {
        auto h = node[0];
        for (uint32_t k = 0; k < h.size(); k++)
            head.append(node[0][k]);
        i = 1;
    } else {
        for (; i < node.size() &&
               !node[i].is(preprocessing_op_or_punc::LEFT_BRACE);
             i++)
            head.append(node[i]);
    }
    bool scoped = false;
    uint32_t name = 0;
    token_index name_at = node.first_token();
    node_index base = 0;
    for (SyntaxElement c : head) {
        if (syntax(c).is(syntax_kind::enum_key)) {
            scoped = true;
            continue;
        }
        if (syntax(c).is(syntax_kind::enum_base)) {
            base = c.index();
            continue;
        }
        if (c.is_token() &&
            p.keyword_of(tokens[c.token()]) == standard_keyword::enum_)
            continue;
        if (opaque && syntax(c).is(preprocessing_op_or_punc::SEMICOLON))
            continue;
        if (c.is_token() && tokens[c.token()].kind == token_kind::identifier) {
            name = tokens.identifier(c.token());
            name_at = c.token();
            continue;
        }
        /* an attribute-specifier-seq, a nested-name-specifier */
        return not_supported(n, "dcl.enum");
    }
    if (!opaque &&
        (i >= node.size() || !node[i].is(preprocessing_op_or_punc::LEFT_BRACE)))
        return not_supported(n, "dcl.enum");
    owned_array<SyntaxElement> enumerators;
    for (i++; i < node.size(); i++) {
        SyntaxElement c = node[i];
        if (syntax(c).is(syntax_kind::enumerator_list)) {
            auto list = syntax(c);
            for (uint32_t k = 0; k < list.size(); k++) {
                SyntaxElement item = syntax(c)[k];
                if (!syntax(item).is(preprocessing_op_or_punc::COMMA))
                    enumerators.append(item);
            }
            continue;
        }
        if (syntax(c).is(preprocessing_op_or_punc::COMMA) ||
            syntax(c).is(preprocessing_op_or_punc::RIGHT_BRACE))
            continue;
        enumerators.append(c);
    }

    /* [dcl.enum]/5: "For a scoped enumeration type, the underlying type is
       int if it is not explicitly specified.  In both of these cases, the
       underlying type is said to be fixed." */
    fundamental_type fixed = fundamental_type::none;
    if (scoped)
        fixed = fundamental_type::int_;
    if (base) {
        auto bn = syntax(base);
        if (bn.size() != 2 ||
            !syntax(base)[1].is(syntax_kind::type_specifier_seq))
            return not_supported(n, "dcl.enum");
        specified s;
        analysis_result r =
            decl_specifier_seq(syntax(base)[1].index(), s, true);
        if (r != analysis_result::well_formed)
            return r;
        /* [dcl.enum]/2: "The type-specifier-seq of an enum-base shall name
           an integral type; any cv-qualification is ignored." */
        if (!s.has_type)
            return not_supported(n, "dcl.enum");
        if (!is_integral(s.type)) {
            if (fundamental_of(s.type) == fundamental_type::none &&
                type_table[s.type.unqualified()].kind != type_kind::pointer)
                return not_supported(n, "dcl.enum");
            return ill_formed(base,
                              "the type-specifier-seq of an enum-base shall "
                              "name an integral type",
                              "dcl.enum");
        }
        fixed = fundamental_of(s.type);
    }

    /* the scope the enumeration is declared in: [basic.scope.enum]/1 the
       unscoped enumerators are declared there too */
    entity_index scope = current_scope;
    if (defining_class)
        scope = defining_class;
    entity_index e = 0;
    bool is_fixed = fixed != fundamental_type::none;
    if (name) {
        entity_index prior = entity_table.find(scope, name);
        if (prior && entity_table[prior].kind == entity_kind::enumeration &&
            (opaque || !entity_table[prior].defined)) {
            /* [dcl.enum]/3: "If an enum-head-name contains a
               nested-name-specifier ...  Otherwise, ... the enumeration
               ... shall be a redeclaration ...: the enum-key and the
               underlying type shall be the same" ("An enumeration whose
               underlying type is fixed ... both shall specify the same
               underlying type"; scoped-ness the same) */
            const entity &x = entity_table[prior];
            if (!x.fixed_underlying || !is_fixed || x.scoped != scoped ||
                x.underlying != fixed)
                return ill_formed_at_token(name_at,
                                           "each redeclaration of an "
                                           "enumeration shall have the same "
                                           "enum-key and underlying type",
                                           "dcl.enum");
            entity_table.add_declaration(prior, current_declaration);
            type = x.type.unqualified();
            if (opaque)
                return analysis_result::well_formed;
            e = prior;
        } else if (prior) {
            if (entity_table[prior].kind == entity_kind::enumeration)
                /* [basic.def.odr]/1: "No translation unit shall contain
                   more than one definition of any variable, function, class
                   type, enumeration type, template, default argument for a
                   parameter (for a function in a given scope), or default
                   template argument." */
                return ill_formed_at_token(name_at,
                                           "no translation unit shall "
                                           "contain more than one definition "
                                           "of any enumeration type",
                                           "basic.def.odr");
            return not_supported(n, "basic.scope.declarative");
        }
    }
    if (opaque && !is_fixed)
        /* (the parser requires an unscoped one's enum-base) */
        return not_supported(n, "dcl.enum");
    bool redeclared = e != 0;
    if (!redeclared)
        e = entity_table.make(entity_kind::enumeration, name, scope);
    entity &x = entity_table[e];
    x.scoped = scoped;
    x.fixed_underlying = is_fixed;
    x.access = declaration_access;
    if (is_namespace(scope)) {
        /* [basic.link]/4: a named enumeration of namespace scope has its
           namespace's linkage */
        x.linkage_of_name = linkage::external;
        if (within_unnamed_namespace(scope))
            x.linkage_of_name = linkage::internal;
    } else if (defining_class) {
        /* [basic.link]/5: a member enumeration has the linkage of its
           class's name */
        x.linkage_of_name = entity_table[defining_class].linkage_of_name;
    }
    if (!redeclared) {
        x.type = qualified_type::make(
            type_table.of_entity(type_kind::enumeration, e), cv_none);
        entity_table.add_declaration(e, current_declaration);
        if (name)
            entity_table.bind(scope, name, e);
    }
    type = entity_table[e].type.unqualified();
    /* [dcl.enum]/6: "An enumeration whose underlying type is fixed is an
       incomplete type from its point of declaration
       ([basic.scope.pdecl]) to immediately after its enum-base (if any),
       at which point it becomes a complete type." */
    entity_table[e].underlying = fixed;
    if (opaque) {
        /* [conv.prom]/4: a fixed underlying type promotes as itself */
        entity_table[e].promoted = fixed;
        return analysis_result::well_formed;
    }

    owned_array<entity_index> enumerators_declared;
    /* [dcl.enum]/5: "Following the closing brace of an enum-specifier, each
       enumerator has the type of its enumeration", so a claim placed after
       the declaration about an expression of the enumerator-list would be
       about another type: none is made (the scope claims are placed in is
       no scope while the list is analysed) */
    struct claims_off {
        entity_index &scope;
        entity_index saved;
        ~claims_off()
        {
            scope = saved;
        }
    } no_claims = {claim_scope, claim_scope};
    claim_scope = UINT32_MAX;
    __int128 previous = -1;
    fundamental_type previous_type = fundamental_type::int_;
    if (is_fixed)
        previous_type = fixed;
    bool any = false;
    unsigned positive_bits = 0, negative_bits = 0;
    /* [basic.scope.enum]/1: a scoped enumeration's enumerator-list is its
       own scope, so a later initializer names an earlier enumerator
       unqualified; the enclosing scope again after the list */
    struct scope_restore {
        entity_index &current, saved;
        ~scope_restore()
        {
            current = saved;
        }
    } restore_scope = {current_scope, current_scope};
    if (scoped)
        current_scope = e;
    for (SyntaxElement c : enumerators) {
        SyntaxElement id = c;
        SyntaxElement init;
        bool has_init = false;
        if (syntax(c).is(syntax_kind::enumerator_definition)) {
            auto d = syntax(c);
            if (d.size() != 3)
                return not_supported(n, "dcl.enum");
            id = syntax(c)[0];
            init = syntax(c)[2];
            has_init = true;
        }
        /* [dcl.enum]/2: "The optional attribute-specifier-seq in an
           enumerator appertains to that enumerator": one without effect
           (Apple's availability) is passed over */
        if (syntax(id).is(syntax_kind::enumerator)) {
            auto attrs = syntax(id);
            for (uint32_t i = 1; i < syntax(id).size(); i++)
                if (!class_head_attributes_without_effect(attrs[i]))
                    return not_supported(n, "dcl.enum");
            id = attrs[0];
        }
        if (!id.is_token() || tokens[id.token()].kind != token_kind::identifier)
            return not_supported(n, "dcl.enum");
        token_index at = id.token();
        __int128 value;
        fundamental_type value_type = previous_type;
        if (has_init) {
            expression_meaning m;
            analysis_result r = expression(init, m);
            if (r == analysis_result::well_formed)
                r = operand_value(node_of(init), m);
            if (r != analysis_result::well_formed)
                return r;
            /* "If the expression has unscoped enumeration type, the
               enumerator has the underlying type of that enumeration
               type" */
            entity_index other = enumeration_of(m.type);
            if (other && !entity_table[other].scoped)
                enumeration_as_underlying(m);
            if (!is_integral(m.type)) {
                if (fundamental_of(m.type) == fundamental_type::none &&
                    !is_pointer(m.type))
                    return not_supported(n, "dcl.enum");
                /* [dcl.enum]/5: "the constant-expression shall be an
                   integral constant expression" (or a converted constant
                   expression of the fixed underlying type) */
                return ill_formed_at_token(
                    first_token(init),
                    "the constant-expression shall be an "
                    "integral constant expression",
                    "dcl.enum");
            }
            bool constant = false;
            r = integral_constant_expression(n, m, constant);
            if (r != analysis_result::well_formed)
                return r;
            if (!constant)
                return not_supported(n, "expr.const");
            fundamental_type f = fundamental_of(m.type);
            value = integer_value(b, f, m.value);
            if (is_fixed) {
                /* "the constant-expression in the enumerator-definition
                   shall be a converted constant expression of the
                   underlying type": [expr.const]/10, no narrowing */
                /* bool to an integer type is an integral promotion
                   ([conv.prom]/6); an integer to bool a boolean conversion
                   that clang allows for 0 and 1 (P1401R5, a defect report
                   against C++20): both when the value fits */
                if (!integer_holds(b, fixed, value))
                    return ill_formed_at_token(
                        first_token(init),
                        "the constant-expression in the "
                        "enumerator-definition shall be a "
                        "converted constant expression of the "
                        "underlying type",
                        "dcl.enum");
            } else {
                value_type = f;
            }
        } else {
            /* "If the first enumerator has no initializer, the value of
               the corresponding constant is zero.  An enumerator-definition
               without an initializer gives the enumerator the value
               obtained by increasing the value of the previous enumerator
               by one." */
            value = previous + 1;
            if (!integer_holds(b, value_type, value)) {
                if (is_fixed)
                    return ill_formed_at_token(at,
                                               "an enumerator's value shall "
                                               "be representable in the "
                                               "fixed underlying type",
                                               "dcl.enum");
                /* "in which case the type is an unspecified integral type
                   sufficient to contain the incremented value.  If no such
                   type exists, the program is ill-formed." */
                static const fundamental_type wider[] = {
                    fundamental_type::int_,
                    fundamental_type::unsigned_int,
                    fundamental_type::long_int,
                    fundamental_type::unsigned_long_int,
                    fundamental_type::long_long_int,
                    fundamental_type::unsigned_long_long_int};
                value_type = fundamental_type::none;
                for (fundamental_type w : wider)
                    if (integer_holds(b, w, value)) {
                        value_type = w;
                        break;
                    }
                if (value_type == fundamental_type::none)
                    return not_supported(n, "dcl.enum");
            }
        }
        if (value < 0) {
            unsigned bits = signed_bits(value);
            if (bits > negative_bits)
                negative_bits = bits;
        } else if (active_bits(value) > positive_bits) {
            positive_bits = active_bits(value);
        }
        any = true;
        previous = value;
        previous_type = value_type;
        uint32_t enumerator_name = tokens.identifier(at);
        entity_index conflict = entity_table.find(scope, enumerator_name);
        if (scoped)
            conflict = entity_table.find(e, enumerator_name);
        if (conflict) {
            if (entity_table[conflict].kind == entity_kind::none)
                return not_supported(n, "basic.scope.declarative");
            /* [basic.scope.declarative]/4: "Given a set of declarations
               in a single declarative region, each of which specifies the
               same unqualified name, they shall all refer to the same
               entity" */
            if (entity_table[conflict].kind == entity_kind::enumerator ||
                entity_table[conflict].kind == entity_kind::variable ||
                entity_table[conflict].kind == entity_kind::function)
                return ill_formed_at_token(at,
                                           "the enumerator's name is "
                                           "declared in the scope already",
                                           "basic.scope.declarative");
            return not_supported(n, "basic.scope.declarative");
        }
        entity_index enumerator_scope = scope;
        if (scoped)
            enumerator_scope = e;
        entity_index en = entity_table.make(entity_kind::enumerator,
                                            enumerator_name, enumerator_scope);
        entity &y = entity_table[en];
        /* "If the underlying type is fixed, the type of each enumerator
           prior to the closing brace is the underlying type" */
        y.type =
            qualified_type::make(type_table.fundamental(value_type), cv_none);
        y.value =
            integer_wrap(b, value_type, static_cast<unsigned __int128>(value));
        y.usable = constness::constant;
        y.defined = true;
        y.access = declaration_access;
        entity_table.add_declaration(en, current_declaration);
        /* [basic.scope.pdecl]/5: "The point of declaration for an
           enumerator is immediately after its enumerator-definition."
           [expr.prim.id.qual]/4: an unscoped enumerator is also named by
           a qualified-id of its enumeration */
        if (scoped) {
            entity_table.bind(e, enumerator_name, en);
        } else {
            entity_table.bind(scope, enumerator_name, en);
            entity_table.bind(e, enumerator_name, en);
        }
        enumerators_declared.append(en);
    }
    if (!any)
        positive_bits = 1;
    fundamental_type underlying = fixed;
    fundamental_type promoted = fixed;
    if (!is_fixed) {
        unsigned int_width = fundamental_type_width(b, fundamental_type::int_);
        unsigned long_width =
            fundamental_type_width(b, fundamental_type::long_int);
        unsigned long_long_width =
            fundamental_type_width(b, fundamental_type::long_long_int);
        if (negative_bits) {
            if (negative_bits <= int_width && positive_bits < int_width)
                underlying = fundamental_type::int_;
            else if (negative_bits <= long_width && positive_bits < long_width)
                underlying = fundamental_type::long_int;
            else if (negative_bits <= long_long_width &&
                     positive_bits < long_long_width)
                underlying = fundamental_type::long_long_int;
            promoted = underlying;
        } else if (positive_bits <= int_width) {
            underlying = fundamental_type::unsigned_int;
            promoted = fundamental_type::int_;
            if (positive_bits == int_width)
                promoted = underlying;
        } else if (positive_bits <= long_width) {
            underlying = fundamental_type::unsigned_long_int;
            promoted = fundamental_type::long_int;
            if (positive_bits == long_width)
                promoted = underlying;
        } else if (positive_bits <= long_long_width) {
            underlying = fundamental_type::unsigned_long_long_int;
            promoted = fundamental_type::long_long_int;
            if (positive_bits == long_long_width)
                promoted = underlying;
        }
        if (underlying == fundamental_type::none)
            /* [dcl.enum]/7: "If no integral type can represent all the
               enumerator values, the enumeration is ill-formed." (clang's
               extension: __int128) */
            return not_supported(n, "dcl.enum");
    }
    /* [dcl.enum]/5: "Following the closing brace of an enum-specifier,
       each enumerator has the type of its enumeration." */
    entity &z = entity_table[e];
    z.underlying = underlying;
    z.promoted = promoted;
    z.defined = true;
    for (entity_index en : enumerators_declared) {
        entity &y = entity_table[en];
        __int128 v = integer_value(b, fundamental_of(y.type), y.value);
        y.value =
            integer_wrap(b, underlying, static_cast<unsigned __int128>(v));
        y.type = z.type;
    }
    return analysis_result::well_formed;
}

/*
 * [dcl.type.elab]:
 *
 *     elaborated-enum-specifier:
 *         enum nested-name-specifier_opt identifier
 *
 * [basic.lookup.elab]/2: the identifier is looked up; an enumeration found
 * is the type.  [dcl.type.elab]/3: "The class-key or enum keyword present
 * in the elaborated-type-specifier shall agree in kind with the
 * declaration to which the name in the elaborated-type-specifier refers":
 * an enumeration not declared yet is not declared by it.
 */
analysis_result
semantic_analysis::elaborated_enum_specifier(ElaboratedEnumSpecifier n,
                                             type_index &type)
{
    auto node = n;
    if (node.size() != 2 || !node[1].is_identifier())
        return not_supported(n, "dcl.type.elab");
    entity_index e = lookup(tokens.identifier(node[1].token()));
    if (!e || entity_table[e].kind != entity_kind::enumeration)
        return not_supported(n, "basic.lookup.elab");
    type = entity_table[e].type.unqualified();
    return analysis_result::well_formed;
}

/*
 * [expr.prim.id.unqual]/2: an enumerator: "The expression is an lvalue if
 * the entity is a function, variable, structured binding, data member, or
 * template parameter object and a prvalue otherwise"; its value is a
 * constant ([dcl.enum]/2: "The identifiers in an enumerator-list are
 * declared as constants, and can appear wherever constants are
 * required.").
 */
analysis_result semantic_analysis::enumerator_value(token_index t,
                                                    entity_index e,
                                                    expression_meaning &m)
{
    const entity &x = entity_table[e];
    if (x.enclosing &&
        entity_table[x.enclosing].kind == entity_kind::class_type &&
        !accessible(e))
        return ill_formed_at_token(t,
                                   "a private or protected member's name can "
                                   "be used only by members and friends of "
                                   "the class in which it is declared",
                                   "class.access");
    m.category = value_category::prvalue;
    m.type = x.type;
    m.evaluation = constness::constant;
    m.value = x.value;
    m.node = expression_node(semantic_kind::constant, m, t);
    return analysis_result::well_formed;
}

} // namespace cplusplus
