enum Color { Red };
enum class Op : unsigned char { Add };
namespace n { enum class Level { Low }; struct S { enum Kind { A }; }; void f(Level, S::Kind, Color, Op) { } }
void g(Color, Op, n::Level, n::S::Kind, Color *, const Op &) { }
