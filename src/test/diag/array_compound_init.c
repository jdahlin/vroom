/* diag-flags: -Wgnu-compound-literal-initializer */
static int good[] = (int[]){ 1, 2 };
static int wrong_bound[3] = (int[2]){ 1, 2 };
static const int wrong_qualifiers[] = (int[]){ 1, 2 };
void f(volatile int value)
{
  int observable[] = (int[]){ value };
}
