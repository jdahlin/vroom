/* D5.5: C23 'auto' infers the type from the initializer (as __auto_type), also with another
   storage class; without one it is clang's error; implicit int has C23's wording */
/* diag-flags: -std=c23 */
auto g = 2.5;
static x = 1;
int f(void)
{
	auto i = 1;
	static auto s = 3u;
	auto int ai = 2;
	auto y;
	_Static_assert(_Generic(i, int: 1), "");
	_Static_assert(_Generic(g, double: 1), "");
	_Static_assert(_Generic(s, unsigned: 1), "");
	return i + ai + x + (int)s;
}
