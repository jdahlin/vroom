/*
 * parse.h: the C++ parser, written against the standard
 * (doc/PLAN-cxx-parse.md).
 *
 * [lex.phases]/1.7: "Each preprocessing token is converted into a token.
 * The resulting tokens are syntactically and semantically analyzed and
 * translated as a translation unit."  This header is the syntactic analysis:
 * the tokens of phase 7 (token_stream), the syntax tree of the grammar's
 * nonterminals (syntax_tree), the kinds of name the grammar depends on
 * (declared_names) and the parser.
 *
 * The storage is flat and indexed so that it can be cached and tuned later
 * without changing the interfaces: a token is a token_index, a node is a
 * node_index, a name is the preprocessor's identifier number.
 */
#ifndef VCC_CPLUSPLUS_PARSE_H
#define VCC_CPLUSPLUS_PARSE_H

#include "frontend/cplusplus/cplusplus.h"

namespace cplusplus
{

/* ---- the tokens of phase 7 --------------------------------------------- */

/* a token of the translation unit: its place in the token_stream */
using token_index = uint32_t;

/*
 * [lex.phases]/1.5-1.7 over the preprocessor: escape sequences converted,
 * "Adjacent string literal tokens are concatenated", and "Each preprocessing
 * token is converted into a token".  The tokens are made as they are asked
 * for and kept, so the parser looks ahead and goes back by index.
 */
class token_stream
{
  public:
    explicit token_stream(preprocessor &pp);
    token_stream(const token_stream &) = delete;
    token_stream &operator=(const token_stream &) = delete;

    /* the token at i; every index from the end of the translation unit on is
       the end_of_file token */
    const token &operator[](token_index i);
    /* whether i is at or after the end of the translation unit */
    bool at_end(token_index i);
    /* the preprocessing tokens token i was made of: more than one for
       concatenated string literals ([lex.phases]/1.6) */
    const preprocessing_token *pieces(token_index i, size_t *count);
    /* where the token is: its invocation's place when a macro made it */
    source_location location(token_index i);
    source_position position(token_index i);
    /* the spelling of the token, its pieces separated by one space */
    size_t spelling(token_index i, vbuffer *out);
    /* the number of the token's identifier (the preprocessor's, so equal
       spellings are equal numbers), 0 if it is no identifier */
    uint32_t identifier(token_index i);
    /* the token is the first of a name that is no identifier (operator +,
       [over.oper]/1): identifier() is id for it from now on */
    void name_token(token_index i, uint32_t id);

    preprocessor &pp;
    source_files &files;

    /* the tokens made so far */
    size_t made() const
    {
        return tokens.size();
    }
    /* [ext.clang] clang_type_trait_names: the keywords (by their index in
       the keyword table) that are identifiers when no '(' follows them;
       set by the parser before the first token is made */
    owned_array<bool> revertible_keywords;

  private:
    owned_array<token> tokens;
    /* token i is preprocessing_tokens[first_piece[i] .. first_piece[i+1]) */
    owned_array<uint32_t> first_piece;
    owned_array<preprocessing_token> preprocessing_tokens;
    owned_array<uint32_t> identifiers;
    /* the preprocessing token read ahead to see whether a string literal is
       followed by another */
    preprocessing_token lookahead;
    bool has_lookahead = false;
    bool complete = false;
    preprocessing_token read();
    void make_through(token_index i);
};

/* ---- the syntax tree ---------------------------------------------------- */

enum class syntax_kind : uint16_t {
#define SYNTAX_NODE(name, nonterminal, subclause) name,
#include "../../../frontend/cplusplus/parse/syntax-kinds.inc"
#undef SYNTAX_NODE
    count
};
/* the nonterminal as the standard spells it, and the subclause that defines
   it ("" for the scaffolding kinds) */
const char *syntax_kind_name(syntax_kind kind);
const char *syntax_kind_subclause(syntax_kind kind);
/* whether the nonterminal is an expression's ([expr.prim] to [expr.comma]):
   a node that has a value, which parentheses around it do not change */
bool syntax_kind_is_expression(syntax_kind kind);

/* a node of the translation unit's syntax tree; 0 is no node */
using node_index = uint32_t;

/* a child of a node: a node or a token of the alternative that matched */
struct syntax_child {
    uint32_t bits = 0;
    static syntax_child node(node_index n)
    {
        return {n << 1};
    }
    static syntax_child token(token_index t)
    {
        return {t << 1 | 1};
    }
    bool is_token() const
    {
        return bits & 1;
    }
    uint32_t index() const
    {
        return bits >> 1;
    }
};

struct syntax_node {
    syntax_kind kind;
    /* the node's children are children[first_child .. first_child+child_count) */
    uint32_t first_child, child_count;
    /* the tokens the node covers: [first_token, end_token) */
    token_index first_token, end_token;
};

/*
 * The syntax tree of one translation unit: the nonterminals that matched,
 * each with the nonterminals and tokens of its alternative in order.  Nodes
 * and child lists are two arrays the tree owns; a node is built after its
 * children, so a child's index is smaller than its parent's.
 */
class syntax_tree
{
  public:
    syntax_tree();
    syntax_tree(const syntax_tree &) = delete;
    syntax_tree &operator=(const syntax_tree &) = delete;

    node_index make(syntax_kind kind, const syntax_child *children,
                    size_t child_count, token_index first_token,
                    token_index end_token);
    const syntax_node &operator[](node_index n) const
    {
        return nodes[n];
    }
    const syntax_child *children(node_index n) const
    {
        return child_lists.data() + nodes[n].first_child;
    }
    /* the number of nodes, the unused node 0 included */
    size_t size() const
    {
        return nodes.size();
    }
    /* how many nodes of a kind the tree has */
    size_t count(syntax_kind kind) const
    {
        return kind_counts[static_cast<size_t>(kind)];
    }
    /* a tentative parse that failed takes back what it built: the tree as
       it was with node_count nodes */
    void truncate(size_t node_count);

