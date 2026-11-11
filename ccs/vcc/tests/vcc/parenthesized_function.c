/* Grouping parentheses retain the defining parameter list, including K&R
   and a function returning a pointer to another function. */
extern void abort(void);
static int (sum)(int a, int b) { return a + b; }
static int (((identity)))(int value) { return value; }
static int (*choose(int which))(int) { return which ? identity : 0; }
static int (*(choose_grouped)(int which))(int ignored) { return which ? identity : 0; }
static int (old_style)(value) int value; { return value * 2; }
static int (variadic)(int value, ...) {
 __builtin_va_list ap;
 __builtin_va_start(ap, value);
 int extra = __builtin_va_arg(ap, int);
 __builtin_va_end(ap);
 return value + extra;
}
int main(void) {
 if (sum(4,5)!=9 || identity(7)!=7 || choose(1)(8)!=8 ||
     choose_grouped(1)(3)!=3 || old_style(6)!=12 || variadic(2,9)!=11)
  abort();
 return 0;
}
