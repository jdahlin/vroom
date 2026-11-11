/* Static compound-literal subobjects and comma operands (Clang-compatible). */
struct Item { int a[3]; double d; const char *text; };
union U { struct Item item; long other; };
struct Outer { union U u; int tail; };
static struct Item item = ((struct Outer){ .u.item = {{2, 4, 8}, 1.5, "hi"}, .tail = 9 }).u.item;
static union U whole = ((struct Outer){ .u.item = {{3, 6, 9}, 2.5, "bye"} }).u;
static struct Item zero = ((struct Outer){ .tail = 7 }).u.item;
static _Complex double selected = ((struct { int a; _Complex double z; }){ 7, 3.0 + 4.0i }).z;
static _Complex float sequence = (1.0 + 2.0i, (0.0 - 1.0i, 5.0 - 6.0i));
static struct Item comma_item = (1, ((struct Outer){.u.item={{7,8,9},10.5,"z"}}).u.item);
static void *pointer = (0, &item, (&whole, &item));
int main(void) {
  if (item.a[2] != 8 || item.d != 1.5 || item.text[1] != 'i') return 1;
  if (whole.item.a[1] != 6 || whole.item.d != 2.5) return 2;
  if (zero.a[0] || zero.d || zero.text) return 3;
  if (__real__ selected != 3 || __imag__ selected != 4) return 5;
  if (__real__ sequence != 5 || __imag__ sequence != -6) return 6;
  if (comma_item.a[0] != 7 || pointer != &item) return 7;
  return 0;
}
