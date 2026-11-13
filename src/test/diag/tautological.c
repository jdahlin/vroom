/* D5.1: comparisons whose result is known: x == x (-Wtautological-compare, not for floating
   types), an array, &object or function against null (-Wtautological-pointer-compare, with
   clang's fix-it notes for a function), and a constant outside the other operand's own type
   (-Wtautological-constant-out-of-range-compare, the type's range as help) */
struct conn { int id; int buf[4]; } c;
int retries, slots[8];
unsigned char byte;
_Bool ready;
double ratio;
int poll(void);
int same(void) { return retries == retries || c.id != c.id || slots[1] < slots[1]; }
int nan_check(void) { return ratio != ratio; }
int nulls(void) { return slots == 0 || &c.id != 0 || c.buf == (void *)0 || poll == 0; }
int r1(void) { return byte < 256; }
int r2(void) { return byte == -1; }
int r3(void) { return ready == 2; }
int r4(void) { return 300 > byte; }
int limits(void) { return byte <= 255 || (byte & 0x1ff) == 256; }
