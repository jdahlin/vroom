// Base classes (Itanium ABI): empty bases at offset 0, a POD base's size against a non-POD base's
// data size (its tail padding reused: user constructor, member initializer, mixed access), several
// bases, an instance as a base.  Layouts checked by clang.
namespace n {
struct E {};                                  // empty
struct E2 {};
struct P { int i; char c; };                  // POD: size 8, its tail padding not reused
struct NP { NP(); int i; char c; };           // not POD (user constructor): tail padding reused
struct NI { int i = 0; char c; };             // not POD (default member initializer)
struct MA { int i; private: char c; };        // not standard-layout (mixed access)
struct DE : E { int x; };                     // empty base: x at 0
struct DEE : E, E2 { int x; };                // two distinct empty bases
struct DP : P { char d; };                    // after P's size: 12
struct DNP : NP { char d; };                  // in NP's tail padding: 8
struct DNI : NI { char d; };
struct DMA : MA { char d; };
struct DD : DNP { short s; };
struct M2 : P, NP { char z; };                // two non-empty bases
struct OnlyBase : P {};                       // no members of its own
struct Deep : OnlyBase { char q; };
class C : public NP { char p; public: int k; };
template <class T> struct holder { T v; };
struct DH : holder<char>, E { char w; };      // an instance as a base
}
n::DE a1; n::DEE a2; n::DP a3; n::DNP a4; n::DNI a5; n::DMA a6; n::DD a7; n::M2 a8; n::Deep a9; n::C a10; n::DH a11;
