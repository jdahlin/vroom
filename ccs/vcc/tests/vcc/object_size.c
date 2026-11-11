typedef __SIZE_TYPE__ size_t;
struct flex { int n; char data[]; } tail = { 1, "01234" };
struct zero { int n; char data[0]; };
struct fixed_tail { int n; char data[8]; };
struct pair { int first; char data[8]; int last; };
extern char unknown_array[];
static int calls;
static char *side_effect(void) { calls++; return (char *)0; }
int main(void)
{
	char bytes[40];
	struct pair p;
	if (__builtin_object_size(bytes, 0) != 40) return 1;
	if (__builtin_object_size(bytes + 7, 0) != 33) return 2;
	if (__builtin_object_size(&bytes[40], 0) != 0) return 3;
	if (__builtin_object_size(&bytes[41], 2) != 0) return 4;
	if (__builtin_object_size(p.data + 2, 0) != sizeof(p) - __builtin_offsetof(struct pair, data) - 2) return 5;
	if (__builtin_object_size(p.data + 2, 1) != 6) return 6;
	if (__builtin_object_size(p.data + 2, 3) != 6) return 7;
	if (__builtin_object_size(&tail.data, 0) != 6) return 8;
	if (__builtin_dynamic_object_size(&tail.data, 0) != 6) return 9;
	if (__builtin_object_size(((struct zero *)&bytes[1])->data, 1) != 35) return 10;
	if (__builtin_dynamic_object_size(((struct zero *)&bytes[1])->data, 1) != 35) return 11;
	if (__builtin_object_size(side_effect(), 0) != (size_t)-1 || calls) return 12;
	if (__builtin_object_size(side_effect(), 2) != 0 || calls) return 13;
	if (__builtin_object_size("hello" + 1, 0) != 5) return 14;
	if (__builtin_object_size(((struct fixed_tail *)bytes)->data, 1) != 36) return 15;
	struct fixed_tail records[2];
	if (__builtin_object_size(records[0].data, 1) != 8) return 16;
	char small[8];
	if (__builtin_object_size(((struct fixed_tail *)small)->data, 1) != 4) return 17;
	if (__builtin_object_size(((struct fixed_tail *)bytes)[0].data, 1) != 36) return 18;
	if (__builtin_object_size(unknown_array, 0) != (size_t)-1) return 19;
	if (__builtin_object_size(unknown_array, 3) != 0) return 20;
	if (__builtin_object_size((void *)bytes + 3, 0) != 37) return 21;
	return 0;
}
