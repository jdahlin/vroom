// RUN: %clang_cc1 -std=c++17 -fsyntax-only %s
// [namespace.qual] with [temp.arg.explicit]: N::f<A>, a qualified
// template-id naming the one function template of the name, is its
// specialization, whose address instantiates it
namespace N {
template <typename T> T *one(T &x) { return x.missing(); } // expected-error {{no member named 'missing' in 'B'}}
template <typename T> T *two(T &x) { return &x; }
}
struct B {};
B *(*p1)(B &) = &N::two<B>;
B *(*p2)(B &) = &N::one<B>; // expected-note {{in instantiation}}
