typedef __SIZE_TYPE__ size_t;
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void *realloc(void *, size_t);
extern void free(void *);
struct header { int a, b; };
struct tail { int n; char data[1]; };
union allocation { struct header h; int n; };
static char replacement[47];
static int sound(size_t n, size_t actual) { return n == (size_t)-1 || n == actual; }
__attribute__((noinline)) static void change(char **p) { *p = replacement; }
__attribute__((noinline)) static int changed(void)
{
	char *p = malloc(11);
	free(p);
	p = replacement;
	return sound(__builtin_object_size(p, 0), 47) &&
	       sound(__builtin_dynamic_object_size(p, 0), 47);
}
__attribute__((noinline)) static int escaped(void)
{
	char *p = malloc(13);
	free(p);
	change(&p);
	return sound(__builtin_object_size(p, 0), 47) &&
	       sound(__builtin_dynamic_object_size(p, 0), 47);
}
__attribute__((noinline)) static int conditional(int which)
{
	char *p = malloc(17);
	free(p);
	if (which) p = replacement;
	return !which || sound(__builtin_object_size(p, 0), 47);
}
__attribute__((noinline)) static int incremented(void)
{
	char a[31];
	char *p = a;
	p++;
	return sound(__builtin_object_size(p, 0), 30);
}
__attribute__((noinline)) static int backward_jump(void)
{
	char a[31];
	char *p = a;
again:
	if (p == a) { p = replacement; goto again; }
	return sound(__builtin_object_size(p, 0), 47);
}
int main(void)
{
	union allocation *u = malloc(sizeof(struct header) + sizeof(struct tail) + 6);
	struct tail *t = (struct tail *)(&u->h + 1);
	if (__builtin_object_size(t->data, 1) != sizeof(struct tail) + 6 - __builtin_offsetof(struct tail, data)) return 1;
	if (__builtin_dynamic_object_size(t->data, 1) != sizeof(struct tail) + 6 - __builtin_offsetof(struct tail, data)) return 2;
	free(u);
	char *p = calloc(3, 19);
	char *alias = p + 7;
	if (__builtin_object_size(alias, 0) != 50) return 3;
	char *q = realloc(p, 91);
	if (__builtin_dynamic_object_size(q + 11, 0) != 80) return 4;
	free(q);
	if (!changed() || !escaped() || !conditional(1) || !incremented() || !backward_jump()) return 5;
	return 0;
}
