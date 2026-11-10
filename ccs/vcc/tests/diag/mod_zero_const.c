/* -Wdivision-by-zero in static initializers: after the constness error, only when the initializer runs */
int x = 5 % 0;
int y = 5 / 0;
int z = 1 << 40;
int w = (1 / 0) + 1;
void f(void) { int a = 5 % 0; (void)a; }
