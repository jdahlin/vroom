// a class whose only virtual function is its destructor (its members can be named and called), a C
// function brought into a namespace by a using-declaration, inline members defined outside their class
// (weak: emitted where used, as clang's)
extern "C" int abs(int);
namespace lib { using ::abs; }
struct Base { virtual ~Base(); int flags = 3; int get() const; void set(int f); };
Base::~Base() {}
inline int Base::get() const { return flags; }
inline void Base::set(int f) { flags = f; }
int main()
{
	Base b;
	b.set(lib::abs(-5));
	return b.get() == 5 ? 0 : 1;
}