    /* [temp.names]/3: the '>>' (or '>=', '>>=') tokens whose first '>'
       ended a template-argument-list: such a token is the child of two
       nodes, the first '>' and the rest */
    owned_array<token_index> split_tokens;
    bool is_split(token_index t) const;
    /* the template-names used as a type without a template-argument-list:
       a placeholder for a deduced class type ([dcl.type.class.deduct]) or,
       in its class, the injected-class-name ([temp.local]/1) */
    owned_array<token_index> template_names_as_types;

    /* the tree under n as indented lines: a nonterminal and where it
       starts, then its children; a token as its spelling in quotes */
    void dump(node_index n, token_stream &tokens, vbuffer *out) const;
    /* [basic.link]: the declarations of the translation unit in order:
       those of its declaration-seq, or of a module unit's global module
       fragment, purview and private module fragment (and its
       module-declaration) */
    void declarations(owned_array<node_index> *out) const;

    node_index root = 0;

  private:
    owned_array<syntax_node> nodes;
    owned_array<syntax_child> child_lists;
    size_t kind_counts[static_cast<size_t>(syntax_kind::count)] = {};
};

/* ---- the names the grammar depends on ----------------------------------- */

/*
 * What the grammar asks of a name: [dcl.type.simple] type-name,
 * [temp.names] template-name, [namespace.def] namespace-name, or none of
 * them.  [basic.lookup]/1: "The name lookup rules apply uniformly to all
 * names (including typedef-names, namespace-names, and class-names) wherever
 * the grammar allows such names in the context discussed by a particular
 * rule."
 */
enum class name_kind : uint8_t {
    not_declared,
    type_name,
    /* a class template, an alias template or a template template
       parameter: its template-id names a type ([temp.names]/1) */
    template_name,
    namespace_name,
    /* a variable, enumerator, ...: declared, and none of the above */
    other,
    /* a function ([temp.names]/2 tells it from other) */
    function,
    /* a function template, a variable template ([temp.names]/2: "an
       overload set that contains a function template"), a concept
       ([temp.concept]/2) */
    function_template,
    variable_template,
    concept_name,
};

/* whether a name of kind k is a template-name ([temp.names]/2: "name
   lookup finds a template-name or an overload set that contains a
   function template") */
inline bool is_template_kind(name_kind k)
{
    return k == name_kind::template_name || k == name_kind::function_template ||
           k == name_kind::variable_template || k == name_kind::concept_name;
}

/* [basic.scope]: the declarative regions the syntax makes */
enum class scope_kind : uint8_t {
    namespace_,
    block,
    class_,
    function_prototype,
    template_parameter,
};

using scope_index = uint32_t;

/*
 * The declarations of a translation unit as far as the grammar needs them:
 * which names are declared in which declarative region, and as what kind.
 * No types, no overload sets: this stands in for [basic.lookup] until the
 * semantic analysis has it in full (doc/PLAN-cxx-parse.md).
 *
 * Every lookup is recorded with its answer, so that a cached parse of the
 * same tokens can later be checked against the names it depended on.
 */
class declared_names
{
  public:
    static constexpr scope_index global_namespace = 0;

    declared_names();
    declared_names(const declared_names &) = delete;
    declared_names &operator=(const declared_names &) = delete;

    /* [basic.scope.namespace]/1: a namespace-definition's region; a
       namespace already declared in the current scope is entered again
       ([namespace.def]/2: an original-namespace-name extends it).  The
       members of an inline namespace ([namespace.def]/7) and of an unnamed
       one ([namespace.unnamed]/1) are found by lookup in the enclosing
       namespace too. */
    scope_index enter_namespace(uint32_t name, bool inline_);
    /* a new region of another kind inside the current one */
    scope_index enter(scope_kind kind);
    /* a region made by enter() is the current one again: a function
       declarator's parameters, whose scope goes on after the ')' */
    void reenter(scope_index region);
    void leave();
    scope_index current() const
    {
        return current_scope;
    }

    /* [basic.scope.declarative]/1: the name is introduced into the current
       declarative region; a class-name with the region of its
       member-specification ([class.pre]/1), when it has one */
    void declare(uint32_t name, name_kind kind, scope_index region = 0,
                 bool dependent = false);
    /* ... into another region: an elaborated-type-specifier's
       ([basic.scope.pdecl]/7), a class defined by a qualified name */
    void declare_in(scope_index in, uint32_t name, name_kind kind,
                    scope_index region = 0, bool dependent = false);
    /* [basic.scope.temparam]/1: a template parameter, in the
       template-parameter scope that is current; dependent: it is a type or
       template whose members are not known until it is substituted
       ([temp.dep.type]/7) */
    void declare_template_parameter(uint32_t name, name_kind kind);
    /* the scope a declaration that is not a template parameter belongs
       to: [temp.pre]/2 "The declaration in a template-declaration (if
       any) shall ... declare or define a function, a class, or a
       variable", whose name is in the scope that encloses the
       template-head's */
    scope_index declaring_scope() const;
    /* how many lookups have found a dependent name ([temp.dep]): a
       caller that compares it before and after a construct knows whether
       the construct names one */
    uint64_t dependent_found = 0;
    /* whether a region encloses the current one (the current instantiation
       of a class template is named inside it, [temp.dep.type]/1) */
    bool encloses_current(scope_index region) const;
    /* the template-parameter scopes of an out-of-class member definition
       ([temp.mem]/1, [temp.local]/8): searched after the member's class
       when the declarator-id put the names in its region */
    owned_array<scope_index> template_parameters_in_effect;
    /* [basic.scope.pdecl]/7: "the smallest namespace or block scope that
       contains the declaration" */
    scope_index nearest_namespace_or_block() const;
    scope_kind kind_of(scope_index region) const
    {
        return scopes[region].kind;
    }
    /* [class.derived]/2, [class.member.lookup]: the members of a base class
       are found by lookup in the derived class's region, after its own */
    void add_base(scope_index derived, scope_index base);
    /* [class.mem]/6: "A complete-class context of a class is a function
       body, default argument, noexcept-specifier, or default member
       initializer within the member-specification of the class", where
       "the class is regarded as complete": complete is a region with every
       member the member-specification declares, searched after the
       class's own region in such a context (complete_contexts > 0) */
    scope_index snapshot(scope_index region);
    void set_complete(scope_index region, scope_index complete);
    unsigned complete_contexts = 0;
    /* [class.pre]/2: a class-specifier defines a class an earlier
       declaration in the scope `in` may have declared (class X; ...
       class X { ... };): that declaration's region, which a typedef-name
       or alias made in between holds, finds the members of the
       definition's region too */
    void define_class(scope_index in, uint32_t name, scope_index region);
    /* [namespace.alias]/2: the identifier is a synonym of the namespace */
    void declare_namespace_alias(uint32_t name, scope_index nominated);
    /* [namespace.udir]/2: "the names in the nominated namespace can be
       used in the scope in which the using-directive appears" */
    void use_namespace(scope_index nominated);
    /* [basic.lookup.unqual]/1: "the scopes are searched for a declaration in
       the order listed ...; name lookup ends as soon as a declaration is
       found for the name"; *scope is the region of the namespace-name or
       class-name found */
    name_kind lookup_unqualified(uint32_t name, scope_index *scope = nullptr,
                                 bool *dependent = nullptr);
    /* [basic.lookup.elab]/2: an elaborated-type-specifier's identifier "is
       looked up according to [basic.lookup.unqual] but ignoring any
       non-type names that have been declared" */
    name_kind lookup_elaborated(uint32_t name);
    /* [basic.lookup.qual]: the name in the namespace a
       nested-name-specifier named */
    name_kind lookup_qualified(scope_index in, uint32_t name,
                               scope_index *scope = nullptr,
                               bool *dependent = nullptr);

