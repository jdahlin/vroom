/* ranges with the operand's parentheses (clang's ParenExpr): division by zero, shifts, -Wformat */
int f(int x){ return x / (0) + x % (1-1) + (x << (64)); }
int printf(const char *, ...);
void p(long v){ printf("%d", (v)); printf("%d", ((v))); }
