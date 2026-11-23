/*
 * syntax.h: typed views of the syntax tree (parse.h's syntax_tree).
 *
 * The tree stores every node the same way: a kind, and its children in the
 * order of the alternative of the grammar that matched.  A view names a
 * node's parts after the standard's grammar instead of indexing children:
 *
 *     auto body = n.function_definition().function_body();
 *     if (body.deleted()) ...
 *
 * Nothing is stored in the tree or allocated for a view.  SyntaxRef is the
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
 * Handles.  A view is a node index (4 bytes) that reads through the
 * thread's current SyntaxContext (syntax_ambient, set by SyntaxScope while
 * a semantic_analysis, or the declarator claims, lives): a record holds
 * one, a function takes and returns one, nothing makes a view of a node
 * index.  ast::node (parse.h) is the raw index the parser and the tree
 * work with; a view converts to it and from it.  A node and a token
 * (token_index) are different types: neither converts to an integer, or to
 * the other, without saying so.
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

/* the context the views read with: the thread's current analysis' (a view
   carries no context of its own, so it is 4 bytes and can be stored) */
inline constinit thread_local const SyntaxContext *syntax_ambient = nullptr;

/* makes ctx the thread's context for as long as it lives */
struct SyntaxScope {
    explicit SyntaxScope(const SyntaxContext &ctx) : saved(syntax_ambient)
    {
        syntax_ambient = &ctx;
    }
    ~SyntaxScope()
    {
        syntax_ambient = saved;
    }
    SyntaxScope(const SyntaxScope &) = delete;
    SyntaxScope &operator=(const SyntaxScope &) = delete;

  private:
    const SyntaxContext *saved;
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
class SyntaxItems;

class SyntaxRef
{
  public:
    SyntaxRef() = default;
    SyntaxRef(syntax_child c)
    {
        if (!c.is_token())
            n = node_index(c.index());
    }
    /* a node of the thread's tree (or none: 0) */
    SyntaxRef(node_index n) : n(n)
    {
    }
    SyntaxRef(decltype(nullptr))
    {
    }

    friend bool operator==(const SyntaxRef &a, const SyntaxRef &b)
    {
        return a.n == b.n;
    }
    friend bool operator!=(const SyntaxRef &a, const SyntaxRef &b)
    {
        return a.n != b.n;
    }
    friend bool operator==(const SyntaxRef &a, node_index b)
    {
        return a.n == b;
    }
    friend bool operator==(node_index a, const SyntaxRef &b)
    {
        return a == b.n;
    }
    friend bool operator!=(const SyntaxRef &a, node_index b)
    {
        return a.n != b;
    }
    friend bool operator!=(node_index a, const SyntaxRef &b)
    {
        return a != b.n;
    }
    friend bool operator<(const SyntaxRef &a, const SyntaxRef &b)
    {
        return a.n < b.n;
    }
    friend bool operator>(const SyntaxRef &a, const SyntaxRef &b)
    {
        return a.n > b.n;
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
        return !c.is_token() && c.index() &&
               syntax_ambient->nodes[c.index()].kind == k;
    }
    /* whether child c is the keyword k */
    bool is_keyword(syntax_child c, standard_keyword k) const
    {
        return c.is_token() && syntax_ambient->p.keyword_of(
                                   syntax_ambient->tokens[c.index()]) == k;
    }
    bool is_punctuator(syntax_child c, preprocessing_op_or_punc op) const
    {
        if (!c.is_token())
            return false;
        const token &t = syntax_ambient->tokens[c.index()];
        return t.kind == token_kind::operator_or_punctuator &&
               preprocessing_op_or_punc_primary(t.op_or_punc) == op;
    }

#define SYNTAX_VIEWS_ACCESSORS
#include "frontend/cplusplus/syntax-views.inc"
#undef SYNTAX_VIEWS_ACCESSORS

