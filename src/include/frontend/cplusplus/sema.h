/*
 * sema.h: semantic analysis of a translation unit's syntax tree
 * (doc/PLAN-cxx-sema.md): [lex.phases]/1.7 "syntactically and semantically
 * analyzed".
 *
 * Analysis does not annotate the syntax tree.  It builds the semantic
 * program: the types (one record per distinct type), the entities with
 * their declarations, and from step S1 a typed tree per initializer and
 * function body.  Like the syntax tree these are arrays the translation
 * unit owns, referred to by 32-bit indexes, with nothing global.
 *
 * A construct whose rules are not written yet is not guessed at: its
 * declaration's result is `unsupported`, with the subclause that owns the
 * rules, and the declaration contributes nothing.  The count over a corpus
 * is the work list, and is zero when analysis is complete.
 */
#ifndef VCC_CPLUSPLUS_SEMA_H
#define VCC_CPLUSPLUS_SEMA_H

#include "frontend/cplusplus/parse.h"

namespace cplusplus
{

/* ---- the syntax tree, as analysis reads it -------------------------------- */

inline bool syntax_is(const syntax_tree &tree, syntax_child c, syntax_kind kind)
{
    return !c.is_token() && tree[c.index()].kind == kind;
}

inline bool syntax_is_attribute(const syntax_tree &tree, syntax_child c)
{
    return syntax_is(tree, c, syntax_kind::attribute_specifier) ||
           syntax_is(tree, c, syntax_kind::attribute_specifier_seq) ||
           syntax_is(tree, c, syntax_kind::alignment_specifier);
}

inline bool syntax_is_punctuator(token_stream &tokens, syntax_child c,
                                 preprocessing_op_or_punc op)
{
    if (!c.is_token())
        return false;
    const token &t = tokens[c.index()];
    return t.kind == token_kind::operator_or_punctuator &&
           preprocessing_op_or_punc_primary(t.op_or_punc) == op;
}

/* ---- types -------------------------------------------------------------- */

/* a type of the translation unit's type table; 0 is no type */
using type_index = uint32_t;

/* [basic.type.qualifier]/1: "a const-qualified version, a
   volatile-qualified version, and a const-volatile-qualified version" */
enum cv_qualifier : uint32_t {
    cv_none = 0,
    cv_const = 1,
    cv_volatile = 2,
};

/*
 * A possibly cv-qualified type: the cv-unqualified type's index and the
 * qualifiers.  [basic.type.qualifier]/1: "The cv-qualified or
 * cv-unqualified versions of a type are distinct types", but they are one
 * record here, so removing or comparing qualifiers reads no table.
 */
struct qualified_type {
    uint32_t bits = 0;
    static qualified_type make(type_index type, uint32_t cv)
    {
        return {type << 2 | cv};
    }
    type_index unqualified() const
    {
        return bits >> 2;
    }
    uint32_t cv() const
    {
        return bits & 3;
    }
    bool operator==(const qualified_type &other) const
    {
        return bits == other.bits;
    }
};

/* [basic.fundamental], and the ways of [basic.compound]/1 to construct a
   compound type */
enum class type_kind : uint8_t {
    none,
    fundamental,
    array,            /* [dcl.array] */
    function,         /* [dcl.fct] */
    pointer,          /* [dcl.ptr] */
    lvalue_reference, /* [dcl.ref] */
    rvalue_reference,
    class_type,        /* [class], unions included; the entity says which */
    enumeration,       /* [dcl.enum] */
    pointer_to_member, /* [dcl.mptr] */
};

struct type {
    type_kind kind = type_kind::none;
    /* kind fundamental: which */
    fundamental_type fundamental = fundamental_type::none;
    /* function: [dcl.fct]/2 "the parameter-declaration-clause terminates
       with an ellipsis" */
    bool variadic = false;
    /* array: [dcl.array]/1 whether the constant-expression is present (if
       not, an "array of unknown bound of T") */
    bool bound_known = false;
    /* array: the element type; function: the return type; pointer and
       reference: the type pointed or referred to; pointer to member: the
       member's type */
    qualified_type operand;
    /* class, enumeration: the entity; pointer to member: the class's */
    uint32_t entity = 0;
    /* array: the bound N */
    uint64_t bound = 0;
    /* function: the parameter types are parameters[first_parameter ..
       first_parameter + parameter_count) */
    uint32_t first_parameter = 0, parameter_count = 0;
};

/*
 * The types of a translation unit.  A type is made once: making it again
 * gives the same index, so two types are the same type exactly when their
 * indexes are equal.
 */
class types
{
  public:
    types();
    types(const types &) = delete;
    types &operator=(const types &) = delete;

