// [dcl.init]: initializer, brace-or-equal-initializer, braced-init-list,
// initializer-list, designated-initializer-list.
struct P {
  int x, y;
};
struct L {
  P a, b;
  int c[2];
};
constexpr int k = 2;

int i1 = 1;
int i2(2);
int i3{3};
int i4 = {4};
int i5{};
int i6 = int();
int i7(k + 1), i8 = k * 2, i9{k - 1}, i10;
int a1[] = {1, 2, 3};
int a2[]{1, 2, 3,};
int a3[2][2] = {{1, 2}, {3, 4}};
int a4[k + 1] = {};
int a5[3] = {k, k + 1, k << 1,};
P p1 = {1, 2};
P p2{.x = 1, .y = 2};
P p3 = {.x = k + 1};
P p4{.x{1}, .y = {2}};
P p5 = {.x = 1, .y = 2,};
L l1 = {{1, 2}, {3, 4}, {5, 6}};
L l2{.a = {1, 2}, .b{.x = 3, .y = 4}, .c = {5, 6}};
L l3{.a{}, .c{}};
P parr[] = {{1, 2}, {.x = 3, .y = 4}, P{5, 6}, P{.x = 7}};
const int &r1 = 1;
const int &r2{k};
const int &r3 = {k};
int *q1 = nullptr, *q2{}, *q3(&i1), **q4 = &q1;
char s1[] = "abc";
char s2[]{"abc"};
char s3[] = {"abc"};
bool b1 = i1 < i2, b2 = (i1, i2), b3 = i1 ? i2 : i3;
int e1 = (i1 = 2, i2 += 3);
int f1(int = 1, int = k + 1, P = {1, 2}, P = P{.x = 1}, int = (1, 2));
