/* -Wconstant-conversion and __builtin_(dynamic_)object_size: clang's evaluator
   leaves an unknown object's size to code generation, so arithmetic on it is no
   constant and does not warn (nouveau_chan.c: args->count =
   __member_size(args->data) / sizeof(*args->data), with -Werror); a known size
   folds and warns as any constant. */
struct d { int m; };
struct a { unsigned char count; struct d data[]; };
char arr[300];
void f(struct a *args, char *q, unsigned char *o)
{
	args->count = __builtin_dynamic_object_size(args->data, 1) / sizeof(*args->data);
	*o = __builtin_object_size(q, 0) / 4;
	*o = 1 ? __builtin_object_size(q, 2) + 300 : 0;
	*o = __builtin_object_size(q, 1);
	*o = __builtin_object_size(arr, 0);
}
