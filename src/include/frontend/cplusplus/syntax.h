/*
 * syntax.h: typed views of the syntax tree (parse.h's syntax_tree).
 *
 * The tree stores every node the same way: a kind, and its children in the
 * order of the alternative of the grammar that matched.  A view names a
 * node's parts after the standard's grammar instead of indexing children:
 *
 *     auto body = syntax(n).function_definition().function_body();
 *     if (body.deleted()) ...
 *
 * A view is a handle (the tree's context and a node index) built where it
 * is used; nothing is stored in the tree or allocated.  SyntaxRef is the
 * untyped handle.  The views are generated (syntax-views.inc, by
 * src/tools/gen-syntax-views.py, from the grammar):
 *
 *   - one per node kind, CamelCase (FunctionDefinition), with an accessor
 *     per nonterminal of its productions whose kind tells its position
 *     (decl_specifier_seq(), declarator(), function_body());
 *   - one per nonterminal whose positions hold nodes of several kinds (a
 *     sum: Declaration is a FunctionDefinition, a SimpleDeclaration, ...),
 *     with visit(), and a range of the elements for a list (X-seq, X-list);
 *   - SyntaxRef::name() per nonterminal: the node as that view.
 *
 * Handles.  ast::node (parse.h) is a node's index, 4 bytes, no context: what
 * a record or a table holds.  A view is a node and the context to read it
 * with (two registers): what a function takes and returns; a record never
 * holds one (syntax-audit.clang-tidy).  semantic_analysis::syntax(n) makes
 * a view of a stored node (or of a stored syntax_child), and a view
 * converts to its ast::node implicitly.  A node and a token (token_index)
 * are different types: neither converts to an integer, or to the other,
 * without saying so.
 *
 * An empty view (the node is of another kind, or absent) answers every
 * question with "none" (no child, false), so a chain needs no test between
 * its steps.  What the grammar does not tell (questions about tokens, a
 * position two nonterminals of one kind share) is hand-written below, in
 * SyntaxExtra<View>, which each generated view derives from.
 */
#ifndef VCC_CPLUSPLUS_SYNTAX_H
#define VCC_CPLUSPLUS_SYNTAX_H

#include "common/fail.h"
#include "frontend/cplusplus/parse.h"

namespace cplusplus
{

/* what a view reads: the tree, its tokens, and the parser's keyword
   table (parser::keyword_of) */
struct SyntaxContext {
    SyntaxContext(const syntax_tree &tree, token_stream &tokens,
                  const parser &p)
        : tree(tree), tokens(tokens), p(p), nodes(tree.node_data()),
          child_lists(tree.child_data())
    {
    }
    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
    /* the tree's two arrays, read directly (the tree is complete when a
       context is made: it must not grow while one is in use) */
    const syntax_node *nodes;
    const syntax_child *child_lists;
};

/* whether a view of type From converts to the sum To (syntax-views.inc) */
template <class From, class To> inline constexpr bool syntax_converts = false;

/* whether kind k is in the set of kinds bits (one bit per syntax_kind) */
inline bool syntax_kind_in(syntax_kind k, const uint64_t *bits)
{
    uint32_t i = static_cast<uint32_t>(k);
    return (bits[i / 64] >> (i % 64)) & 1;
}

#define SYNTAX_VIEWS_DECLARE
#include "frontend/cplusplus/syntax-views.inc"
#undef SYNTAX_VIEWS_DECLARE

class SyntaxElement;
class SyntaxElements;

class SyntaxRef
{
  public:
    SyntaxRef() = default;
    SyntaxRef(const SyntaxContext &cx, syntax_child c) : cx(&cx)
    {
        if (!c.is_token())
            n = node_index(c.index());
    }
    SyntaxRef(const SyntaxContext &cx, node_index n) : cx(&cx), n(n)
    {
    }

