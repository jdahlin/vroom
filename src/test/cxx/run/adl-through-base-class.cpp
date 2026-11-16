// p == q of a class derived from one in another namespace: the base's namespace is searched for the
// operator ([basic.lookup.argdep]/3; nlohmann's json_reverse_iterator, derived from std::reverse_iterator)
namespace ns {
template <class I> struct RI { I cur; I base() const { return cur; } };
template <class I1, class I2> bool operator==(const RI<I1> &x, const RI<I2> &y) { return x.base() == y.base(); }
template <class I1, class I2> bool operator!=(const RI<I1> &x, const RI<I2> &y) { return !(x == y); }
struct Plain { int v; };
int twice(const Plain &p) { return p.v * 2; }
}
template <class B> struct JR : ns::RI<B> { JR(B b) : ns::RI<B>{b} {} };
struct Mine : ns::Plain { Mine(int v) : ns::Plain{v} {} };
int main()
{
	int a[2];
	JR<int *> p(a), q(a), r(a + 1);
	return p == q && p != r && twice(Mine(4)) == 8 ? 0 : 1;
}
