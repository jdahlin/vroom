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
    /* not an entity: a block scope or a function parameter scope
       ([basic.scope.block], [basic.scope.param]), the scope the names
       declared in it are bound in */
    block_scope,
};

/* [basic.link]/2 */
enum class linkage : uint8_t {
    none,
    internal,
    external,
};

/* whether an evaluation is a constant expression ([expr.const]/11) with a
   value analysis knows, is not one, or analysis cannot tell */
enum class constness : uint8_t { constant, not_constant, unknown };

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
    /* [expr.const]/4: a variable usable in constant expressions: whether
       it is (constant, with its value as expression_meaning keeps one),
       is not, or analysis could not tell (unknown) */
    constness usable = constness::not_constant;
    uint64_t value = 0;
    /* [dcl.constexpr]/1 */
    bool constexpr_ = false;
    /* [dcl.inline]: an inline function or variable */
    bool inline_ = false;
    /* a variable: [basic.stc.auto]/1, a local variable not declared static
       (or a parameter) */
    bool automatic = false;
    /* a block_scope: the scope whose names it shall not redeclare (the
       function parameter scope for the outermost block of a function
       definition, the scope of a statement's condition for its
       substatements' outermost blocks), and whether that is the parameter
       scope */
    entity_index conflicts = 0;
    bool parameter_scope = false;
    /* [basic.link]: its declarations, in order, are a chain through
       `declarations`: the first and the last */
    uint32_t first_declaration = 0, last_declaration = 0;
    /* the semantic tree of its definition: a function's
       semantic_kind::function_body, a variable's initializer (0: none) */
    uint32_t definition = 0;
    /* an array of unknown bound whose initializer would give the bound
       ([dcl.array]/7) and was not analysed: its type is not known */
    bool bound_unknown = false;
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

/* the conclusion about a construct of two parts: ill-formed if either is,
   else unsupported if either is */
inline analysis_result worse(analysis_result a, analysis_result b)
{
    if (a == analysis_result::ill_formed || b == analysis_result::ill_formed)
        return analysis_result::ill_formed;
    if (a == analysis_result::not_parsed || b == analysis_result::not_parsed)
        return analysis_result::not_parsed;
    if (a == analysis_result::unsupported || b == analysis_result::unsupported)
        return analysis_result::unsupported;
    return analysis_result::well_formed;
}

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
    /* the declaration of the translation unit it is in
       (analysed_declaration::node), and the entity it declared */
    node_index outermost = 0;
    entity_index entity = 0;
    token_index id = 0;
    entity_kind kind = entity_kind::none;
    qualified_type type;
    /* whether a claim can be placed just after the declaration: not for a
       declaration in a condition or init-statement */
    bool claimable = true;
};

/* ---- values of constant expressions (expr/expr.const.cpp) ---- */

/* [basic.fundamental]/2: the integer types; [basic.fundamental]/12: the
   floating-point types (_BitInt and _Float16 are neither here: analysis
   has no rules for them yet) */
bool fundamental_type_is_integral(fundamental_type t);
bool fundamental_type_is_floating(fundamental_type t);
/* [conv.integral]/3: "the unique value of the destination type that is
   congruent to the source integer modulo 2^N": as an expression_meaning
   keeps it, sign-extended for a signed type ([conv.bool] for bool) */
uint64_t integer_wrap(const implementation_defined_behavior &behavior,
                      fundamental_type t, unsigned __int128 v);
/* whether the integer type t can represent v */
bool integer_holds(const implementation_defined_behavior &behavior,
                   fundamental_type t, __int128 v);
/* whether the value v of the integer type t (wrapped to it) is what
   integer_wrap keeps: a 128-bit type's value only when it fits in 64 bits
   (an operation's result otherwise has no value analysis knows) */
bool integer_kept_exactly(const implementation_defined_behavior &behavior,
                          fundamental_type t, unsigned __int128 v);
/* the value an expression_meaning keeps for the integer type t */
__int128 integer_value(const implementation_defined_behavior &behavior,
                       fundamental_type t, uint64_t kept);
/* the constness of an operation on operands of the given constnesses,
   before it is evaluated */
constness combined_constness(constness a, constness b);
/* a floating-point value as an expression_meaning keeps it */
double floating_value(uint64_t kept);
uint64_t floating_kept(double v);

/* the cv-qualifiers of a type, an array's those of its elements
   ([basic.type.qualifier]/3) */
