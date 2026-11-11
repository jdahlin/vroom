/* Literal byte offsets must be in bounds and preserve evaluated effects. */
static volatile unsigned calls;
unsigned long strlen(const char *s)
{
 unsigned long n = 0;
 while (s[n]) n++;
 return n;
}
unsigned long sl_offset(void) { return __builtin_strlen("abcd" + 2); }
unsigned long sl_embedded(void) { return __builtin_strlen("a\0bc" + 2); }
unsigned long sl_terminator(void) { return __builtin_strlen(4 + "abcd"); }
unsigned long sl_expr(void) { return __builtin_strlen("abcdef" + (1 + 2)); }
/* These undefined inputs are compiled only to check conservative fallback. */
unsigned long sl_oob(void) { return __builtin_strlen("abcd" + 6); }
unsigned long sl_negative(void) { return __builtin_strlen("abcd" - 1); }
unsigned long sl_side_effect(void) { return __builtin_strlen("abcd" + (++calls, 2)); }
unsigned long sl_variable(unsigned n) { return __builtin_strlen("abcd" + n); }
int main(void)
{
 if (sl_offset() != 2 || sl_embedded() != 2 || sl_terminator() != 0 || sl_expr() != 3) return 1;
 if (sl_side_effect() != 2 || calls != 1 || sl_variable(1) != 3) return 2;
 return 0;
}
