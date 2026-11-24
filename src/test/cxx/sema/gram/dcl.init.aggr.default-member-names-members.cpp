// [dcl.init.aggr]/5.1, [class.mem]/6: a default member initializer that names other members of the aggregate being initialized: `this` is that object
struct S { int a; int b[2]; int d = b[a]; };
S ss{1, {2, 3}};
S tt{1, {2, 3}, 7};
struct T { int x; int y = x + 1; };
T t1{4};
void f() { S local{0, {5, 6}}; T t2{1}; }