uint32_t cv_of(const types &table, qualified_type t);

/* [basic.lval]/1: "Every expression belongs to exactly one of the
   fundamental classifications in this taxonomy: lvalue, xvalue, or
   prvalue." */
enum class value_category : uint8_t { prvalue, lvalue, xvalue };

/* what analysis found an expression to be ([expr.pre]/1: "An expression
   is a sequence of operators and operands that specifies a computation") */
struct expression_meaning {
    /* [expr.type]/1: never a reference type */
    qualified_type type;
    value_category category = value_category::prvalue;
    /* a prvalue: whether it is a constant expression and its value: an
       integral value sign- or zero-extended to 64 bits, or a float or
       double value as binary64 bits.  A glvalue: whether evaluating it
       (not reading the object) is constant */
    constness evaluation = constness::unknown;
    uint64_t value = 0;
    /* [conv.ptr]/1: an integer literal with value zero */
    bool zero_literal = false;
    /* a string-literal, perhaps parenthesized ([dcl.init.string]) */
    token_index string_literal = 0;
    /* an id-expression, perhaps parenthesized: the variable or function */
    entity_index entity = 0;
    /* a floating-point operand or a comma operator inside: not an integral
       constant expression before C++11 (C++03 [expr.const]/1) */
    bool beyond_integral_constant = false;
    /* the expression in the semantic tree, with the conversions applied to
       it so far: 0 while it has none */
    uint32_t node = 0;
};

/* ---- the semantic tree (sema/semantic_tree.cpp) ---- */

/*
 * The operations the standard describes, typed, with every implicit step
 * explicit (doc/PLAN-cxx-sema.md 3.2): what lowering reads.  An expression
 * node has the type and value category analysis gave the expression; a
 * statement node none.
 */
enum class semantic_kind : uint8_t {
    none,
    /* ---- expressions ---- */
    /* a prvalue whose value analysis knows (a literal, sizeof, a folded
       operation): value, as expression_meaning keeps it */
    constant,
    /* at: the string-literal; an lvalue of array type */
    string_literal,
    /* a: the variable or function named; a reference variable names the
       object or function it refers to */
    entity,
    /* op: a conversion_kind; a: the operand */
    conversion,
    /* op: the operator (+ - ~ !); a */
    unary,
    /* &a, *a */
    address,
    indirection,
    /* op: an increment_kind; a: the lvalue */
    increment,
    /* op: the operator; a, b: the converted operands (pointer arithmetic
       too) */
    binary,
    logical_and,
    logical_or,
    /* op: = or the compound operator; a: the lvalue, b: the converted
       right operand; a compound assignment's computation type is
       computation */
    assignment,
    /* a ? b : c; a glvalue when b and c are */
    conditional,
    /* a, b */
    comma,
    /* a: the function lvalue or pointer prvalue; the arguments, each
       initialized as its parameter, are list[first .. first + count) */
    call,
    /* a: the pointer prvalue, b: the integer prvalue (E1[E2] with the
       operands in that order) */
    subscript,
    /* ---- initializers ---- */
    /* value- or zero-initialization: a scalar's 0, an array's all zero */
    zero,
    /* an array's elements, each initialized, in list[first .. first +
       count); the rest zero */
    initializer_list,
    /* a: the glvalue a reference binds to; a prvalue: a temporary
       materialized from it ([conv.rval]) */
    bind_reference,
    /* ---- statements ---- */
    /* the statements in list[first .. first + count) */
    block,
    /* a: the expression */
    expression_statement,
    /* a: the variable declared, b: its initializer (0: none) */
    declaration_statement,
    /* a declaration of several variables: their declaration_statements in
       list[first .. first + count), in the enclosing block's scope */
    declarations,
    /* a: the condition (bool), b: the substatement, c: the else (0: none),
       d: the init-statement (0: none) */
    if_statement,
    /* a: the condition, b: the substatement */
    while_statement,
    /* a: the substatement, b: the condition */
    do_statement,
    /* a: the init-statement (0: none), b: the condition (0: none), c: the
       expression (0: none), d: the substatement */
    for_statement,
    /* a: the condition (promoted), b: the substatement, d: the
       init-statement */
    switch_statement,
    /* value: the case's converted value; a: the statement */
    case_label,
    /* a: the statement */
    default_label,
    /* value: the identifier's number; a: the statement */
    identifier_label,
    /* value: the identifier's number */
    goto_statement,
    break_statement,
    continue_statement,
    /* a: the operand, initialized as the result (0: none) */
    return_statement,
    /* ; */
    null_statement,
    /* a condition that declares: a: the declaration_statement, b: its
       variable's value converted as the condition ([stmt.pre]/6, /7) */
    condition_declaration,
    /* a function definition: a: the outermost block; the parameters'
       entities (0 for an unnamed one) are list[first .. first + count) */
    function_body,
};

