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
       ([namespace.def]/1: "the same declarative region") */
    scope_index enter_namespace(uint32_t name);
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
    /* [basic.lookup.unqual]/1: "the scopes are searched for a declaration in
       the order listed ...; name lookup ends as soon as a declaration is
       found for the name" */
    name_kind lookup_unqualified(uint32_t name);
    /* [basic.lookup.qual]: the name in the scope a nested-name-specifier
       named; *scope is the namespace's region when the name is one */
    name_kind lookup_qualified(scope_index in, uint32_t name,
                               scope_index *scope = nullptr);

    struct recorded_lookup {
        uint32_t name;
        scope_index from;
        bool qualified;
        name_kind found;
    };
    owned_array<recorded_lookup> lookups;

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
    };
    owned_objects<scope> scopes;
    scope_index current_scope = 0;
    const declaration *find(scope_index in, uint32_t name) const;
};

/* ---- the parser ---------------------------------------------------------- */

struct parser_statistics {
    uint64_t tokens = 0, nodes = 0;
    uint64_t tentative_parses = 0, rewinds = 0, rewound_tokens = 0;
    uint64_t lookups = 0;
    uint64_t unparsed = 0, erroneous = 0;
};

/*
 * The syntactic analysis of [lex.phases]/1.7: a function per nonterminal,
 * in the file of the subclause that defines it, each starting with the
 * productions it implements.
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
    /* [lex.key]: the keywords the parser asks about, as their index in the
       keyword table of this edition (-1: no keyword in it) */
    struct keywords {
        int namespace_, inline_, extern_, export_, template_, class_, struct_,
            union_, enum_, operator_, requires_, try_, catch_;
    } keyword;
    bool at_keyword(int keyword_index, unsigned ahead = 0);
    bool at_end()
    {
        return tokens.at_end(position);
    }
    void consume()
    {
        position++;
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
        position++;
    }
    node_index finish_node(syntax_kind kind, size_t mark,
                           token_index first_token);

    /*
     * A parse that may turn out not to be what the tokens are: the rules
     * worded "could possibly be" ([dcl.ambig.res], [stmt.ambig]) try one
     * reading first.  Unless commit() is called, leaving the region puts
     * the position, the pending children and the tree back, and discards
     * the diagnostics issued in it.
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
        size_t pending, nodes, messages, errors;
        bool committed = false;
    };

    /* ---- parse/parser.cpp: scaffolding */
    /* one declaration's tokens as an unparsed node: up to the ';' or the
       '}' that ends it, brackets balanced */
    node_index unparsed_declaration();

    /* ---- [dcl.pre] */
    node_index declaration_seq();
    node_index declaration();

  private:
    parser_statistics counters;
};

} // namespace cplusplus

#endif
