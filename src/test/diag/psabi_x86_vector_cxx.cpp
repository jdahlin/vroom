/* diag-mode: -c */
/* diag-flags: --target=x86_64-linux-gnu -std=c++17 */
/* -Wpsabi in C++: an out-of-line member at its definition, templates,
   member, qualified, virtual and constructor calls. */
typedef int v8 __attribute__((vector_size(32)));

struct S {
    v8 member(v8 a);
    static v8 in_class(v8);
    virtual v8 over(v8);
};
v8 S::member(v8 a) { return a; }

void calls(S &s, v8 *p)
{
    *p = s.member(*p);
    *p = S::in_class(*p);
    *p = s.over(*p);
}

template <class T> T identity(T a) { return a; }
void instantiates(v8 *p) { *p = identity(*p); }

inline v8 shared(v8 a) { return a; }
void uses_inline(v8 *p) { *p = shared(*p); }

struct Holder {
    Holder(v8 v) : m(v) {}
    v8 m;
};
void constructs(v8 *p) { Holder h(*p); }

/* nothing: internal linkage, through a pointer */
namespace
{
v8 internal(v8 a) { return a; }
} // namespace
void silent(v8 *p, v8 (*f)(v8)) { *p = internal(*p); *p = f(*p); }
