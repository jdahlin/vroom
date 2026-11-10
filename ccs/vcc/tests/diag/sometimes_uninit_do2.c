int g(int);
int f(int a, int b) {
  int x;
  do {
    if (a) break;
    if (b) break;
    x = 1;
  } while (0);
  return x;
}
int h(int a) {
  int y;
  if (a) y = 1;
  else g(0);
  y = y + 0;
  int z;
  if (a) z = 2;
  return z;
}
int k(int a) {
  int w;
  if (a) { w = 1; }
  return g(a) ? w : 0;
}