const char *semantic_kind_name(semantic_kind kind);

/* [conv]: the standard conversions, and the explicit ones of [expr.cast]
   analysis has */
enum class conversion_kind : uint8_t {
    lvalue_to_rvalue,    /* [conv.lval] */
    array_to_pointer,    /* [conv.array] */
    function_to_pointer, /* [conv.func] */
    integral,            /* [conv.integral], [conv.prom] */
    floating,            /* [conv.double], [conv.fpprom] */
    floating_integral,   /* [conv.fpint] */
    boolean,             /* [conv.bool] */
    null_pointer, /* [conv.ptr]/1: a null pointer constant to a pointer */
    pointer,      /* [conv.ptr]/2, [conv.qual]: the same address */
    to_void,      /* [expr.static.cast]/6 */
    /* the explicit conversions of a cast ([expr.cast]/4) that are not
       implicit ones: */
    qualification_cast,  /* [expr.const.cast]/3: the same address */
    pointer_cast,        /* [expr.static.cast]/13, [expr.reinterpret.cast]/6,
                            /7, /8: the same address, another type */
    pointer_to_integral, /* [expr.reinterpret.cast]/4 */
    integral_to_pointer, /* [expr.reinterpret.cast]/5 */
};

enum class increment_kind : uint8_t {
    pre_increment,
    pre_decrement,
    post_increment,
    post_decrement,
};

struct semantic_node {
    semantic_kind kind = semantic_kind::none;
    uint8_t op = 0;
    value_category category = value_category::prvalue;
    constness evaluation = constness::unknown;
    qualified_type type;
    /* a compound assignment's computation type */
    qualified_type computation;
    /* the token the node is reported at */
    token_index at = 0;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    /* a list's first index and its count */
    uint32_t first = 0, count = 0;
    uint64_t value = 0;
};

class semantic_tree
{
  public:
    semantic_tree();
    semantic_tree(const semantic_tree &) = delete;
    semantic_tree &operator=(const semantic_tree &) = delete;

    uint32_t add(const semantic_node &n);
    /* a list of node or entity indexes: its first index */
    uint32_t list(const uint32_t *items, size_t count);

    const semantic_node &operator[](uint32_t n) const
    {
        return nodes[n];
    }
    semantic_node &operator[](uint32_t n)
    {
        return nodes[n];
    }
    const uint32_t *items(uint32_t first) const
    {
        return lists.data() + first;
    }
    size_t size() const
    {
        return nodes.size();
    }

  private:
    owned_array<semantic_node> nodes;
    owned_array<uint32_t> lists;
};

/* one expression analysis gave a type: what cxxsema -type-claims has clang
   check */
