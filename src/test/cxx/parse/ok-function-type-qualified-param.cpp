// void(N::E *): a function type whose parameter type is qualified, not the
// nested declarator (N::E) (Skia's GlyphVector.h: using Releaser = void(std::byte*))
namespace N { enum class E : unsigned char {}; struct S { using T = E; }; }
using R1 = void(N::E *);
using R2 = void(N::S::T &);
using R3 = int(N::S);
R1 *f;
