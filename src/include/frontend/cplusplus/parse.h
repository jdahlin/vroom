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

    preprocessor &pp;
    source_files &files;

    /* the tokens made so far */
    size_t made() const
    {
        return tokens.size();
    }

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

    /* the tree under n as indented lines: a nonterminal and where it
       starts, then its children; a token as its spelling in quotes */
    void dump(node_index n, token_stream &tokens, vbuffer *out) const;

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
    template_name,
    namespace_name,
    /* a variable, function, enumerator, ...: declared, and none of the above */
    other,
};

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
    void leave();
    scope_index current() const
    {
        return current_scope;
    }

    /* [basic.scope.declarative]/1: the name is introduced into the current
       declarative region */
    void declare(uint32_t name, name_kind kind);
    /* [namespace.alias]/2: the identifier is a synonym of the namespace */
    void declare_namespace_alias(uint32_t name, scope_index nominated);
    /* [namespace.udir]/2: "the names in the nominated namespace can be
       used in the scope in which the using-directive appears" */
    void use_namespace(scope_index nominated);
    /* [basic.lookup.unqual]/1: "the scopes are searched for a declaration in
       the order listed ...; name lookup ends as soon as a declaration is
       found for the name"; *scope is a namespace-name's region */
    name_kind lookup_unqualified(uint32_t name, scope_index *scope = nullptr);
    /* [basic.lookup.qual]: the name in the namespace a
       nested-name-specifier named */
    name_kind lookup_qualified(scope_index in, uint32_t name,
                               scope_index *scope = nullptr);

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
        /* the region of a namespace-name */
        scope_index scope;
    };
    struct scope {
        scope_kind kind;
        scope_index enclosing;
        owned_array<declaration> declarations;
        /* inline and unnamed namespaces of this one, and the namespaces
           using-directives nominated in it: searched with it */
        owned_array<scope_index> searched_with;
    };
    owned_objects<scope> scopes;
    scope_index current_scope = 0;
    /* each change, to undo: a declaration added (where) or a scope searched
       with another (where, with the high bit) */
    owned_array<uint32_t> journal;
    const declaration *find(scope_index in, uint32_t name,
                            unsigned depth = 0) const;
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
};

/* what a decl-specifier-seq said that the rest of the declaration needs */
struct decl_specifier_facts {
    node_index node = 0;
    bool typedef_ = false;
    bool has_defining_type_specifier = false;
    /* a class-specifier, enum-specifier or elaborated-type-specifier, which
       may be all a declaration declares ([dcl.pre]/5) */
    bool declares_type = false;
    /* in: a type-id's type-specifier-seq, not a defining-type-specifier-seq */
    bool type_specifier_seq = false;
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
        position++;
    }
    standard_keyword keyword_of(const token &t) const;

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
        position++;
    }
    /* how many namespace-bodies and linkage-specification braces enclose
       the current declaration: a '}' outside the declaration ends them */
    unsigned enclosing_braces = 0;
    /* cxxparse -debug-fallbacks: why each declaration was not parsed */
    bool debug_fallbacks = false;
    /* the token if it is op: added, true */
    bool accept(preprocessing_op_or_punc op);
    bool accept(standard_keyword k);
    /* the node of the children since mark, added to the pending children
       in their place */
    node_index finish_node(syntax_kind kind, size_t mark,
                           token_index first_token);

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
        size_t pending, nodes, messages, errors, lookups;
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

    /* ---- [dcl.pre] */
    node_index declaration_seq(bool until_closing_brace);
    node_index declaration();
    bool starts_module_declaration();
    node_index block_or_function_declaration();
    node_index simple_declaration_rest(size_t mark, token_index first,
                                       const decl_specifier_facts &specifiers);
    node_index static_assert_declaration();
    node_index alias_declaration();

    /* ---- [dcl.spec], [dcl.type], [dcl.type.simple], [dcl.type.elab] */
    /* unknown_names: a name the parser cannot classify counts as one */
    bool at_decl_specifier(bool unknown_names);
    node_index decl_specifier_seq(decl_specifier_facts &facts,
                                  bool type_specifiers_only);
    /* a type-name at the current token (after any nested-name-specifier):
       the identifier names a type in the scope the lookup finds */
    bool type_name_follows(unsigned ahead);
    node_index simple_type_specifier_with_name();
    node_index elaborated_or_class_specifier(decl_specifier_facts &facts);
    node_index decltype_specifier();

    /* ---- [expr.prim.id], [expr.prim.id.qual] */
    /* nested-name-specifier_opt: 0 if none; *scope is the namespace it
       names when it names one, *known whether every name in it was found */
    node_index nested_name_specifier(scope_index *scope, bool *known);
    bool nested_name_specifier_follows(unsigned ahead);
    node_index declarator_id(declarator_facts &facts);

    /* ---- [dcl.decl], [dcl.name], [dcl.fct] */
    node_index init_declarator_list(const decl_specifier_facts &specifiers,
                                    declarator_facts &first);
    node_index init_declarator(const decl_specifier_facts &specifiers,
                               declarator_facts &facts, bool first);
    node_index declarator(declarator_form form, declarator_facts &facts);
    node_index ptr_declarator(declarator_form form, declarator_facts &facts);
    node_index noptr_declarator(declarator_form form, declarator_facts &facts);
    bool parenthesis_could_be_initializer();
    bool at_ptr_operator();
    node_index ptr_operator(derivation *kind);
    node_index cv_qualifier_seq();
    node_index parameters_and_qualifiers();
    bool parameters_follow(declarator_form form);
    node_index parameter_declaration_clause();
    node_index parameter_declaration();
    node_index trailing_return_type();
    node_index type_id();
    node_index noexcept_specifier();

    /* ---- [dcl.init], [dcl.fct.def.general] */
    node_index initializer();
    node_index function_body();

    /* ---- [basic.namespace], [dcl.link] */
    node_index namespace_definition();
    node_index namespace_alias_definition();
    node_index using_directive();
    node_index linkage_specification();

  private:
    parser_statistics counters;
    owned_array<standard_keyword> keyword_kinds;
};

} // namespace cplusplus

#endif