struct analysed_expression {
    syntax_child at;
    node_index declaration = 0;
    expression_meaning meaning;
    /* where a claim about it can be placed: just after this token (the
       statement of a block it is in), or after the declaration if 0; not
       at all if placeable is false (a name of a condition would be out of
       scope there) */
    token_index place = 0;
    bool placeable = true;
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
    owned_array<analysed_expression> expressions;
    /* the semantic tree of every definition analysed */
    semantic_tree semantics;
    /* the entities a declaration of declarations defined with a tree
       (entity::definition): functions, and variables of namespace scope
       with an initializer */
    struct definition_record {
        node_index declaration = 0;
        entity_index entity = 0;
    };
    owned_array<definition_record> definitions;
    /* the syntax tree analysed */
    const syntax_tree &syntax() const
    {
        return tree;
    }

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
             inline_ = false, constexpr_ = false;
    };
    analysis_result decl_specifier_seq(node_index seq, specified &s,
                                       bool parameter);
    /* [dcl.type.simple] Table 14: the type of the simple-type-specifier
       keywords of a decl-specifier-seq */
    struct simple_type_keywords {
        unsigned signed_ = 0, unsigned_ = 0, short_ = 0, long_ = 0, char_ = 0,
                 int_ = 0, double_ = 0, alone = 0, int128 = 0;
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
    bool declared_by(entity_index e, node_index node) const;
    analysis_result declare_local(node_index declaration, token_index id,
                                  entity_kind kind, qualified_type type,
                                  const specified &s,
                                  entity_index &declared_entity);
    /* [basic.lookup.unqual]/1: the entity an unqualified name finds from
       the current scope out, 0 if none */
    entity_index lookup(uint32_t name) const;
    /* a block scope in the current one, made current */
    entity_index open_scope(entity_index conflicts, bool parameters);
    analysis_result pointer_operator(node_index op, qualified_type &type,
                                     declarator_meaning &m);
    analysis_result array(node_index at, const syntax_child *bound,
                          qualified_type &type);
    analysis_result reference(node_index at, bool rvalue, qualified_type &type,
                              declarator_meaning &m);
    /* [dcl.fct] */
    analysis_result function(node_index parameters, qualified_type &type);
    /* the parameters of the function declarator analysed last: a function
       definition's, with the types they have in its body ([dcl.fct]/5's
       adjustment, top-level cv-qualifiers kept) */
    struct parameter_meaning {
        token_index id = 0;
        qualified_type type;
    };
    owned_array<parameter_meaning> function_parameters;
    analysis_result parameter_declaration(node_index n, qualified_type &type,
                                          bool &unnamed, token_index &id);
    /* cv-qualifiers added to a type: to an array's elements
       ([basic.type.qualifier]/3), none to a reference or a function type
       ([dcl.ref]/1, [dcl.fct]/7) */
    qualified_type add_cv(qualified_type t, uint32_t cv);

    analysis_result init_declarator(node_index declaration, syntax_child item,
                                    const specified &s, bool definition);

    /* [basic.link], [basic.scope.declarative]: the name of a declarator
       declared at global namespace scope, or redeclared; the entity */
    analysis_result declare(node_index declaration, token_index id,
                            entity_kind kind, qualified_type type,
                            const specified &s, bool definition,
                            bool initialized, entity_index &declared_entity);

    /* ---- the semantic tree (sema/semantic_tree.cpp) ---- */
    /* a node for the expression m is now */
    uint32_t expression_node(semantic_kind kind, const expression_meaning &m,
                             token_index at, uint32_t a = 0, uint32_t b = 0,
                             uint8_t op = 0);
    /* m's node, converted to what m now is */
    void converted(expression_meaning &m, conversion_kind kind);
    uint32_t statement_node(semantic_kind kind, token_index at, uint32_t a = 0,
                            uint32_t b = 0, uint32_t c = 0, uint32_t d = 0);
    token_index first_token(syntax_child c) const;

    /* ---- expressions ([expr]) ---- */
    /* [expr.pre]: an expression's type, value category and value; the
       expression is recorded in expressions */
    analysis_result expression(syntax_child e, expression_meaning &m);
    analysis_result expression_kind(syntax_child e, expression_meaning &m);
    /* [expr.prim.literal], [lex.literal] */
    analysis_result literal(token_index t, expression_meaning &m);
    /* [expr.prim.id.unqual] */
    analysis_result id_expression(token_index t, expression_meaning &m);
    /* [expr.prim.paren] */
    analysis_result parenthesized(node_index n, expression_meaning &m);
    /* [expr.unary] */
    analysis_result unary(node_index n, expression_meaning &m);
    analysis_result size_of(node_index n, bool alignment,
                            expression_meaning &m);
    analysis_result increment(node_index n, syntax_child operand,
                              bool decrement, bool postfix,
                              expression_meaning &m);
    /* [expr.post] */
    analysis_result postfix(node_index n, expression_meaning &m);
    /* [expr.cast], [expr.type.conv] */
    analysis_result cast(node_index n, expression_meaning &m);
    /* explicit_conversion's pointer to pointer conversions */
    analysis_result pointer_conversion(node_index at, qualified_type to,
                                       expression_meaning &m);
    analysis_result explicit_conversion(node_index at, qualified_type to,
                                        expression_meaning &m);
    /* [expr.mul] to [expr.log.or] */
    analysis_result binary(node_index n, expression_meaning &m);
    analysis_result binary_built(analysis_result r, token_index at,
                                 preprocessing_op_or_punc op,
                                 const expression_meaning &left,
                                 const expression_meaning &right,
                                 expression_meaning &m);
    analysis_result arithmetic(node_index n, preprocessing_op_or_punc op,
                               expression_meaning &left,
                               expression_meaning &right,
                               expression_meaning &m);
    analysis_result comparison(node_index n, preprocessing_op_or_punc op,
                               expression_meaning &left,
                               expression_meaning &right,
                               expression_meaning &m);
    /* [expr.type]/3: the composite pointer type of two operands, one a
       pointer and the other a pointer or a null pointer constant, both
       converted to it; unsupported where analysis has no rule */
    analysis_result composite_pointer_type(node_index at, expression_meaning &a,
                                           expression_meaning &b);
    /* [expr.cond] */
    analysis_result conditional(node_index n, expression_meaning &m);
    /* [expr.ass] */
    analysis_result assignment(node_index n, expression_meaning &m);
    /* [expr.comma] */
    analysis_result comma(node_index n, expression_meaning &m);
    /* a type-id ([dcl.name]) */
    analysis_result type_id(node_index n, qualified_type &type);

    /* ---- standard conversions ([conv]) ---- */
    /* the fundamental type of t, none if t is no fundamental type */
    fundamental_type fundamental_of(qualified_type t) const;
    bool is_integral(qualified_type t) const;
    bool is_floating(qualified_type t) const;
    bool is_arithmetic(qualified_type t) const;
    bool is_pointer(qualified_type t) const;
    bool is_object_pointer(qualified_type t) const;
    /* [basic.lval]/6: the lvalue-to-rvalue, array-to-pointer and
       function-to-pointer conversions of an operand that is to be a
       prvalue */
    analysis_result operand_value(node_index at, expression_meaning &m);
    /* [conv.prom] */
    void promote(expression_meaning &m);
    /* a prvalue converted to the arithmetic type to, its value with it */
    void convert_arithmetic(expression_meaning &m, fundamental_type to);
    /* ... its type and value, without the node */
    void arithmetic_value(expression_meaning &m, fundamental_type to);
    /* [expr.arith.conv]: the operands' common type, both converted */
    fundamental_type usual_arithmetic_conversions(expression_meaning &a,
                                                  expression_meaning &b);
    /* [conv.qual]/2 */
    bool similar(qualified_type a, qualified_type b) const;
    /* [conv.qual]/3: whether a prvalue of type from converts to type to by
       a qualification conversion */
    bool qualification_convertible(qualified_type from,
                                   qualified_type to) const;
    /* [dcl.init]/17.9: the standard conversion sequence of an implicit
       conversion of the prvalue m to to (cv-unqualified); ill-formed
       without one */
    analysis_result implicit_conversion(node_index at, expression_meaning &m,
                                        qualified_type to, bool direct);
    /* [conv.bool], [expr.context]/1: contextually converted to bool */
    analysis_result contextual_bool(node_index at, expression_meaning &m);

    /* ---- initialization ([dcl.init]) ---- */
    struct initialized_value {
        constness evaluation = constness::unknown;
        uint64_t value = 0;
        /* the initialization in the semantic tree */
        uint32_t node = 0;
    };
    /* the object or reference of type type (an array of unknown bound gets
       its bound) initialized by the initializer node (a
       brace-or-equal-initializer, initializer or braced-init-list) */
    analysis_result initializer(syntax_child init, qualified_type &type,
                                initialized_value &result);
    analysis_result initialize_from(node_index at, syntax_child clause,
                                    qualified_type &type, bool direct,
                                    bool list, initialized_value &result);
    analysis_result list_initialization(node_index list, qualified_type &type,
                                        bool direct, initialized_value &result);
    analysis_result string_initialization(node_index at,
                                          const expression_meaning &string,
                                          qualified_type &type);
    analysis_result reference_binding(node_index at, expression_meaning &m,
                                      qualified_type reference);
    /* [dcl.init.list]/7: whether converting m to to narrows */
    analysis_result narrowing(node_index at, const expression_meaning &m,
                              qualified_type to, bool &narrows);
    /* the type's size in bytes ([expr.sizeof]), 0 if not complete */
    uint64_t size_of_type(qualified_type t) const;
    uint64_t alignment_of_type(qualified_type t) const;

    /* the declaration analysed now, for the expressions' records */
    node_index current_declaration = 0;
    /* the scope analysis is in: 0 the global namespace, else a
       block_scope entity */
    entity_index current_scope = 0;
    /* the entity the last declarator declared */
    entity_index last_declared = 0;
    /* where claims about the expressions analysed now go
       (analysed_expression::place), the scope there, and whether
       declarations analysed now can have claims after them */
    token_index claim_place = 0;
    entity_index claim_scope = 0;
    bool declarations_claimable = true;

    /* ---- statements ([stmt]) ---- */
    /* a point of a function body: the innermost scope there, and a token
       at it (later tokens are later points) */
    struct jump_point {
        entity_index scope = 0;
        token_index at = 0;
    };
    struct label_record {
        uint32_t name = 0;
        jump_point point;
        bool analysed = false;
    };
    struct goto_record {
        uint32_t name = 0;
        jump_point point;
        node_index node = 0;
    };
    struct switch_record {
        /* the adjusted type of the condition, none if unknown */
        fundamental_type type = fundamental_type::none;
        jump_point point;
        size_t first_value = 0;
        bool has_default = false;
    };
    /* the function definition whose body is analysed */
    struct function_context {
        bool active = false;
        qualified_type returns;
        unsigned loops = 0, breakable = 0;
        owned_array<label_record> labels;
        owned_array<goto_record> gotos;
        /* [stmt.dcl]/3: the variables whose initialization a jump shall
           not bypass, where each is declared */
        owned_array<jump_point> initialized;
        owned_array<switch_record> switches;
        owned_array<uint64_t> case_values;
    } function_state;
    analysis_result function_body(node_index definition, syntax_child compound);
    void collect_labels(syntax_child s);
    /* the statement functions give the statement's node in built (when
       well-formed) */
    analysis_result statement(syntax_child s, uint32_t &built);
    /* a substatement ([stmt.pre]/2): a block scope of its own whose
       outermost names shall not redeclare conflicts' */
    analysis_result substatement(syntax_child s, entity_index conflicts,
                                 uint32_t &built);
    analysis_result compound_statement(node_index n, entity_index conflicts,
                                       uint32_t &built);
    analysis_result block_items(node_index n, uint32_t &built);
    analysis_result expression_statement(node_index n, uint32_t &built);
    analysis_result selection_statement(node_index n, uint32_t &built);
    analysis_result iteration_statement(node_index n, uint32_t &built);
    analysis_result jump_statement(node_index n, uint32_t &built);
    analysis_result labeled_statement(node_index n, uint32_t &built);
    analysis_result case_label(node_index n, syntax_child value,
                               size_t innermost);
    /* operand: the operand's node, initialized as the result (0: none) */
    analysis_result return_statement(node_index n, uint32_t &operand);
    /* the variables declared at block scope, in order, until the
       declaration statement that declares them takes them */
    owned_array<entity_index> block_variables;
    /* the declaration statement of block_variables[first ..] */
    uint32_t declaration_statements(size_t first, token_index at);
    node_index node_of(syntax_child c) const;
    /* a condition's declarator is analysed ([stmt.pre]/4) */
    bool in_condition = false;
    /* [stmt.pre]: a condition, its value converted to bool (or, for a
       switch, the condition's value promoted: m) */
    analysis_result condition(syntax_child c, bool for_switch,
                              expression_meaning &m);
    analysis_result switch_condition(node_index at, expression_meaning &m);
    /* [stmt.dcl]/3: a jump from from to to bypasses no initialization */
    bool jump_bypasses(jump_point from, jump_point to) const;
    bool in_scope(jump_point variable, jump_point at) const;
    /* [expr.call] */
    analysis_result call(node_index n, expression_meaning &m);
    /* the initialization of type from the analysed expression m */
    analysis_result initialize_with(node_index at, expression_meaning &m,
                                    qualified_type type, bool direct, bool list,
                                    initialized_value &result);
    /* a declarator and its initializer (none if !initialized), as in an
       init-declarator or a condition */
    analysis_result declare_with_initializer(node_index declaration,
                                             syntax_child d, syntax_child init,
                                             bool initialized,
                                             const specified &s,
                                             bool definition);

    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
    diagnostic_consumer &diagnostics;
};

/* ---- the Itanium C++ ABI (abi/itanium/) ---- */

/* [abi:mangling]: the symbol of entity e, a function or a variable,
   appended to out: its mangled name, or its identifier where the ABI does
   not mangle it; false if the name needs rules not written yet (out may
   have a part of it) */
bool itanium_symbol_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index e, vbuffer *out);

} // namespace cplusplus

#endif