    /* false for an empty view, or a token */
    explicit operator bool() const
    {
        return n != 0;
    }
    /* the node (0 for an empty view): a view goes wherever a node_index
       does, so a function can take a view before its callees do */
    operator node_index() const
    {
        return n;
    }
    node_index index() const
    {
        return n;
    }
    syntax_child child() const
    {
        if (!n)
            return {};
        return syntax_child::node(n);
    }
    const SyntaxContext &context() const
    {
        return *cx;
    }
    syntax_kind kind() const
    {
        return node().kind;
    }
    uint32_t size() const
    {
        if (!n)
            return 0;
        return node().child_count;
    }
    /* the node's tokens: [first_token(), end_token()) */
    token_index first_token() const
    {
        return node().first_token;
    }
    token_index end_token() const
    {
        return node().end_token;
    }
    /* the i-th child, a node or a token; {} past the end */
    SyntaxElement operator[](uint32_t i) const;
    /* the last child ({} without one) */
    SyntaxElement back() const;
    /* every child, nodes and tokens, in order */
    SyntaxElements elements() const;
    /* the children from the first-th on: for (auto c : x.elements(1)) */
    SyntaxElements elements(uint32_t first) const;
    bool is(syntax_kind k) const
    {
        return n && node().kind == k;
    }
    /* whether the node is of one of the kinds */
    template <class... K>
    bool is(syntax_kind k, syntax_kind k2, K... more) const
    {
        return is(k) || is(k2, more...);
    }
    /* whether child c is a node of kind k */
    bool is(syntax_child c, syntax_kind k) const
    {
        return !c.is_token() && c.index() && cx->nodes[c.index()].kind == k;
    }
    /* whether child c is the keyword k */
    bool is_keyword(syntax_child c, standard_keyword k) const
    {
        return c.is_token() && cx->p.keyword_of(cx->tokens[c.index()]) == k;
    }
    bool is_punctuator(syntax_child c, preprocessing_op_or_punc op) const
    {
        if (!c.is_token())
            return false;
        const token &t = cx->tokens[c.index()];
        return t.kind == token_kind::operator_or_punctuator &&
               preprocessing_op_or_punc_primary(t.op_or_punc) == op;
    }

#define SYNTAX_VIEWS_ACCESSORS
#include "frontend/cplusplus/syntax-views.inc"
#undef SYNTAX_VIEWS_ACCESSORS

  protected:
    const syntax_node &node() const
    {
        return cx->nodes[n.index()];
    }
    const syntax_child *children() const
    {
        return cx->child_lists + cx->nodes[n.index()].first_child;
    }
    /* this node as view T, {} if it is of another kind */
    template <class T> T as() const
    {
        if (!n || !T::accepts(node().kind))
            return {};
        return T(*cx, n);
    }
    /* the first child that is a node of view T, {} if none */
    template <class T> T first() const
    {
        for (uint32_t i = 0; i < size(); i++) {
            syntax_child c = children()[i];
            if (!c.is_token() && c.index() &&
                T::accepts(cx->nodes[c.index()].kind))
                return T(*cx, node_index(c.index()));
        }
        return {};
    }
    /* the first child that is a token of kind k, {} if none */
    SyntaxElement token_of_kind(token_kind k) const;
    const SyntaxContext *cx = nullptr;
    node_index n = 0;
};

/*
 * A child: a node, whose view this is (every accessor of SyntaxRef), or a
 * token, for which the view is empty and the questions below answer.
 */
class SyntaxElement : public SyntaxRef
{
  public:
    SyntaxElement() = default;
    SyntaxElement(const SyntaxContext &cx, syntax_child c)
        : SyntaxRef(cx, c), c(c)
    {
    }
    /* false for no child at all (a token is one) */
    explicit operator bool() const
    {
        return c.bits != 0;
    }
    operator syntax_child() const
    {
        return c;
    }
    bool is_token() const
    {
        return c.is_token();
    }
    bool is_node() const
    {
        return c.bits && !c.is_token();
    }
    /* the node (0 for a token: read a token with token()).  With
       -DVCC_CHECK_SYNTAX_VIEWS it is a bug to ask a token for its node
       index, which would silently be 0 where the old child index was the
       token's */
#ifdef VCC_CHECK_SYNTAX_VIEWS
    node_index index(const char *file = __builtin_FILE(),
                     int line = __builtin_LINE()) const
    {
        if (c.is_token())
            vbug(file, line, "SyntaxElement::index() of a token: use token()");
        return SyntaxRef::index();
    }
#else
    node_index index() const
    {
        return SyntaxRef::index();
    }
#endif
    operator node_index() const
    {
        return SyntaxRef::index();
    }
    /* the token (0 for a node) */
    token_index token() const
    {
        if (!c.is_token())
            return 0;
        return c.index();
    }
    using SyntaxRef::is;
    bool is_identifier() const
    {
        return c.is_token() &&
               cx->tokens[c.index()].kind == token_kind::identifier;
    }
    bool is(preprocessing_op_or_punc op) const
    {
        return is_punctuator(c, op);
    }
    /* an attribute-specifier, its seq or an alignment-specifier */
    bool is_attribute() const
    {
        return is(syntax_kind::attribute_specifier,
                  syntax_kind::attribute_specifier_seq,
                  syntax_kind::alignment_specifier);
    }
    bool is(standard_keyword k) const
    {
        return is_keyword(c, k);
    }

