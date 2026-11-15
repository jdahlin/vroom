// class X; declares X in its own scope even when an enclosing one has an X
// (Skia: SkSL::Program and SkSL::RP::Program); friend class X; still finds it
namespace A { struct P; }
namespace A::B { class P; }
namespace A { namespace C { class P; } }
const A::B::P *g;
A::C::P *h;
A::P *k;
namespace A { struct Q; struct R { friend class Q; }; }
A::Q *q;