    struct recorded_lookup {
        uint32_t name;
        scope_index from;
        bool qualified;
        name_kind found;
    };
    owned_array<recorded_lookup> lookups;

    /* what a tentative parse puts back: the declarations made since a mark */
    struct mark {
        size_t journal;
        scope_index current;
    };
    mark journal_mark() const
    {
        return {journal.size(), current_scope};
    }
    void rollback(mark m);

  private:
    struct declaration {
        uint32_t name;
        name_kind kind;
        /* the region of a namespace-name, or of a class-name's
           member-specification (0: none known) */
        scope_index scope;
        /* a type or template that depends on a template parameter
           ([temp.dep.type]) */
        bool dependent = false;
    };
    struct scope {
        scope_kind kind;
        scope_index enclosing;
        owned_array<declaration> declarations;
        /* inline and unnamed namespaces of this one, and the namespaces
           using-directives nominated in it: searched with it */
        owned_array<scope_index> searched_with;
        /* a class's region with all its members ([class.mem]/6), 0 when
           there is none yet */
        scope_index complete = 0;
    };
    owned_objects<scope> scopes;
    scope_index current_scope = 0;
    /* each change, to undo: a declaration added (where) or a scope searched
       with another (where, with the high bit) */
    owned_array<uint32_t> journal;
    const declaration *find(scope_index in, uint32_t name,
                            unsigned depth = 0) const;
    /* find skips the names of no type (lookup_elaborated) */
    bool ignoring_non_types = false;
};

/* ---- the parser ---------------------------------------------------------- */

struct parser_statistics {
    uint64_t tokens = 0, nodes = 0;
    uint64_t tentative_parses = 0, rewinds = 0, rewound_tokens = 0;
    uint64_t lookups = 0;
    uint64_t unparsed = 0, erroneous = 0;
    /* declarations kept as tokens because they use grammar of a later step */
    uint64_t beyond_step = 0;
};

/* [lex.key] Table 5, as parse/keywords.inc names them */
enum class standard_keyword : uint8_t {
    none,
#define STANDARD_KEYWORD(name, spelling) name,
#include "../../../frontend/cplusplus/parse/keywords.inc"
#undef STANDARD_KEYWORD
    /* a keyword of the lexer's table that the standard does not reserve:
       an extension's (__attribute__, _Atomic, __int128, ...) */
    extension,
};

/* [class.pre]/1: class-key: class, struct or union */
inline bool is_class_key(standard_keyword k)
{
    return k == standard_keyword::class_ || k == standard_keyword::struct_ ||
           k == standard_keyword::union_;
}

/* the keywords of an extension's own grammar (ext/extension-keywords.inc),
   when the extension is on */
enum class extension_keyword : uint8_t {
    none,
    attribute,
    restrict,
    extension,
    type_trait,
    type_transformation,
    int128,
};

/*
 * What a declarator declares, as far as the grammar needs to know: the
 * first derivation applied to its declarator-id ([dcl.meaning]/1: the
 * declarator's parts bind as the grammar nests them, the suffixes of a
 * noptr-declarator before its ptr-operators).  A function-definition
 * needs a declarator whose first derivation is a function ([dcl.fct.def.general]).
 */
enum class derivation : uint8_t {
    none,
    pointer,
    reference,
    member_pointer,
    array,
    function
};

struct declarator_facts {
    node_index node = 0;
    /* the declarator-id, if there is one (an abstract-declarator has none) */
    token_index id_first = 0, id_last = 0;
    bool has_id = false, qualified_id = false;
    /* the identifier the declarator declares, when it is one (0: none or
       qualified or an operator-function-id) */
    uint32_t name = 0;
    derivation first = derivation::none;
    /* noptr-declarator parameters-and-qualifiers trailing-return-type */
    bool trailing_return = false;
    /* the function parameter scope of the declarator's last
       parameters-and-qualifiers ([basic.scope.param]/1) */
    bool has_parameters = false;
    scope_index parameters = 0;
    /* the declarator-id is a qualified-id naming a member of a class the
       parser knows: the names after it are looked up in that class too
       ([basic.scope.class]/1, [class.mfct]/2, [class.static.data]/3) */
    bool has_member_of = false;
    scope_index member_of = 0;
    /* the declarator-id is a template-id ([temp.expl.spec], partial
       specializations of variable templates): it declares no new name */
    bool template_id = false;
    /* ... id-expression: a declarator-id that declares a pack
       ([temp.variadic]/4) */
    bool pack = false;
};

/* what a decl-specifier-seq said that the rest of the declaration needs */
struct decl_specifier_facts {
    node_index node = 0;
    bool typedef_ = false;
    token_index typedef_at = 0;
    bool has_defining_type_specifier = false;
    /* a class-specifier, enum-specifier or elaborated-type-specifier, which
       may be all a declaration declares ([dcl.pre]/5) */
    bool declares_type = false;
    /* in: a type-id's type-specifier-seq, not a defining-type-specifier-seq */
    bool type_specifier_seq = false;
    /* how many decl-specifiers the sequence has */
    unsigned count = 0;
    /* in: [temp.res]/5's contexts, where a qualified-id that names a member
       of an unknown specialization is assumed to name a type */
    bool implicit_typename = false;
    /* out: a name in the sequence depends on a template parameter */
    bool dependent = false;
    /* [dcl.type.elab]/1: an elaborated-type-specifier, whether it has the
       form class-key attribute-specifier-seq_opt identifier, and where its
       attribute-specifier-seq is (an elaborated-type-specifier may have one
       only as the sole constituent of a declaration) */
    bool elaborated = false, elaborated_declaring_form = false;
    bool elaborated_attributes = false;
    /* out: the region of the class a type-name in the sequence names (0:
       none, or not known) */
    scope_index type_region = 0;
    /* decl-specifier attribute-specifier-seq: the sequence ends with one */
    bool trailing_attributes = false;
    token_index elaborated_attributes_at = 0;
    token_index elaborated_at = 0;
    /* [dcl.friend], [class.static] */
    bool friend_ = false, static_ = false;
    token_index friend_at = 0;
    /* in: the class whose member-declaration this is, when it has a name:
       that name before ( is a constructor's declarator-id ([class.ctor]/1),
       not a decl-specifier */
    uint32_t constructor_name = 0;
};

/* what an attribute-specifier-seq holds that the rules on its context
   check */
struct attribute_facts {
    /* [dcl.attr.likelihood]: the attribute-tokens likely and unlikely */
    bool likely = false, unlikely = false;
    /* the first of the standard's attributes or an alignment-specifier:
       where it is and the subclause that says what it applies to */
    bool standard = false;
    token_index standard_at = 0;
    const char *standard_subclause = nullptr;
};

/* what a name at a place in the tokens is, to the grammar */
enum class name_class : uint8_t {
    /* no identifier there */
    none,
    /* a type-name: a type-specifier */
    type,
    /* declared, not as a type: where a declarator would start, or an
       id-expression */
    not_a_type,
    /* not declared, or declared where the parser cannot see (a class's
       member, a template's): a later step's */
    unknown,
    /* C::C or C::~C: the declarator-id of a constructor or destructor
       ([class.qual]/2: "the name is instead considered to name the
       constructor of class C") */
    constructor,
    /* a member of an unknown specialization ([temp.dep.type]/6): T::x with
       T a template parameter.  [temp.res]/6: it "denotes a non-type"
       unless typename says otherwise or the context assumes a type
       ([temp.res]/5) */
    dependent,
    /* a concept-name, with its template arguments: the type-constraint of
       a placeholder-type-specifier ([dcl.spec.auto]) */
    concept_,
};

/* the forms a declarator can take */
enum class declarator_form : uint8_t {
    /* declarator: a declarator-id is required */
    concrete,
    /* abstract-declarator_opt: no declarator-id ([dcl.name]) */
    abstract,
    /* a parameter-declaration's: either ([dcl.fct]/3) */
    either,
};

/*
 * The syntactic analysis of [lex.phases]/1.7: a function per nonterminal,
 * in the file of the subclause that defines it, each starting with the
 * productions it implements.
 *
 * A function returns its node (0 when an _opt production matched nothing).
 * When the tokens are not the production, it calls expected(), which notes
 * the first place and production that failed, and returns 0; the caller
 * returns too.  A declaration that failed is then either erroneous (its
 * diagnostic is issued) or, when it uses grammar a later step of
 * doc/PLAN-cxx-parse.md writes, kept as an unparsed node.
 */
class parser
{
  public:
    parser(token_stream &tokens, syntax_tree &tree, declared_names &names,
           diagnostic_consumer &diagnostics);
    parser(const parser &) = delete;
    parser &operator=(const parser &) = delete;