  private:
    syntax_child c = {};
};

/* the children of a node, nodes and tokens, as SyntaxElement */
class SyntaxElements
{
  public:
    class iterator
    {
      public:
        iterator(const SyntaxContext *cx, const syntax_child *at)
            : cx(cx), at(at)
        {
        }
        SyntaxElement operator*() const
        {
            return SyntaxElement(*cx, *at);
        }
        iterator &operator++()
        {
            at++;
            return *this;
        }
        bool operator!=(const iterator &o) const
        {
            return at != o.at;
        }

      private:
        const SyntaxContext *cx;
        const syntax_child *at;
    };
    SyntaxElements() = default;
    SyntaxElements(const SyntaxContext &cx, const syntax_child *first,
                   uint32_t count)
        : cx(&cx), first(first), count(count)
    {
    }
    iterator begin() const
    {
        return iterator(cx, first);
    }
    iterator end() const
    {
        return iterator(cx, first + count);
    }

  private:
    const SyntaxContext *cx = nullptr;
    const syntax_child *first = nullptr;
    uint32_t count = 0;
};

inline SyntaxElement SyntaxRef::operator[](uint32_t i) const
{
    if (i >= size())
        return {};
    return SyntaxElement(*cx, children()[i]);
}

inline SyntaxElement SyntaxRef::back() const
{
    if (!size())
        return {};
    return (*this)[size() - 1];
}

inline SyntaxElement SyntaxRef::token_of_kind(token_kind k) const
{
    for (uint32_t i = 0; i < size(); i++) {
        syntax_child c = children()[i];
        if (c.is_token() && cx->tokens[c.index()].kind == k)
            return SyntaxElement(*cx, c);
    }
    return {};
}

inline SyntaxElements SyntaxRef::elements() const
{
    if (!n)
        return {};
    return SyntaxElements(*cx, children(), size());
}

inline SyntaxElements SyntaxRef::elements(uint32_t first) const
{
    if (!n || first >= size())
        return {};
    return SyntaxElements(*cx, children() + first, size() - first);
}

/* the node children of a node, each as a view of type T (tokens are
   skipped), or one node as the only element: for (auto d : seq.declarations()) */
template <class T> class SyntaxRange
{
  public:
    class iterator
    {
      public:
        iterator(const SyntaxContext *cx, const syntax_child *at,
                 const syntax_child *end)
            : cx(cx), at(at), end(end)
        {
            skip();
        }
        T operator*() const
        {
            return T(*cx, at->node());
        }
        iterator &operator++()
        {
            at++;
            skip();
            return *this;
        }
        bool operator!=(const iterator &o) const
        {
            return at != o.at;
        }

      private:
        void skip()
        {
            while (at != end && (at->is_token() || !at->index()))
                at++;
        }
        const SyntaxContext *cx;
        const syntax_child *at;
        const syntax_child *end;
    };
    SyntaxRange() = default;
    SyntaxRange(const SyntaxContext &cx, const syntax_child *first,
                uint32_t count)
        : cx(&cx), first(first), count(count)
    {
    }
    /* the one element c */
    SyntaxRange(const SyntaxContext &cx, syntax_child c)
        : cx(&cx), single(c), count(1)
    {
    }
    iterator begin() const
    {
        const syntax_child *at = start();
        return iterator(cx, at, at + count);
    }
    iterator end() const
    {
        const syntax_child *at = start();
        return iterator(cx, at + count, at + count);
    }

  private:
    const syntax_child *start() const
    {
        if (first)
            return first;
        return &single;
    }
    const SyntaxContext *cx = nullptr;
    const syntax_child *first = nullptr;
    syntax_child single = {};
    uint32_t count = 0;
};

/* one callable from several lambdas, for visit:
   d.visit(Overloaded{[&](FunctionDefinition f) {...},
                      [&](Declaration other) {...}}) */
template <class... F> struct Overloaded : F... {
    using F::operator()...;
};
template <class... F> Overloaded(F...) -> Overloaded<F...>;

/* f(v), or, when no case takes v's view alone (a case of a sum v is in
   and the visitor's default both convert it), f of the narrowest of the
   sums Sums (v's, narrowest first) a case takes: the most specific case */
template <class S, class... Rest, class F, class V>
decltype(auto) syntax_call_sum(F &f, V v)
{
    if constexpr (requires { f(S(v)); } || sizeof...(Rest) == 0)
        return f(S(v));
    else
        return syntax_call_sum<Rest...>(f, v);
}
template <class... Sums, class F, class V> decltype(auto) syntax_call(F &f, V v)
{
    if constexpr (requires { f(v); } || sizeof...(Sums) == 0)
        return f(v);
    else
        return syntax_call_sum<Sums...>(f, v);
}

/* a member function of self as a callable for visit */
template <class T, class R, class A>
auto bind_member(T *self, R (T::*member)(A))
{
    return [self, member](A a) { return (self->*member)(a); };
}

/*
 * What a view adds to the generated one: SyntaxExtra<View> is its base.
 * A sum's visit calls f with the view of the alternative the node is, or
 * with the sum itself for a node the grammar does not place there
 * ((unparsed), (erroneous)).  Each alternative's view converts to the sum,
 * so a case taking the sum is the visitor's default; a visitor without one
 * needs a case for every alternative, or it does not compile.
 */
template <class View> struct SyntaxExtra : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    SyntaxExtra() = default;
    SyntaxExtra(const SyntaxRef &r) : SyntaxRef(r)
    {
    }

