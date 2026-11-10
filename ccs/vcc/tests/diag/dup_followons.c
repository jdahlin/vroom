/* D2.3 duplicates: an invalid declaration's uses, a compound literal in
   parentheses and a corrected name are each reported once, as clang does */
void v;
int use_v(void) { return v + 1; }
struct P { int x, y; };
int g(void) { return ((struct P){ 1, 2 }).x + ((struct P){ 1, }).zz; }
int counter;
int h(void) { return countr + countr * 2; }
int k(void) { countre = 1; return countre; }
