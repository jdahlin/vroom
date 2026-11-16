// [dcl.init.list] Example 14 (paragraph 7), docs/cxx-spec/dcl/dcl.init.list.md
// needs: P3
// status: annotated

int x = 999;                    // x is not a constant expression
const int y = 999;
const int z = 99;
char c1 = x;                    // OK, though it might narrow (in this case, it does narrow)
char c2{x};                     // error: might narrow // semantic
char c3{y};                     // error: narrows (assuming char is 8 bits) // semantic
char c4{z};                     // OK: no narrowing needed
unsigned char uc1 = {5};        // OK: no narrowing needed
unsigned char uc2 = {-1};       // error: narrows // semantic
unsigned int ui1 = {-1};        // error: narrows // semantic
signed int si1 =
  { (unsigned int)-1 };         // error: narrows // semantic
int ii = {2.0};                 // error: narrows // semantic
float f1 { x };                 // error: might narrow // semantic
float f2 { 7 };                 // OK: 7 can be exactly represented as a float
bool b = {"meow"};              // error: narrows // semantic
int f(int);
int a[] = { 2, f(2), f(2.0) };  // OK: the double-to-int conversion is not at the top level