  protected:
    /* the view made from a node (or an element, or another view) that is
       not of its kinds is empty; with -DVCC_CHECK_SYNTAX_VIEWS it is a
       bug at the caller's line: a view is asked for where a node of its
       kind is known, and x.kind_name() tests */
    void narrow(bool accepted, const char *file, int line)
    {
        if (accepted)
            return;
#ifdef VCC_CHECK_SYNTAX_VIEWS
        vbug(file, line, "a node converted to a view of another kind");
#endif
        n = 0;
    }
};

/*
 * [dcl.fct.def.general]/1:
 *
 *     function-body:
 *         ctor-initializer_opt compound-statement
 *         function-try-block
 *         = default ;
 *         = delete ;
 *
 * with [dcl.fct.def.delete]'s = delete ( string-literal ) ; and, in the
 * first pass over a member-specification, a body kept as its tokens
 * (unparsed).
 */
template <> struct SyntaxExtra<FunctionBody> : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    SyntaxExtra() = default;
    SyntaxExtra(const SyntaxRef &r) : SyntaxRef(r)
    {
    }
    void narrow(bool accepted, const char *file, int line)
    {
        if (accepted)
            return;
#ifdef VCC_CHECK_SYNTAX_VIEWS
        vbug(file, line, "a node converted to a view of another kind");
#endif
        n = 0;
    }
    bool unparsed() const
    {
        return is(syntax_kind::function_body) && size() == 1 &&
               (*this)[0].is(syntax_kind::unparsed);
    }
    bool defaulted() const
    {
        return is(syntax_kind::function_body) && size() == 3 &&
               (*this)[0].is(preprocessing_op_or_punc::EQUAL) &&
               (*this)[1].is(standard_keyword::default_);
    }
    bool deleted() const
    {
        return is(syntax_kind::function_body) && size() >= 3 &&
               (*this)[0].is(preprocessing_op_or_punc::EQUAL) &&
               (*this)[1].is(standard_keyword::delete_);
    }
    /* ctor-initializer_opt compound-statement: the compound-statement
       ({} for another form) and the ctor-initializer ({} if none) */
    SyntaxElement compound_statement() const
    {
        if (!is(syntax_kind::function_body))
            return {};
        auto last = back();
        if (!last.is(syntax_kind::compound_statement) || size() > 2 ||
            (size() == 2 && !(*this)[0].is(syntax_kind::ctor_initializer)))
            return {};
        return last;
    }
    SyntaxElement ctor_initializer() const
    {
        if (!compound_statement() || size() != 2)
            return {};
        return (*this)[0];
    }
    /* a compound-statement alone: what analysis handles for any function
       (a ctor-initializer is a constructor's) */
    bool plain() const
    {
        return compound_statement() && size() == 1;
    }
};