    /* [basic.link]: translation-unit; the tree's root */
    node_index translation_unit();
    /* [module] */
    bool at_phase4_keyword(preprocessing_token_kind kind, unsigned ahead = 0);
    bool at_module_declaration();
    bool at_private_module_fragment();
    node_index module_name();
    node_index module_partition();
    node_index module_declaration();
    node_index module_import_declaration();
    node_index export_declaration();
    node_index module_unit_rest(bool global_fragment);
    void declaration_seq_until_module(bool private_);
    bool in_export_declaration = false;

    parser_statistics statistics();

    token_stream &tokens;
    syntax_tree &tree;
    declared_names &names;
    diagnostic_consumer &diagnostics;

    /* the implementation, one file per subclause */

    /* ---- parse/parser.cpp: the tokens, building nodes, tentative parsing */
    token_index position = 0;
    const token &peek(unsigned ahead = 0)
    {
        if (split_active && !ahead)
            return split_rest;
        return tokens[position + ahead];
    }
    bool at(preprocessing_op_or_punc op, unsigned ahead = 0);
    bool at(standard_keyword k, unsigned ahead = 0);
    bool at_identifier(unsigned ahead = 0);
    bool at_end()
    {
        return tokens.at_end(position);
    }
    void consume()
    {
        split_active = false;
        position++;
    }
    standard_keyword keyword_of(const token &t) const;
    extension_keyword extension_keyword_of(const token &t) const;
    bool at(extension_keyword k, unsigned ahead = 0);
    bool has(extension e) const
    {
        return tokens.files.behavior.has(e);
    }

