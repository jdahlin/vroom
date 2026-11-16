// [class.inhctor.init] Example 1 (paragraph 1), docs/cxx-spec/class/class.inhctor.init.md
// needs: P7
// status: annotated

struct B1 {
  B1(int, ...) { }
};

struct B2 {
  B2(double) { }
};

int get();

struct D1 : B1 {
  using B1::B1;     // inherits B1(int, ...)
  int x;
  int y = get();
};

void test() {
  D1 d(2, 3, 4);    // OK: B1 is initialized by calling B1(2, 3, 4),
                    // then d.x is default-initialized (no initialization is performed),
                    // then d.y is initialized by calling get()
  D1 e;             // error: D1 has a deleted default constructor // semantic
}

struct D2 : B2 {
  using B2::B2;
  B1 b;
};

D2 f(1.0);          // error: B1 has a deleted default constructor // semantic

struct W { W(int); };
struct X : virtual W { using W::W; X() = delete; };
struct Y : X { using X::X; };
struct Z : Y, virtual W { using Y::Y; };
Z z(0);             // OK: initialization of Y does not invoke default constructor of X

template<class T> struct Log : T {
  using T::T;       // inherits all constructors from class T
  ~Log() { std::clog << "Destroying wrapper" << std::endl; }
};