/*
 * [dcl.fct.def.general]/1:
 *
 *     function-definition:
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator
 *             virt-specifier-seq_opt function-body
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator
 *             requires-clause function-body
 *
 * ([ext:gnu_attributes] may come first too.)
 */
template <> struct SyntaxExtra<FunctionDefinition> : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    SyntaxExtra() = default;
    SyntaxExtra(const SyntaxRef &r) : SyntaxRef(r)
    {
    }
    void narrow(bool accepted, const char *file, int line)
    {
        if (accepted)
            return;
#ifdef VCC_CHECK_SYNTAX_VIEWS
        vbug(file, line, "a node converted to a view of another kind");
#endif
        n = 0;
    }
    /* the declarator after the decl-specifier-seq ({} without one; a
       declarator that is only its identifier is that token) */
    SyntaxElement declarator() const
    {
        for (uint32_t i = 0; i + 1 < size(); i++)
            if ((*this)[i].is(syntax_kind::decl_specifier_seq))
                return (*this)[i + 1];
        return {};
    }
    /* decl-specifier-seq declarator function-body and nothing after the
       decl-specifier-seq: no virt-specifier-seq, requires-clause or
       function-try-block (what comes before it is the caller's) */
    bool plain() const
    {
        return size() >= 3 &&
               (*this)[size() - 3].is(syntax_kind::decl_specifier_seq) &&
               (*this)[size() - 1].is(syntax_kind::function_body);
    }
};

/*
 * [dcl.decl.general]/5, [dcl.name]/1:
 *
 *     noptr-declarator:
 *         declarator-id attribute-specifier-seq_opt
 *         noptr-declarator parameters-and-qualifiers
 *         noptr-declarator [ constant-expression_opt ]
 *             attribute-specifier-seq_opt
 *         ( ptr-declarator )
 *
 * and noptr-abstract-declarator alike: the parser makes one node of a head
 * (the parenthesized declarator, or ..._opt and the declarator-id) and the
 * suffixes after it.
 */
struct NoptrDeclaratorParts : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    /* ( ptr-declarator ): the declarator in the parentheses */
    SyntaxElement parenthesized() const
    {
        if (!(*this)[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
            !(*this)[2].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return {};
        return (*this)[1];
    }
    /* the ... of a parameter pack's declarator */
    SyntaxElement ellipsis() const
    {
        if (!(*this)[0].is(preprocessing_op_or_punc::ELLIPSIS))
            return {};
        return (*this)[0];
    }
    /* what follows the ..._opt when it is not a suffix or parenthesis:
       the declarator-id (an identifier, a qualified-id, ...) */
    SyntaxElement declarator_id() const
    {
        uint32_t at = 0;
        if (ellipsis())
            at = 1;
        auto id = (*this)[at];
        if (!id || id.is(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
            id.is(preprocessing_op_or_punc::LEFT_BRACKET) ||
            id.is(syntax_kind::parameters_and_qualifiers))
            return {};
        return id;
    }
};

#define SYNTAX_VIEWS_DEFINE
#include "frontend/cplusplus/syntax-views.inc"
#undef SYNTAX_VIEWS_DEFINE

} // namespace cplusplus

#endif
