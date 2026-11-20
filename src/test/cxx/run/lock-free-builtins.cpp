// std: c++20
// __atomic_always_lock_free, __atomic_is_lock_free, __c11_atomic_is_lock_free and
// __builtin_constant_p (clang's Builtins.td; libc++ <__atomic/is_always_lock_free.h> and
// <__utility/is_pointer_in_range.h>): bool and int results, constants in constant expressions
template <class T> struct always {
	static const bool value = __atomic_always_lock_free(sizeof(T), nullptr);
};
struct three {
	char c[3];
};
struct eight {
	long a;
};
struct big {
	char c[32];
};
static_assert(__atomic_always_lock_free(1, 0));
static_assert(__atomic_always_lock_free(2, 0));
static_assert(__atomic_always_lock_free(4, nullptr));
static_assert(__atomic_always_lock_free(8, 0));
static_assert(!__atomic_always_lock_free(3, 0));
static_assert(!__atomic_always_lock_free(32, 0));
static_assert(always<char>::value);
static_assert(always<eight>::value);
static_assert(!always<three>::value);
static_assert(!always<big>::value);
static_assert(__c11_atomic_is_lock_free(4));
static_assert(__atomic_is_lock_free(8, 0));
static_assert(__atomic_is_lock_free(4, nullptr));
static_assert(__builtin_constant_p(1));
static_assert(__builtin_constant_p(1.5));
static_assert(__builtin_constant_p(sizeof(int) + 2));
int main()
{
	int x = 0;
	if (!__atomic_is_lock_free(sizeof(int), 0))
		return 1;
	bool b = __atomic_always_lock_free(sizeof(long), 0);
	if (!b || !always<int>::value)
		return 3;
	if (!__builtin_constant_p(4))
		return 4;
	if (__builtin_constant_p(x) && x != 0)
		return 5;
	return 0;
}