    /* the children of the node being built: a function notes where its own
       begin, adds them, and makes its node of everything after that mark */
    owned_array<syntax_child> pending;
    size_t begin_node() const
    {
        return pending.size();
    }
    void add(node_index n)
    {
        if (n)
            pending.append(syntax_child::node(n));
    }
    void add_token()
    {
        pending.append(syntax_child::token(position));
        split_active = false;
        position++;
    }
    /* how many namespace-bodies and linkage-specification braces enclose
       the current declaration: a '}' outside the declaration ends them */
    unsigned enclosing_braces = 0;
    /* -fcxx-debug-fallbacks: why each declaration was not parsed */
    bool debug_fallbacks = false;
    /* an expression of the current declaration named something
       declared_names does not know (a template, a class's member, a name
       declared in a part kept as tokens): if the declaration then does not
       parse, that may be for what the name is, so it is kept as tokens */
    bool unknown_name = false;
    /* [dcl.ambig.res]/2's second reading (dcl.ambig.res.cpp): a name the
       parser cannot classify is taken for a type-name, to see whether the
       tokens could be a type-id at all */
    bool unknown_names_are_types = false;
    /* how deeply the expressions and initializers being parsed nest
       ([implimits]/2), and the most this implementation accepts */
    unsigned nesting = 0;
    static constexpr unsigned nesting_limit = 1024;
    /* the current declaration went past the limit, at this token: its
       diagnostic is this one, whatever a tentative parse made of it */
    bool nesting_limit_exceeded = false;
    token_index nesting_limit_at = 0;
    /* [implimits]/2: counts one more level for the caller's scope (level);
       past the limit the parse fails, and 0 is returned */
    bool nesting_within_limit(const char *what);
    struct nesting_level {
        parser &p;
        explicit nesting_level(parser &p) : p(p)
        {
            p.nesting++;
        }
        ~nesting_level()
        {
            p.nesting--;
        }
    };
    /* the token if it is op: added, true */
    bool accept(preprocessing_op_or_punc op);
    bool accept(standard_keyword k);
    bool accept(extension_keyword k);
    /* the node of the children since mark, added to the pending children
       in their place */
    node_index finish_node(syntax_kind kind, size_t mark,
                           token_index first_token);
    /* item ..._opt , item ..._opt , ...: each item parsed by item(),
       then after(), which may fail the parse (both return false when
       failed()); the list is a node of kind list only if it has more than
       one item or an ellipsis, a lone item passes through */
    template <typename Item, typename After>
    bool comma_list(syntax_kind list, Item item, After after)
    {
        size_t mark = begin_node();
        token_index first = position;
        unsigned count = 0;
        bool ellipsis = false;
        for (;;) {
            item();
            if (failed())
                return false;
            count++;
            if (accept(preprocessing_op_or_punc::ELLIPSIS))
                ellipsis = true;
            after();
            if (failed())
                return false;
            if (!accept(preprocessing_op_or_punc::COMMA))
                break;
        }
        if (count > 1 || ellipsis)
            finish_node(list, mark, first);
        return true;
    }
    template <typename Item> bool comma_list(syntax_kind list, Item item)
    {
        return comma_list(list, item, [] {});
    }

    /* ---- failure: the first place the tokens were not the grammar */
    struct failure_state {
        bool failed = false;
        /* the declaration uses grammar of a later step */
        bool beyond_step = false;
        token_index at = 0;
        const char *expected = nullptr, *subclause = nullptr;
        /* parse_expected: "expected WHAT"; parse_violation: a rule the
           tokens break, quoted */
        diagnostic_id id = diagnostic_id::parse_expected;
    } failure;
    bool failed() const
    {
        return failure.failed;
    }
    /* "expected WHAT ([SUBCLAUSE])" at the current token; returns 0 */
    node_index expected(const char *what, const char *subclause);
    /* the tokens use grammar a later step writes (a template, an operator
       name, an attribute, an extension's keyword ...): returns 0 */
    node_index beyond_this_step();
    /* the tokens are the grammar but break a rule on its form (quoted, of
       subclause), at token at; returns 0 */
    node_index violates(const char *rule, const char *subclause,
                        token_index at);

    /*
     * A parse that may turn out not to be what the tokens are: the rules
     * worded "could possibly be" ([dcl.ambig.res], [stmt.ambig]) try one
     * reading first.  Unless commit() is called, leaving the region puts
     * the position, the pending children, the tree, the failure state and
     * the declared names' lookups back, and discards the diagnostics
     * issued in it.
     */
    class tentative_parse
    {
      public:
        explicit tentative_parse(parser &p);
        ~tentative_parse();
        tentative_parse(const tentative_parse &) = delete;
        tentative_parse &operator=(const tentative_parse &) = delete;
        void commit();

      private:
        parser &p;
        token_index position;
        size_t pending, nodes, messages, errors, lookups, splits;
        bool split_active;
        token split_rest;
        declared_names::mark names;
        failure_state failure;
        bool committed = false;
    };

    /* ---- parse/parser.cpp: scaffolding */
    /* one declaration's tokens as an unparsed node: up to the ';' or the
       '}' that ends it, brackets balanced */
    node_index unparsed_declaration();
    /* tokens up to (not including) one of stops outside all brackets, as an
       unparsed node: a part of the grammar not written yet (an expression,
       an initializer-clause); 0 if there are none */
    node_index unparsed_until(const preprocessing_op_or_punc *stops,
                              size_t stop_count);
    /* a bracketed group, ( ) [ ] or { }, at the current token, as an
       unparsed node of all its tokens */
    node_index unparsed_group();
    /* the lookahead after the bracketed group that starts at
       tokens[ahead], its brackets balanced (ahead itself if no bracket
       opens there) */
    unsigned group_end(unsigned ahead);

    /* ---- [dcl.attr.grammar] */
    /* an attribute-specifier starts at tokens[ahead]: [[ or alignas */
    bool at_attribute_specifier(unsigned ahead = 0);
    /* the lookahead after an attribute-specifier-seq_opt at tokens[ahead],
       its brackets balanced, without parsing it */
    unsigned attribute_specifier_seq_end(unsigned ahead);
    /* attribute-specifier-seq_opt: 0 when there is none; with
       appertains_to_type it is one that appertains to a type */
    node_index attribute_specifier_seq(bool appertains_to_type = false);
    node_index attribute_specifier(attribute_facts &facts);
    node_index attribute(bool using_prefix, attribute_facts &facts);
    bool at_attribute_identifier(unsigned ahead = 0);
    node_index balanced_token_seq(preprocessing_op_or_punc closing);
    /* [dcl.attr.grammar]/7: [[ where no attribute-specifier can be */
    node_index misplaced_attribute_brackets();