    type_index fundamental(fundamental_type which);
    type_index pointer_to(qualified_type pointee);
    type_index lvalue_reference_to(qualified_type referred);
    type_index rvalue_reference_to(qualified_type referred);
    type_index array_of(qualified_type element, uint64_t bound);
    type_index array_of_unknown_bound_of(qualified_type element);
    type_index function(qualified_type result, const qualified_type *parameters,
                        size_t parameter_count, bool variadic);
    type_index of_entity(type_kind kind, uint32_t entity);
    type_index pointer_to_member(uint32_t class_entity, qualified_type member);

    const type &operator[](type_index t) const
    {
        return records[t];
    }
    const qualified_type *parameters(type_index t) const
    {
        return parameter_lists.data() + records[t].first_parameter;
    }
    /* the number of types, the unused type 0 included */
    size_t size() const
    {
        return records.size();
    }

  private:
    /* the type equal to t, or t added */
    type_index intern(const type &t, const qualified_type *parameters);
    uint32_t hash(const type &t, const qualified_type *parameters) const;
    bool same(const type &a, const qualified_type *a_parameters,
              type_index b) const;
    void grow();

    owned_array<type> records;
    owned_array<qualified_type> parameter_lists;
    /* open addressing: a type's index, 0 for an empty slot */
    owned_array<type_index> slots;
};

/* ---- entities ----------------------------------------------------------- */

/* an entity of the translation unit; 0 is no entity */
using entity_index = uint32_t;

/* [basic.pre]/3: "An entity is a value, object, reference, structured
   binding, function, enumerator, type, class member, bit-field, template,
   template specialization, namespace, or pack."  The kinds a declaration
   introduces; a class member or a bit-field is a variable or a function
   whose scope is a class. */
enum class entity_kind : uint8_t {
    none,
    variable, /* an object or a reference: [basic.pre]/6 */
    structured_binding,
    function,
    enumerator,
    class_type,
    enumeration,
    typedef_name, /* [dcl.typedef]: no entity of its own in the standard's
                     list, but a declared name with a type */
    template_entity,
    template_specialization,
    namespace_entity,
    pack,
};

/* [basic.link]/2 */
enum class linkage : uint8_t {
    none,
    internal,
    external,
};

struct entity {
    entity_kind kind = entity_kind::none;
    linkage linkage_of_name = linkage::none;
    /* [basic.def]/2: a declaration of it was a definition */
    bool defined = false;
    /* the preprocessor's identifier number, 0 for an unnamed entity */
    uint32_t name = 0;
    /* the entity whose scope the name is declared in (a namespace, a
       class, a function); 0 at global namespace scope */
    entity_index enclosing = 0;
    qualified_type type;
    /* [basic.link]: its declarations, in order, are a chain through
       `declarations`: the first and the last */
    uint32_t first_declaration = 0, last_declaration = 0;
};

/* one declaration of an entity: the syntax node that declares it */
struct entity_declaration {
    node_index node = 0;
    /* the entity's next declaration, 0 at the last */
    uint32_t next = 0;
};

class entities
{
  public:
    entities();
    entities(const entities &) = delete;
    entities &operator=(const entities &) = delete;

    entity_index make(entity_kind kind, uint32_t name, entity_index enclosing);
    /* the entity a name declared in a scope refers to, 0 if none: what a
       declaration in the scope finds of an earlier one
       ([basic.scope.declarative]/4) */
    entity_index find(entity_index scope, uint32_t name) const;
    void bind(entity_index scope, uint32_t name, entity_index e);
    /* another declaration of the entity ([basic.link]/1: "A name ... can
       be introduced by declarations in more than one place) */
    void add_declaration(entity_index e, node_index node);

    entity &operator[](entity_index e)
    {
        return records[e];
    }
    const entity &operator[](entity_index e) const
    {
        return records[e];
    }
    const entity_declaration &declaration(uint32_t d) const
    {
        return declarations[d];
    }
    size_t size() const
    {
        return records.size();
    }

  private:
    owned_array<entity> records;
    owned_array<entity_declaration> declarations;
    /* the names bound in scopes: open addressing over (scope, name), the
       entity's index, 0 for an empty slot */
    owned_array<entity_index> slots;
    void grow();
};

/* ---- analysis ----------------------------------------------------------- */

/* what analysis concluded about one declaration of the translation unit */
enum class analysis_result : uint8_t {
    /* its rules are written, and it meets them */
    well_formed,
    /* [intro.compliance]/2.2: it "contains a violation of [a] diagnosable
       rule": a diagnostic was issued */
    ill_formed,
    /* rules it needs are not written yet: nothing is concluded */
    unsupported,
    /* the parser's: it has an erroneous node, or one kept as tokens */
    not_parsed,
};
const char *analysis_result_name(analysis_result result);

/* a construct analysis has no rules for yet */
struct unsupported_construct {
    node_index node = 0;
    /* the subclause whose rules are missing, without brackets */
    const char *subclause = "";
};

struct analysed_declaration {
    node_index node = 0;
    analysis_result result = analysis_result::unsupported;
    /* result unsupported: the first reason, an index into
       semantic_analysis::unsupported */
    uint32_t first_unsupported = 0;
};

/* a name a declaration declared, with the type analysis gave it: what
   cxxsema -declaration-claims has clang check */
struct declared_name {
    node_index declaration = 0;
    token_index id = 0;
    entity_kind kind = entity_kind::none;
    qualified_type type;
};

class semantic_analysis
{
  public:
    semantic_analysis(const syntax_tree &tree, token_stream &tokens,
                      const parser &p, diagnostic_consumer &diagnostics);
    semantic_analysis(const semantic_analysis &) = delete;
    semantic_analysis &operator=(const semantic_analysis &) = delete;