  protected:
    const syntax_node &node() const
    {
        return syntax_ambient->nodes[n.index()];
    }
    const syntax_child *children() const
    {
        return syntax_ambient->child_lists +
               syntax_ambient->nodes[n.index()].first_child;
    }
    /* this node as view T, {} if it is of another kind */
    template <class T> T as() const
    {
        if (!n || !T::accepts(node().kind))
            return {};
        return T(n);
    }
    /* the first child that is a node of view T, {} if none */
    template <class T> T first() const
    {
        for (uint32_t i = 0; i < size(); i++) {
            syntax_child c = children()[i];
            if (!c.is_token() && c.index() &&
                T::accepts(syntax_ambient->nodes[c.index()].kind))
                return T(node_index(c.index()));
        }
        return {};
    }
    /* the first child that is a token of kind k, {} if none */
    SyntaxElement token_of_kind(token_kind k) const;
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
    /* a child of the thread's tree */
    SyntaxElement(syntax_child c) : SyntaxRef(c), c(c)
    {
    }
    /* a node as a child */
    SyntaxElement(const SyntaxRef &r) : SyntaxRef(r), c(r.child())
    {
    }
    /* a token as a child, to ask it what an element is asked */
    static SyntaxElement from_token(token_index t)
    {
        return SyntaxElement(syntax_child::token(t));
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
    /* the items of a list: the children of this node if it is of kind
       list, else this child alone; the commas between them left out:
       for (auto a : x.items(syntax_kind::initializer_list)) */
    SyntaxItems items(syntax_kind list) const;
    /* the keyword this child is: none for a node or another token */
    standard_keyword keyword() const
    {
        if (!c.is_token())
            return standard_keyword::none;
        return syntax_ambient->p.keyword_of(syntax_ambient->tokens[c.index()]);
    }
    bool is_identifier() const
    {
        return c.is_token() &&
               syntax_ambient->tokens[c.index()].kind == token_kind::identifier;
    }
    bool is(preprocessing_op_or_punc op) const
    {
        return is_punctuator(c, op);
    }
    /* whether the child is one of the punctuators */
    template <class... K>
    bool is(preprocessing_op_or_punc op, preprocessing_op_or_punc op2,
            K... more) const
    {
        return is(op) || is(op2, more...);
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
    /* whether the child is one of the keywords */
    template <class... K>
    bool is(standard_keyword k, standard_keyword k2, K... more) const
    {
        return is(k) || is(k2, more...);
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
        explicit iterator(const syntax_child *at) : at(at)
        {
        }
        SyntaxElement operator*() const
        {
            return SyntaxElement(*at);
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
        const syntax_child *at;
    };
    SyntaxElements() = default;
    SyntaxElements(const syntax_child *first, uint32_t count)
        : first(first), count(count)
    {
    }
    iterator begin() const
    {
        return iterator(first);
    }
    iterator end() const
    {
        return iterator(first + count);
    }

  private:
    const syntax_child *first = nullptr;
    uint32_t count = 0;
};

/* the children of a list, or the one child that is no list, without the
   commas that separate them (SyntaxElement::items) */
class SyntaxItems
{
  public:
    class iterator
    {
      public:
        iterator(const syntax_child *at, const syntax_child *end)
            : at(at), end(end)
        {
            skip();
        }
        SyntaxElement operator*() const
        {
            return SyntaxElement(*at);
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
            while (at != end &&
                   SyntaxElement(*at).is(preprocessing_op_or_punc::COMMA))
                at++;
        }
        const syntax_child *at;
        const syntax_child *end;
    };
    SyntaxItems() = default;
    SyntaxItems(const syntax_child *first, uint32_t count)
        : first(first), count(count)
    {
    }
    explicit SyntaxItems(syntax_child one) : single(one), count(1)
    {
    }
    iterator begin() const
    {
        const syntax_child *at = start();
        return iterator(at, at + count);
    }
    iterator end() const
    {
        const syntax_child *at = start();
        return iterator(at + count, at + count);
    }

  private:
    const syntax_child *start() const
    {
        if (first)
            return first;
        return &single;
    }
    const syntax_child *first = nullptr;
    syntax_child single = {};
    uint32_t count = 0;
};

/* a node's children from the first-th on (after its leading attributes):
   [] and size() count from there, as the grammar names the rest */
class SyntaxTail
{
  public:
    SyntaxTail(SyntaxRef node, uint32_t first) : node(node), first(first)
    {
    }
    SyntaxElement operator[](uint32_t i) const
    {
        return node[first + i];
    }
    uint32_t size() const
    {
        if (node.size() < first)
            return 0;
        return node.size() - first;
    }
    SyntaxElements elements() const
    {
        return node.elements(first);
    }

  private:
    SyntaxRef node;
    uint32_t first;
};

inline SyntaxItems SyntaxElement::items(syntax_kind list) const
{
    if (!c)
        return {};
    if (is(list))
        return SyntaxItems(children(), size());
    return SyntaxItems(c);
}

inline SyntaxElement SyntaxRef::operator[](uint32_t i) const
{
    if (i >= size())
        return {};
    return SyntaxElement(children()[i]);
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
        if (c.is_token() && syntax_ambient->tokens[c.index()].kind == k)
            return SyntaxElement(c);
    }
    return {};
}

inline SyntaxElements SyntaxRef::elements() const
{
    if (!n)
        return {};
    return SyntaxElements(children(), size());
}

inline SyntaxElements SyntaxRef::elements(uint32_t first) const
{
    if (!n || first >= size())
        return {};
    return SyntaxElements(children() + first, size() - first);
}

/* the node children of a node, each as a view of type T (tokens are
   skipped), or one node as the only element: for (auto d : seq.declarations()) */
template <class T> class SyntaxRange
{
  public:
    class iterator
    {
      public:
        iterator(const syntax_child *at, const syntax_child *end)
            : at(at), end(end)
        {
            skip();
        }
        T operator*() const
        {
            return T(at->node());
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
        const syntax_child *at;
        const syntax_child *end;
    };
    SyntaxRange() = default;
    SyntaxRange(const syntax_child *first, uint32_t count)
        : first(first), count(count)
    {
    }
    /* the one element c */
    explicit SyntaxRange(syntax_child c) : single(c), count(1)
    {
    }
    iterator begin() const
    {
        const syntax_child *at = start();
        return iterator(at, at + count);
    }
    iterator end() const
    {
        const syntax_child *at = start();
        return iterator(at + count, at + count);
    }

  private:
    const syntax_child *start() const
    {
        if (first)
            return first;
        return &single;
    }
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
/* what every SyntaxExtra<View> has: SyntaxRef's constructors, a view made
   from a node of any kind, and narrow(), which an empty view is made by when
   the node is of another kind (with -DVCC_CHECK_SYNTAX_VIEWS, a bug at the
   caller's line: a view is asked for where a node of its kind is known, and
   x.kind_name() tests) */
struct SyntaxExtraBase : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    SyntaxExtraBase() = default;
    SyntaxExtraBase(const SyntaxRef &r) : SyntaxRef(r)
    {
    }

  protected:
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

template <class View> struct SyntaxExtra : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
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
template <> struct SyntaxExtra<FunctionBody> : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
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
template <> struct SyntaxExtra<FunctionDefinition> : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
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

/*
 * [dcl.meaning]/1: "A declarator contains exactly one declarator-id; it
 * names the entity that is declared."  A declarator taken apart by its
 * syntax alone, without its meaning (which is semantic_analysis::declarator's):
 * the declarator-id, the noptr-declarator it heads and what wraps them.
 *
 * It reads through
 *
 *     init-declarator, member-declarator: the declarator (an initializer,
 *         virt-specifiers, a bit-field width or a requires-clause after
 *         it are not the declarator's)
 *     declarator: noptr-declarator parameters-and-qualifiers
 *         trailing-return-type ([dcl.decl.general]/4), the trailing one
 *     [ext:gnu_asm_labels] declarator gnu-asm-label_opt attributes
 *     ptr-declarator: ptr-operator ptr-declarator ([dcl.ptr], [dcl.ref],
 *         [dcl.mptr])
 *     noptr-declarator: ( ptr-declarator ) suffixes
 *
 * to the noptr-declarator whose head is the declarator-id, or to the
 * declarator-id itself when it stands alone (the parser makes no node for
 * a noptr-declarator that is only its declarator-id).  The abstract forms
 * are read alike, with no declarator-id.
 */
struct DeclaratorParts {
    /* the declarator-id: an identifier token, or a qualified-id,
       unqualified-id (~ class-name), simple-template-id,
       operator-function-id, literal-operator-id or conversion-function-id
       node; {} for an abstract declarator or what is no declarator */
    SyntaxElement id;
    /* the noptr-declarator (or noptr-abstract-declarator) whose head is the
       declarator-id or the ... of a pack, and whose suffixes are the first
       derivations of what is declared ([dcl.meaning]/3); {} when the
       declarator-id stands alone */
    SyntaxRef noptr;
    /* the ... of a parameter pack's declarator ([dcl.fct]/21) */
    SyntaxElement ellipsis;
    /* declarator's trailing-return-type ([dcl.fct]/2), {} without one */
    SyntaxElement trailing_return;
    /* no ptr-operator and no parentheses come between the declarator and
       its declarator-id: what it declares is the declarator-id's own
       noptr-declarator's derivations, then the decl-specifier-seq's type */
    bool direct = true;

    DeclaratorParts() = default;
    explicit DeclaratorParts(SyntaxElement d)
    {
        while (d) {
            if (d.is_token()) {
                if (d.is_identifier())
                    id = d;
                else if (d.is(preprocessing_op_or_punc::ELLIPSIS))
                    ellipsis = d;
                return;
            }
            switch (d.kind()) {
            case syntax_kind::init_declarator:
            case syntax_kind::member_declarator:
            case syntax_kind::gnu_declarator:
                d = d[0];
                continue;
            case syntax_kind::declarator:
            case syntax_kind::abstract_declarator:
                if (d.back().is(syntax_kind::trailing_return_type))
                    trailing_return = d.back();
                d = d[0];
                continue;
            case syntax_kind::ptr_declarator:
            case syntax_kind::ptr_abstract_declarator:
                /* ptr-operator ptr-declarator_opt */
                direct = false;
                if (d.size() != 2)
                    return;
                d = d[1];
                continue;
            case syntax_kind::noptr_declarator:
            case syntax_kind::noptr_abstract_declarator: {
                SyntaxRef node = d;
                NoptrDeclaratorParts parts(node);
                if (SyntaxElement inner = parts.parenthesized()) {
                    direct = false;
                    d = inner;
                    continue;
                }
                noptr = node;
                ellipsis = parts.ellipsis();
                id = parts.declarator_id();
                return;
            }
            case syntax_kind::qualified_id:
            case syntax_kind::unqualified_id:
            case syntax_kind::simple_template_id:
            case syntax_kind::operator_function_id:
            case syntax_kind::literal_operator_id:
            case syntax_kind::conversion_function_id:
                id = d;
                return;
            default:
                return;
            }
        }
    }
    /* the first declarator of a simple-declaration, member-declaration or
       function-definition (a template-declaration's or
       explicit-specialization's: its declaration's), taken apart; a
       declarator is taken apart as itself.  The decl-specifier-seq,
       attributes and function-body are not declarators, and a declarator
       that is only its identifier is that token ([dcl.decl.general]/1) */
    static DeclaratorParts of_declaration(SyntaxRef n)
    {
        while (n.is(syntax_kind::template_declaration,
                    syntax_kind::explicit_specialization) &&
               n.back().is_node())
            n = n.back();
        if (!n.is(syntax_kind::simple_declaration,
                  syntax_kind::member_declaration,
                  syntax_kind::function_definition))
            return DeclaratorParts(n);
        for (SyntaxElement c : n.elements()) {
            if (c.is(syntax_kind::init_declarator_list,
                     syntax_kind::member_declarator_list))
                return DeclaratorParts(c[0]);
            if (c.is_identifier())
                return DeclaratorParts(c);
            if (!c.is_node() || c.is(syntax_kind::decl_specifier_seq) ||
                c.is_attribute())
                continue;
            DeclaratorParts parts(c);
            if (parts.id)
                return parts;
        }
        return {};
    }
    /* the nested-name-specifier of a qualified declarator-id
       ([dcl.meaning]/2), {} for an unqualified one */
    SyntaxElement qualifier() const
    {
        if (!id.is(syntax_kind::qualified_id) ||
            !id[0].is(syntax_kind::nested_name_specifier))
            return {};
        return id[0];
    }
    /* the declarator-id without its nested-name-specifier (and the
       template keyword after it) */
    SyntaxElement unqualified() const
    {
        if (!qualifier())
            return id;
        return id.back();
    }
    /* the parameters-and-qualifiers right after the declarator-id (and its
       attributes): the declarator-id names a function ([dcl.fct]/1),
       whatever ptr-operators around it make of its return type; {} else */
    SyntaxElement parameters() const
    {
        if (!noptr)
            return {};
        uint32_t at = 0;
        if (ellipsis)
            at++;
        if (id)
            at++;
        while (noptr[at].is_attribute())
            at++;
        if (!noptr[at].is(syntax_kind::parameters_and_qualifiers))
            return {};
        return noptr[at];
    }
};

/*
 * [stmt.iter]/1:
 *
 *     iteration-statement:
 *         while ( condition ) statement
 *         do statement while ( expression ) ;
 *         for ( init-statement condition_opt ; expression_opt ) statement
 *         for ( init-statement_opt for-range-declaration : for-range-initializer ) statement
 *
 * (the init-statement ends with its own ;).  The grammar names each part
 * but the kinds of the condition and the expression overlap, so the
 * parts are named here by their position in each form.
 */
template <> struct SyntaxExtra<IterationStatement> : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
    /* the keyword the statement begins with: while, do or for */
    standard_keyword keyword() const
    {
        return (*this)[0].keyword();
    }
    /* the statement: the do's second child, else the last */
    SyntaxElement body() const
    {
        if (keyword() == standard_keyword::do_)
            return (*this)[1];
        return back();
    }
    /* while ( condition ) statement */
    SyntaxElement while_condition() const
    {
        return (*this)[2];
    }
    /* do statement while ( expression ) ; */
    SyntaxElement do_expression() const
    {
        return (*this)[4];
    }
    /* for ( init-statement condition_opt ; expression_opt ) statement */
    SyntaxElement for_init_statement() const
    {
        return (*this)[2];
    }
    SyntaxElement for_condition() const
    {
        auto c = (*this)[3];
        if (c.is(preprocessing_op_or_punc::SEMICOLON))
            return {};
        return c;
    }
    SyntaxElement for_expression() const
    {
        auto c = (*this)[after_for_condition()];
        if (c.is(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return {};
        return c;
    }
    /* whether the children are the for's: the ; after the condition_opt,
       then the expression_opt, the ) and the statement */
    bool for_shaped() const
    {
        uint32_t i = 3;
        if (!(*this)[i].is(preprocessing_op_or_punc::SEMICOLON))
            i++;
        if (!(*this)[i].is(preprocessing_op_or_punc::SEMICOLON))
            return false;
        i++;
        if (i < size() &&
            !(*this)[i].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            i++;
        return i + 2 == size();
    }

  private:
    /* the index after the ; that ends the condition_opt */
    uint32_t after_for_condition() const
    {
        uint32_t i = 3;
        if (!(*this)[i].is(preprocessing_op_or_punc::SEMICOLON))
            i++;
        return i + 1;
    }
};

/*
 * [expr.prim.id.qual]/1:
 *
 *     nested-name-specifier:
 *         ::
 *         type-name ::
 *         namespace-name ::
 *         decltype-specifier ::
 *         nested-name-specifier identifier ::
 *         nested-name-specifier template_opt simple-template-id ::
 *
 * The parser makes one node of a leading :: and each name with the :: after
 * it (the last one's may be missing in a node that is cut short).
 */
template <> struct SyntaxExtra<NestedNameSpecifier> : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
    /* a name, whether a template keyword came before it (before a
       simple-template-id), and whether the :: follows it */
    struct Part {
        SyntaxElement name;
        bool has_template;
        bool scoped;
    };
    class Parts
    {
      public:
        class iterator
        {
          public:
            iterator(SyntaxRef node, uint32_t at) : node(node), at(at)
            {
            }
            Part operator*() const
            {
                uint32_t i = at;
                bool has_template = template_before();
                if (has_template)
                    i++;
                return {node[i], has_template,
                        node[i + 1].is(preprocessing_op_or_punc::COLON_COLON)};
            }
            iterator &operator++()
            {
                at += 2;
                if (template_before())
                    at++;
                return *this;
            }
            /* (compared with end(): whether the names are not all seen) */
            bool operator!=(const iterator &) const
            {
                return at < node.size();
            }

          private:
            bool template_before() const
            {
                return node[at].is(standard_keyword::template_) &&
                       node[at + 1].is(syntax_kind::simple_template_id);
            }
            SyntaxRef node;
            uint32_t at;
        };
        Parts(SyntaxRef node, uint32_t first) : node(node), first(first)
        {
        }
        iterator begin() const
        {
            return iterator(node, first);
        }
        iterator end() const
        {
            return iterator(node, node.size());
        }

      private:
        SyntaxRef node;
        uint32_t first;
    };
    /* begins with the :: of the global namespace */
    bool global() const
    {
        return size() && (*this)[0].is(preprocessing_op_or_punc::COLON_COLON);
    }
    /* the names, each with whether its :: follows */
    Parts parts() const
    {
        if (global())
            return Parts(*this, 1);
        return Parts(*this, 0);
    }
};

/*
 * [over.oper.general]/1:
 *
 *     operator-function-id:
 *         operator operator
 *
 * where the operator is a punctuator, new or delete (with [ ] after it for
 * the array forms), or ( ) or [ ] (the second token of a pair is a child).
 */
template <> struct SyntaxExtra<OperatorFunctionId> : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
    /* the operator keyword: the name's token */
    SyntaxElement operator_keyword() const
    {
        return (*this)[0];
    }
    /* the operator: a punctuator, new or delete */
    SyntaxElement symbol() const
    {
        return (*this)[1];
    }
    /* new[] and delete[] */
    bool is_array_form() const
    {
        return size() == 4;
    }
};

/*
 * [over.literal]/1:
 *
 *     literal-operator-id:
 *         operator string-literal identifier
 *         operator user-defined-string-literal
 */
template <> struct SyntaxExtra<LiteralOperatorId> : SyntaxExtraBase {
    using SyntaxExtraBase::SyntaxExtraBase;
    SyntaxElement operator_keyword() const
    {
        return (*this)[0];
    }
    /* the string-literal (which has no characters), or the
       user-defined-string-literal that has the suffix after its quotes */
    SyntaxElement literal() const
    {
        return (*this)[1];
    }
    /* the suffix identifier of the first form ({} for the second) */
    SyntaxElement suffix_identifier() const
    {
        if (size() != 3)
            return {};
        return (*this)[2];
    }
};

#define SYNTAX_VIEWS_DEFINE
#include "frontend/cplusplus/syntax-views.inc"
#undef SYNTAX_VIEWS_DEFINE

} // namespace cplusplus

#endif
