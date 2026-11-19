// arrays of unknown bound at namespace scope: named before the definition gives the bound, and T a[] = {...}
// or = "text" defined with the initializer's bound (llvm-test-suite 2003-05-14-array-init)
extern int table[];
int f() { return table[1] + *table; }
int *g() { return table; }
int t1[] = {1, 2, 3};
char s1[] = "abc";
char s3[8] = "ab";
const char s2[] = "abc";
const char *ps[] = {"x", "yz"};
int main() { return f() == 1 && g()[3] == 4 && sizeof t1 == 12 && sizeof s1 == 4 && ps[1][1] == 'z' && t1[2] == 3 && s3[1] == 0x62 && !s3[7] && sizeof s2 == 4 && s1[2] == 0x63 ? 0 : 1; }
int table[] = { 1, 0, 3, 4 };
