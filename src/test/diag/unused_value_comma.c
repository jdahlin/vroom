int a, b;
void f(void) {
 (a < b, 1);
 (a < b++, 1);
 (a + b++, 1);
 (a + b, 1);
 (-a, 1);
 a == 1, b;
}
