/* [class.bit]: bit-fields of enumeration type, and in unions ([class.union]/2) */
enum K { k0, k1, k2 };
enum class L : unsigned char { a, b };
struct S { K k : 2; L l : 1; int x : 5; };
union U { char c; int a : 12; };
union V { int a : 3; short b : 9; };
int main() { S s; s.k = k2; s.l = L::b; s.x = -3; U u; u.a = 100; V v; v.b = 200;
  return (s.k == k2) + (s.l == L::b) * 2 + (s.x == -3) * 4 + (u.a == 100) * 8 + (v.b == 200) * 16 + (int)sizeof(U) * 32 + (int)sizeof(V) * 256 - 7*1 - 24 - 128 - 1024; }
