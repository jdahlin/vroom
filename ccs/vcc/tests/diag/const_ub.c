/* D5.7: undefined behaviour the constant folder sees, each with a line on why:
   shift counts against the promoted type's width, signed overflow in +, -, * and
   unary - (clang's -Winteger-overflow: the wrapped result, with digit separators),
   division by zero */
int shl(int x) { return x << 34; }
unsigned long wide = 1u << 40;
int neg = 1 << -1;
int big = 2147483647 + 1;
int low = -2147483647 - 2;
long fine = 2147483647L + 1;
int f(void) { return -(-2147483647 - 1) + 1 / 0; }
int g(void) { return 65536 * 65536; }
