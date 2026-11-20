// std: c++20
// clang's __builtin_operator_new and __builtin_operator_delete (libc++'s <__new/allocate.h>) call
// the replaceable global allocation and deallocation functions ([basic.stc.dynamic]/1), here
// from a function template reached through a member of a class template and through an
// enumeration's functional cast ([expr.type.conv]/2), as libc++'s allocator does
typedef decltype(sizeof(0)) size_t;
namespace std { enum class align_val_t : size_t {}; }
void *operator new(size_t, std::align_val_t);
void operator delete(void *, std::align_val_t) noexcept;
static int aligned_news;
static char arena[4096];
static size_t used;
void *operator new(size_t n)
{
	void *p = arena + used;
	used += (n + 15) & ~15ul;
	return p;
}
void operator delete(void *) noexcept { aligned_news += 1000; }
void *operator new(size_t n, std::align_val_t a)
{
	aligned_news++;
	void *p = __builtin_operator_new(n + 64);
	return (void *)(((unsigned long)p + 63) & ~63ul);
}
void operator delete(void *, std::align_val_t) noexcept { aligned_news += 100; }
enum class element_count : size_t {};
template <class T> T *allocate(element_count n, size_t align = alignof(T))
{
	size_t bytes = static_cast<size_t>(n) * sizeof(T);
	if (align > 16)
		return static_cast<T *>(__builtin_operator_new(bytes, static_cast<std::align_val_t>(align)));
	return static_cast<T *>(__builtin_operator_new(bytes));
}
template <class T> void deallocate(T *p)
{
	__builtin_operator_delete(p);
}
template <class T> struct A {
	T *get(size_t n) { return allocate<T>(element_count(n)); }
};
struct alignas(64) Big { char c[64]; };
int main()
{
	A<int> a;
	int *p = a.get(4);
	p[3] = 7;
	int r = p[3];
	deallocate(p);
	Big *b = allocate<Big>(element_count(1));
	if ((unsigned long)b % 64 || aligned_news != 1001)
		return 1;
	return r - 7;
}
