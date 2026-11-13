/* vcc-flags: -fno-builtin-malloc */
typedef __SIZE_TYPE__ size_t;
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void free(void *);
int main(void)
{
	char *p = malloc(37);
	char *q = calloc(3, 17);
	if (__builtin_object_size(p, 0) != (size_t)-1) return 1;
	if (__builtin_dynamic_object_size(p, 0) != (size_t)-1) return 2;
	if (__builtin_object_size(q, 0) != 51) return 3;
	free(p);
	free(q);
	return 0;
}
