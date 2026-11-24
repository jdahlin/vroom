// [expr.static.cast]/11, [expr.const]/5.7: a static_cast down to a class the
// complete object is not (undefined behavior) is not a constant expression;
// one to the class the object really is, is.
struct A { int x; };
struct B : A { int y; };
constexpr A a = {1};
constexpr B b = {{2}, 3};
constexpr const B &r1 = static_cast<const B &>(a); // expected-error {{constexpr variable 'r1' must be initialized by a constant expression}} expected-note {{cannot cast object of dynamic type 'const A' to type 'const B'}}
constexpr const B &r2 = static_cast<const B &>(static_cast<const A &>(b));
constexpr const B *p1 = static_cast<const B *>(&a); // expected-error {{constexpr variable 'p1' must be initialized by a constant expression}} expected-note {{cannot cast object of dynamic type 'const A' to type 'const B'}}
constexpr const B *p2 = static_cast<const B *>(static_cast<const A *>(&b));