    /* ---- ext/gnu/attribute.cpp: __attribute__ (( ... )) */
    bool at_gnu_attribute(unsigned ahead = 0);
    node_index gnu_attribute_specifier();
    node_index gnu_attributes_opt();
    bool standard_attributes_first = false;
    /* ---- ext/gnu/extension_keyword.cpp: __extension__ before a
       declaration (alternative parses what follows) or an expression */
    node_index gnu_extension_prefixed(node_index (parser::*alternative)());
    node_index gnu_extension_expression();
    /* ---- ext/gnu/labels_as_values.cpp: &&label, goto *p; */
    node_index gnu_label_address();
    node_index gnu_computed_goto(size_t mark, token_index first);
    /* ---- ext/gnu/designated_initializers.cpp: { [1] = x, .a.b = y } */
    bool gnu_initializer_list_needed();
    node_index gnu_initializer_list();
    /* ---- ext/gnu/compound_literal.cpp: ( type-id ) { ... } */
    node_index gnu_compound_literal(size_t mark, token_index first);
    /* ---- ext/clang/type_traits.cpp: __is_same(T, U), __remove_cv(T) */
    node_index clang_type_trait_expression();
    node_index clang_type_transformation();
    /* ---- ext/gnu/asm_label.cpp: declarator asm ( "name" ) attributes */
    bool gnu_declarator_suffix_follows(const declarator_facts &facts);
    node_index gnu_declarator_suffix(size_t mark, token_index first,
                                     declarator_facts &facts);

    /* ---- [dcl.pre] */
    node_index declaration_seq(bool until_closing_brace);
    node_index declaration();
    node_index declaration_alternative();
    /* [intro.compliance]/2.2: a declaration (or member-declaration) that
       does not parse is kept as its tokens, erroneous or unparsed, and the
       next one starts after it */
    node_index recover_declaration(node_index (parser::*alternative)());
    bool starts_module_declaration();
    node_index block_or_function_declaration();
    node_index simple_declaration_rest(size_t mark, token_index first,
                                       const decl_specifier_facts &specifiers,
                                       bool attributes);
    /* [dcl.struct.bind]: ref-qualifier_opt [ identifier-list ], each
       identifier declared */
    bool structured_binding_follows();
    node_index structured_binding_identifiers();
    node_index static_assert_declaration();
    node_index alias_declaration();
    node_index attribute_declaration();

    /* ---- [dcl.enum], [enum.udecl] */
    /* an opaque-enum-declaration starts here: enum-key
       attribute-specifier-seq_opt enum-head-name, then ; or an enum-base
       that ends with ; */
    bool opaque_enum_declaration_follows();
    node_index opaque_enum_declaration();
    /* enum-specifier or elaborated-enum-specifier in a decl-specifier-seq */
    node_index enum_specifier_or_elaborated(decl_specifier_facts &facts);
    node_index enumerator_list(bool scoped);
    node_index using_enum_declaration();

    /* ---- [namespace.udecl], [dcl.asm] */
    node_index using_declaration();
    node_index using_declarator();
    node_index asm_declaration();

    /* ---- [dcl.spec], [dcl.type], [dcl.type.simple], [dcl.type.elab] */
    /* unknown_names: a name the parser cannot classify counts as one */
    bool at_decl_specifier(bool unknown_names);
    node_index decl_specifier_seq(decl_specifier_facts &facts,
                                  bool type_specifiers_only);
    /* the name at tokens[ahead ...]: ::_opt (identifier ::)* identifier,
       looked up; *length is how many tokens it has.  With template_ids, a
       name followed by < is unknown (a template-id is P7's) */
    name_class classify_name(unsigned ahead, unsigned *length,
                             bool template_ids = true);
    /* a type-name at the current token (after any nested-name-specifier):
       the identifier names a type in the scope the lookup finds */
    bool type_name_follows(unsigned ahead);
    node_index simple_type_specifier_with_name();
    scope_index last_type_region = 0;
    node_index elaborated_or_class_specifier(decl_specifier_facts &facts);
    /* explicit ( constant-expression ) ([dcl.fct.spec]) */
    node_index explicit_specifier();
    node_index decltype_specifier();

    /* ---- [expr.prim.id], [expr.prim.id.qual] */
    /* nested-name-specifier_opt: 0 if none; *scope is the namespace or
       class it names when it names one, *known whether every name in it
       was found; qualifier_dependent: it names a type that depends on a
       template parameter ([temp.dep.type]), whose members are members of
       an unknown specialization */
    node_index nested_name_specifier(scope_index *scope, bool *known,
                                     bool template_ids = true);
    bool qualifier_dependent = false;
    bool nested_name_specifier_follows(unsigned ahead);
    node_index declarator_id(declarator_facts &facts);

    /* ---- [dcl.decl], [dcl.name], [dcl.fct] */
    /* declares the identifier a declarator declares (facts.name) */
    void declare_declarator(const decl_specifier_facts &specifiers,
                            const declarator_facts &facts);
    node_index init_declarator_list(const decl_specifier_facts &specifiers,
                                    declarator_facts &first);
    node_index init_declarator(const decl_specifier_facts &specifiers,
                               declarator_facts &facts, bool first);
    node_index declarator(declarator_form form, declarator_facts &facts);
    node_index ptr_declarator(declarator_form form, declarator_facts &facts);
    node_index noptr_declarator(declarator_form form, declarator_facts &facts);
    bool parenthesis_could_be_initializer();
    bool at_ptr_operator();
    bool at_ptr_operator_at(unsigned ahead);
    node_index ptr_operator(derivation *kind);
    node_index cv_qualifier_seq();
    /* *parameters: the function parameter scope it made */
    node_index parameters_and_qualifiers(scope_index *parameters = nullptr);
    bool parameters_follow(declarator_form form);
    node_index parameter_declaration_clause();
    node_index parameter_declaration();
    node_index trailing_return_type();
    node_index type_id();
    node_index noexcept_specifier();
    /* [except.spec] of C++17 and earlier: throw ( type-id-list_opt ) */
    node_index dynamic_exception_specification();

