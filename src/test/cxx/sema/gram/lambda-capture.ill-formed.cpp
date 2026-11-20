// Lambda captures that are ill-formed, one function per rule, each with a
// well-formed near miss ([expr.prim.lambda.capture]/2-/5, /7, /10;
// [basic.def.odr]/9; [expr.prim.this]): bench/cxx-sema.py compares each
// verdict with clang's.
struct S {
    int m;
    static int s;
    void ok_this() { auto l = [this] { return m; }; (void)l; }
    void ok_star() { auto l = [*this] { return m; }; (void)l; }
    void ok_default() { auto l = [=] { return m; }; (void)l; }
    void bad_this_twice() { auto l = [this, this] { return m; }; (void)l; }
    void bad_star_and_this() { auto l = [this, *this] { return m; }; (void)l; }
    static void bad_this_static() { auto l = [this] { return 0; }; (void)l; }
};
void ok_copy() { int a = 1; auto l = [a] { return a; }; (void)l; }
void ok_ref() { int a = 1; auto l = [&a] { return a; }; (void)l; }
void ok_default_and_ref() { int a = 1, b = 2; auto l = [=, &b] { return a + b; }; (void)l; }
void ok_default_ref_and_copy() { int a = 1, b = 2; auto l = [&, b] { return a + b; }; (void)l; }
void bad_default_copy_and_copy() { int a = 1; auto l = [=, a] { return a; }; (void)l; }
void bad_default_ref_and_ref() { int a = 1; auto l = [&, &a] { return a; }; (void)l; }
void bad_twice() { int a = 1; auto l = [a, a] { return a; }; (void)l; }
void bad_twice_ref() { int a = 1; auto l = [a, &a] { return a; }; (void)l; }
void bad_no_capture() { int a = 1; auto l = [] { return a; }; (void)l; }
void bad_no_capture_nested() { int a = 1; auto l = [=] { return [] { return a; }(); }; (void)l; }
void bad_not_local() { static int a = 1; auto l = [a] { return a; }; (void)l; }
void bad_not_local_global() { auto l = [bad_twice] { return 0; }; (void)l; }
void bad_parameter_same_name() { int a = 1; auto l = [a](int a) { return a; }; (void)l; }
void ok_not_odr_use() { const int k = 1; auto l = [] { return k; }; (void)l; }
void ok_sizeof() { int a = 1; auto l = [] { return sizeof(a); }; (void)l; }
void ok_decltype() { int a = 1; auto l = [] { decltype(a) b = 2; return b; }; (void)l; }
void bad_odr_use_of_constant() { const int k = 1; auto l = [] { return &k; }; (void)l; }
void ok_init_capture() { int a = 1; auto l = [b = a] { return b; }; (void)l; }
void bad_init_capture_twice() { int a = 1; auto l = [b = a, b = a] { return b; }; (void)l; }
void bad_init_capture_parameter() { int a = 1; auto l = [b = a](int b) { return b; }; (void)l; }
auto bad_default_at_namespace_scope = [=] { return 0; };
auto ok_namespace_scope = [] { return 0; };
void ok_bit_field() { struct B { int f : 3; } b{1}; auto l = [b] { return b.f; }; (void)l; auto m = [x = b.f] { return x; }; (void)m; }
void ok_structured() { struct P { int x, y; } p{1, 2}; auto [x, y] = p; auto l = [x, y] { return x + y; }; (void)l; }
void bad_pack_not_pack() { int a = 1; auto l = [a...] { return a; }; (void)l; }
void ok_mutable() { int a = 1; auto l = [a]() mutable { return ++a; }; (void)l; }
void bad_modify_copy() { int a = 1; auto l = [a] { return ++a; }; (void)l; }
