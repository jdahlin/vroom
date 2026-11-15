// c ? class : other, the other operand converted to the class by its constructor in its own branch (json's
// byte_ != 0 ? concat(" at byte ", ...) : ""); c ? lvalue : prvalue copies the lvalue (a prvalue result)
static int live = 0, made = 0;
struct S { int v; S(int x) : v(x) { ++live; ++made; } S(const S &o) : v(o.v) { ++live; ++made; } ~S() { --live; } };
struct Str { const char *p; Str(const char *s) : p(s) {} };
S mk(int v) { return S(v); }
int f(bool c) { return (c ? mk(4) : 9).v; }
int g(bool c) { S h(2); int m0 = made; int v = (c ? h : 9).v; return !c || made - m0 == 1 ? v : -1; }
const char *s(bool c) { return (c ? Str("yes") : "no").p; }
int main() {
  int r = f(true) == 4 && f(false) == 9 && g(true) == 2 && g(false) == 9 && live == 0;
  return r && s(true)[0] == 'y' && s(false)[0] == 'n' ? 0 : 1;
}