    /* ---- [class.pre], [class.mem], [class.derived], [class.base.init] */
    /* class-head's base-clause_opt, { member-specification_opt }: the
       class's region is made by the caller */
    node_index base_clause(scope_index derived);
    node_index base_specifier(scope_index derived);
    /* class-or-decltype; *region: the class it names, if the parser knows
       it (0 if not) */
    node_index class_or_decltype(scope_index *region);
    /* { member-specification_opt } of the class name (0: unnamed) whose
       region is region, in two passes ([class.mem]/6) */
    node_index class_body(uint32_t name, scope_index region);
    node_index member_specification();
    node_index member_declaration();
    node_index member_declaration_alternative();
    node_index member_declarator(const decl_specifier_facts &specifiers,
                                 size_t mark, token_index first, bool first_one,
                                 bool *definition);
    /* virt-specifier-seq_opt: 0 when there is none */
    node_index virt_specifier_seq();
    bool at_virt_specifier(unsigned ahead = 0);
    /* an identifier with the special meaning word ([lex.name]/2 Table 4) */
    bool at_word(const char *word, unsigned ahead = 0);
    /* ( at tokens[ahead] begins a constructor's parameters ([class.ctor]) */
    bool constructor_parameters_follow(unsigned ahead);
    node_index ctor_initializer();
    node_index mem_initializer();
    /* the first pass over a member-specification: only the names its
       member-declarations declare; the complete-class contexts are skipped */
    bool collecting_members = false;
    /* a member-declaration's declarator is being parsed: its default
       arguments are complete-class contexts */
    bool member_declarator_context = false;
    /* the class whose member-specification is being parsed directly: what
       a nested-name-specifier-less constructor name is */
    uint32_t member_class_name = 0;

    /* ---- [over.oper], [over.literal], [class.conv.fct] */
    /* operator-function-id, literal-operator-id or conversion-function-id
       at operator */
    node_index operator_unqualified_id();
    node_index conversion_type_id();

    /* ---- [dcl.init], [dcl.fct.def.general] */
    node_index initializer();
    node_index initializer_clause();
    node_index braced_init_list();
    /* a braced-init-list where C++03 has none (anywhere but in the = { }
       of an aggregate's initializer): when one starts here in an edition
       without list-initialization, the parse fails as an extension's */
    bool list_initialization_missing();
    /* in_braces: a braced-init-list's, whose ',' before the '}' is not
       the list's */
    node_index initializer_list(bool in_braces);
    node_index designated_initializer_list();
    /* the declarator is the function-definition's: its parameters' scope
       goes on through the body ([basic.scope.param]/1) */
    void defaultable_declarator(const declarator_facts &declarator,
                                bool member);
    node_index function_body(const declarator_facts &declarator,
                             bool member = false);

    /* ---- [stmt] */
    /* statement, with the recovery of [intro.compliance]/2.2: a statement
       that does not parse is kept as its tokens, either erroneous (its
       diagnostic issued) or unparsed (a later step's grammar), and the
       next statement starts after it.  Only where no tentative parse other
       than the enclosing statement's or declaration's is open: inside one,
       the statement's failure is the reading's. */
    node_index statement();
    /* the statement without recovery */
    node_index statement_alternative();
    /* the tokens of the statement at the current token, to its ';' or the
       '}' of its last substatement, as one unparsed node; *ended: it
       stopped at such a ';' or '}', not at an enclosing '}' or the end */
    node_index unparsed_statement(bool *ended);
    node_index labeled_statement();
    node_index expression_statement();
    node_index compound_statement();
    node_index selection_statement();
    node_index iteration_statement();
    node_index jump_statement();
    /* [stmt.return.coroutine], [except.pre] */
    node_index coroutine_return_statement();
    node_index try_block();
    node_index handler_seq();
    node_index handler();
    node_index exception_declaration();
    /* declaration-statement: block-declaration */
    node_index block_declaration();
    /* the alternatives every declaration context shares, chosen by their
       first tokens: alias-declaration, using-enum-declaration,
       using-declaration, static_assert-declaration and
       opaque-enum-declaration; false, *n untouched, if none starts here */
    bool shared_declaration_alternative(node_index *n);
    node_index init_statement();
    /* condition, followed by closing (')', or ';' in a for statement) */
    node_index condition(preprocessing_op_or_punc closing);
    node_index for_range_declaration();
    /* a substatement: in a block scope of its own ([basic.scope.block]/1) */
    node_index substatement();

    /* ---- [stmt.ambig] */
    /* what a statement (or init-statement, or condition) that starts at
       tokens[ahead] can be, by its first tokens */
    enum class reading : uint8_t {
        /* nothing but a declaration starts so */
        declaration,
        /* nothing but an expression */
        expression,
        /* a type-name or a type's keyword: either, a declaration if it can
           be ([stmt.ambig]/1, [stmt.pre]/7) */
        either,
        /* a name the parser cannot classify: what the tokens are may
           depend on whether it is a type */
        unknown,
    };
    reading reading_at(unsigned ahead);
    /* parse the tokens as declaration() or expression() by [stmt.ambig] */
    template <class Declaration, class Expression>
    node_index declaration_or_expression(reading r, Declaration declaration,
                                         Expression expression);
    /* how many tentative parses are open, and how many were open where the
       innermost statement or declaration that recovers began */
    unsigned speculation = 0, recovery_level = 0;
    /* a block-declaration is being parsed: a function-definition is not
       one ([dcl.fct.def.general]/2: "A function shall be defined only in
       namespace or class scope") */
    bool block_scope = false;

