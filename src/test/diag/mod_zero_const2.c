/* the same: constant initializers, sizeof, static locals */
int a = 1 ? 2 : 1/0;
int b = sizeof(1/0);
static int c = 4 % 0 + 1;
void f(void) { static int d = 1 / 0; int e[2] = { 1 % 0 }; (void)d; (void)e; }
int g = 0 && 1/0;
