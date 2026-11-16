// n::Concept<args> auto: a qualified type-constraint on a placeholder (Skia's
// DrawAtlas.h: void visitRecords(std::invocable<Record> auto &&fn) const)
#include <concepts>
namespace n { template <class T, class U> concept C = true; template <class T> concept D = true; }
struct R {};
struct V {
    void visit(std::invocable<R> auto &&fn) const;
    void g(n::C<int> auto &&x, ::n::D auto y);
};
n::D auto h() { return 1; }
namespace m { struct S {}; } m::S *s; // (a qualified type, no constraint)
