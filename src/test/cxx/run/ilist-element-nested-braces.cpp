// m = {{"k", {1, 2, 3}}, ...}: an element of an initializer_list that is a braced list with a braced list
// argument of its own (json::object_t o = {{"array", {1, 2, 3, 4}}}: pair's second member from the inner list)
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
public:
	typedef E value_type;
	constexpr const E *begin() const { return __begin_; }
	constexpr unsigned long size() const { return __size_; }
};
}
struct V { int n, sum; V(std::initializer_list<int> l) : n((int)l.size()), sum(0) { for (unsigned long i = 0; i < l.size(); i++) sum += l.begin()[i]; } V(int x) : n(1), sum(x) {} };
struct P { const char *k; V v; P(const char *k, const V &v) : k(k), v(v) {} };
struct M { int n, total; M(std::initializer_list<P> l) : n((int)l.size()), total(0) { for (unsigned long i = 0; i < l.size(); i++) total += l.begin()[i].v.n * 100 + l.begin()[i].v.sum; } };
int main()
{
	M m = { {"array", {1, 2, 3, 4}}, {"one", 7} };
	return m.n == 2 && m.total == 410 + 107 ? 0 : 1;
}
