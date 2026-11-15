// c ? "ab" : "xyz": array operands decay to pointers, the composite pointer type (json's is_ordered ? R"(...)" : R"(...)")
int len(const char *s) { int n = 0; while (s[n]) n++; return n; }
int f(bool b) { return len(b ? "ab" : "xyz"); }
int g(bool b) { const char *p = b ? "ab" : "cd"; return len(p); }
int h(bool b, const char *q) { return len(b ? q : "xyz"); }
int main() { return f(true) + f(false) + g(true) + h(false, "q") - 10; }
