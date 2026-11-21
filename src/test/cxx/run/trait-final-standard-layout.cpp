// [meta.unary.prop]: __is_final and __is_standard_layout; a class with a
// constructor template is still analysed for __is_trivially_copyable.
struct Plain { int a, b; };
struct Mixed { int a; private: int b; };
struct Base { int x; };
struct Ctor { template <class T> Ctor(T) {} int v; };

static_assert(__is_standard_layout(int));
static_assert(!__is_standard_layout(int &));
static_assert(__is_standard_layout(Plain));
static_assert(!__is_standard_layout(Mixed));
static_assert(!__is_final(Plain));
static_assert(__is_trivially_copyable(Ctor));

int main() { return 0; }