    /* ---- [dcl.ambig.res] */
    /* ( type-id ) at the current '(' when the tokens can be that
       ([dcl.ambig.res]/2); with cast, only when a cast-expression can
       follow.  True: the three are added.  False: nothing is, and the
       parse has failed if what the tokens are is a later step's to say. */
    bool parenthesized_type_id(bool cast);
    bool cast_expression_follows();

    /* ---- [expr.prim] to [expr.comma], [expr.const] */
    node_index primary_expression();
    /* an id-expression of an expression; member: after . or ->, where the
       name is the object's class's to look up ([basic.lookup.classref]) */
    node_index id_expression(bool member);
    node_index postfix_expression();
    node_index postfix_expression_suffixes(size_t mark, token_index first,
                                           node_index n);
    node_index expression_list();
    /* ( expression-list_opt ) at the current '(': false after
       expected(what, subclause) or another failure */
    bool parenthesized_expression_list(const char *what, const char *subclause);
    node_index unary_expression();
    node_index noexcept_expression();
    /* [expr.await], [expr.yield], [expr.throw] */
    node_index await_expression();
    node_index yield_expression();
    node_index throw_expression();
    /* [expr.prim.lambda] */
    node_index lambda_expression();
    node_index lambda_introducer();
    node_index lambda_capture();
    node_index capture();
    node_index lambda_declarator(scope_index *parameters);
    node_index new_expression();
    node_index new_type_id();
    node_index new_declarator();
    node_index delete_expression();
    node_index cast_expression();
    /* pm-expression to logical-or-expression: the operators of level
       lowest_level and above ([expr.mptr.oper] to [expr.log.or]) */
    node_index binary_expression(unsigned lowest_level);
    /* *conditional: the second alternative matched */
    node_index conditional_expression(bool *conditional = nullptr);
    node_index assignment_expression();
    node_index expression();
    node_index constant_expression();

    /* ---- [basic.namespace], [dcl.link] */
    node_index namespace_definition();
    node_index namespace_alias_definition();
    node_index using_directive();
    node_index linkage_specification();

    /* ---- [temp], [temp.names], [temp.res], [temp.variadic] */
    /* [temp.names]/3: whether '>' and '>>' are operators.  Not in a
       template-argument-list (nor a template-parameter-list,
       [temp.param]/16) outside the brackets nested in it */
    bool greater_is_operator = true;
    /* [temp.names]/3: "the first non-nested >> is treated as two
       consecutive but distinct > tokens": the current token is the second
       of them (or the rest of a >= or >>=), split_rest */
    bool split_active = false;
    token split_rest;
    /* [temp.res]/5: the parameter-declarations parsed now assume a
       qualified-id names a type (those of a member-declaration, of a
       declarator with a qualified declarator-id, of a lambda-declarator or
       requirement-parameter-list, of a non-type template-parameter), and
       the type-id parsed now is in a type-id-only context */
    bool parameters_implicit_typename = false;
    bool type_id_only = false;
    /* the scope a template-declaration's declaration declares its name in
       (none: UINT32_MAX); a declaration there declares a template */
    scope_index template_scope = UINT32_MAX;
    /* the kind a declaration that would declare plain declares: a
       template's ([temp.pre]/1) when it is the template-declaration's */
    name_kind declared_kind(name_kind plain);
    /* the lookahead after the '>' of the template-argument-list whose '<'
       is at tokens[ahead], brackets balanced; 0 when none ends before a
       ';', '{' or '}' */
    unsigned angle_group_end(unsigned ahead);
    bool named_pack_follows();
    bool binary_operator_follows();
    /* in the declaration of an explicit-instantiation or
       explicit-specialization ([dcl.type.elab]/1) */
    bool explicit_declaration = false;
    bool explicit_instantiation_ = false;
    /* the scope a template-declaration, explicit-specialization or
       explicit-instantiation declares its name in ([temp.pre]/5: "the
       init-declarator-list in the declaration shall contain at most one
       declarator") */
    scope_index single_declarator_scope = UINT32_MAX;
    /* [temp.pre]/5: a second declarator, or one after a class template's
       definition: the failure at the current token, else false */
    bool template_declarator_violates(const decl_specifier_facts &specifiers,
                                      bool first);
    /* the last name of the last nested-name-specifier parsed, before its
       template-argument-list if any ([class.qual]/2) */
    uint32_t last_qualifier_name = 0;
    bool abstract_pack_follows();
    /* the token ends a template-argument-list: '>', or '>>', '>=', '>>='
       of which the first '>' does ([temp.names]/3) */
    bool at_closing_angle();
    bool accept_closing_angle();
    /* template-declaration, explicit-specialization or
       explicit-instantiation at template (or extern template); declaration
       parses the declaration they contain (a member-declaration in a
       class) */
    node_index template_declaration(node_index (parser::*declaration)());
    node_index explicit_instantiation(node_index (parser::*declaration)());
    node_index template_head();
    node_index template_parameter_list();
    node_index template_parameter();
    /* a type-constraint at tokens[ahead]: nested-name-specifier_opt
       concept-name < template-argument-list_opt >_opt; *length its tokens */
    bool type_constraint_follows(unsigned ahead, unsigned *length);
    node_index type_constraint();
    node_index requires_clause();
    node_index constraint_logical_or_expression();
    node_index concept_definition();
    bool deduction_guide_follows();
    node_index deduction_guide();
    /* < template-argument-list_opt > at the current '<', its tokens and
       the list pending; with the name before it since mark, a node of
       kind (simple-template-id or template-id) */
    node_index template_id_rest(size_t mark, token_index first,
                                syntax_kind kind);
    node_index template_argument_list();
    node_index template_argument();
    /* [temp.names]/2: the identifier at the current token followed by <
       names a template: kind is what lookup found */
    bool names_template(name_kind kind, bool qualified, bool template_keyword);
    node_index typename_specifier();
    /* [expr.prim.fold], [expr.prim.req], [expr.sizeof]'s sizeof ... */
    bool fold_expression_follows();
    node_index fold_expression();
    node_index requires_expression();
    node_index requirement();

  private:
    parser_statistics counters;
    owned_array<standard_keyword> keyword_kinds;
    owned_array<extension_keyword> extension_kinds;
};

} // namespace cplusplus

#endif