    /* [basic.link]: every declaration of the translation unit, in order */
    void translation_unit();
    /* one declaration of a declaration-seq */
    analysis_result declaration(node_index n);

    types type_table;
    entities entity_table;
    owned_array<analysed_declaration> declarations;
    owned_array<unsupported_construct> unsupported;
    owned_array<declared_name> declared;

  private:
    /* the node needs rules that are not written: those of the subclause
       that defines its nonterminal, or of the one named */
    analysis_result not_supported(node_index n);
    analysis_result not_supported(node_index n, const char *subclause);
    /* whether the parser gave up on a part of the declaration */
    bool parsed_completely(node_index n) const;

    /* the node at, or its first token, violates a rule: the diagnostic,
       naming the subclause */
    analysis_result ill_formed(node_index at, const char *rule,
                               const char *subclause);
    analysis_result ill_formed_at_token(token_index at, const char *rule,
                                        const char *subclause);

    /* a rule clang enforces only when asked: ill-formed when the
       diagnostic is an error on this command line, else well-formed with
       the warning (or nothing) */
    analysis_result extension_used(diagnostic_id id, token_index at,
                                   const char *argument);

    /* [dcl.pre] */
    analysis_result empty_declaration(node_index n);
    analysis_result simple_declaration(node_index n);
    analysis_result function_definition(node_index n);
    analysis_result init_declarators(node_index declaration,
                                     node_index specifiers, bool definition);

    /* [dcl.spec]: what a decl-specifier-seq says */
    struct specified {
        qualified_type type;
        /* [dcl.type]/3: a defining-type-specifier that is not a
           cv-qualifier was there */
        bool has_type = false;
        bool typedef_ = false, static_ = false, extern_ = false,
             inline_ = false;
    };
    analysis_result decl_specifier_seq(node_index seq, specified &s,
                                       bool parameter);
    /* [dcl.type.simple] Table 14: the type of the simple-type-specifier
       keywords of a decl-specifier-seq */
    struct simple_type_keywords {
        unsigned signed_ = 0, unsigned_ = 0, short_ = 0, long_ = 0, char_ = 0,
                 int_ = 0, double_ = 0, alone = 0;
        fundamental_type alone_type = fundamental_type::none;
        token_index first = 0;
    };
    analysis_result simple_type(node_index seq, const simple_type_keywords &k,
                                type_index &type);

    /* [dcl.meaning]: the type a declarator gives its declarator-id */
    struct declarator_meaning {
        bool has_id = false;
        token_index id = 0;
        /* a reference the declarator made, rather than a typedef-name's
           ([dcl.ref]/5 and /6) */
        bool reference_made = false;
    };
    analysis_result declarator(node_index declaration, syntax_child d,
                               qualified_type &type, declarator_meaning &m);
    token_index declarator_id(syntax_child d) const;
    void hold_names(node_index declaration);
    analysis_result pointer_operator(node_index op, qualified_type &type,
                                     declarator_meaning &m);
    analysis_result array(node_index at, const syntax_child *bound,
                          qualified_type &type);
    analysis_result reference(node_index at, bool rvalue, qualified_type &type,
                              declarator_meaning &m);
    /* [dcl.fct] */
    analysis_result function(node_index parameters, qualified_type &type);
    analysis_result parameter_declaration(node_index n, qualified_type &type,
                                          bool &unnamed);
    /* cv-qualifiers added to a type: to an array's elements
       ([basic.type.qualifier]/3), none to a reference or a function type
       ([dcl.ref]/1, [dcl.fct]/7) */
    qualified_type add_cv(qualified_type t, uint32_t cv);

    /* [basic.link], [basic.scope.declarative]: the name of a declarator
       declared at global namespace scope, or redeclared */
    analysis_result declare(node_index declaration, token_index id,
                            entity_kind kind, qualified_type type,
                            const specified &s, bool definition);

    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
    diagnostic_consumer &diagnostics;
};

} // namespace cplusplus

#endif
