struct bits { _Bool a : 1; _Bool : 0; _Bool b : 1; };
struct flexible { int n; unsigned char data[]; };
struct zero { int n; unsigned char data[0]; int tail; };
union flexible_union { int n; unsigned char data[]; };
_Static_assert(sizeof(struct flexible) == sizeof(int), "flexible size");
_Static_assert(__builtin_offsetof(struct flexible, data) == sizeof(int), "flexible offset");
_Static_assert(__builtin_offsetof(struct zero, tail) == sizeof(int), "GNU zero array");
int main(void) {
  struct bits b = { 1, 0 };
  static struct flexible f = { 3, { 5, 7, 9 } };
  if (b.a != 1 || b.b != 0 || f.n != 3 || f.data[0] != 5 || f.data[2] != 9)
    __builtin_abort();
  b.b = 2;
  if (b.b != 1) __builtin_abort();
  return 0;
}
