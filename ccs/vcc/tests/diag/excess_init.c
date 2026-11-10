/* -Wexcess-initializers: arrays, structs, unions, scalars, strings too long; excess braced elements skipped */
int a[2] = { 1, 2, 3 };
struct S { int x, y; } s = { 1, 2, 3 };
char c[2] = "abc";
char d[3] = "abc";
int e = { 1, 2 };
struct S t[1] = { { 1 }, { 2 } };
int f[2][2] = { 1, 2, 3, 4, 5 };
union U { int i; float f; } u = { 1, 2 };
char g[2][3] = { "abcd", "ab" };
struct T { char n[2]; } h = { "xyz" };
char i[4] = { "abcde" };
int j[2] = { 1, 2, { 3 } };
int k[1] = { 1, (2 + 3) * 4 };
int l = { 1, { 2 } };
