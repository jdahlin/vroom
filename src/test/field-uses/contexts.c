/* select: S.f */
struct S { int f; };
struct T { int f; };
int g(struct S *s)
{
	typeof(s->f) a = sizeof(s->f);
	_Static_assert(sizeof(((struct S *)0)->f) == 4, "");
	int b = _Generic(s->f, int: 1, default: 2);
	if (0)
		return s->f;
	return a + b + __builtin_types_compatible_p(typeof(s->f), int) + _Alignof(typeof(s->f));
}
