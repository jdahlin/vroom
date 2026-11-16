# C.1.7 [[temp]](temp.md): templates [diff.cpp17.temp]

[Contents](README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.6 [[over]](over.md): overloading \[diff.cpp17.over\]](diff.cpp17.over.md) · next: [C.1.8 [[except]](except.md): exception handling \[diff.cpp17.except\]](diff.cpp17.except.md)

**1** **Affected subclause:** [[temp.names]](temp.names.md) <br>**Change:** An *unqualified-id* that is followed by a `<` and for which name lookup finds nothing or finds a function will be treated as a *template-name* in order to potentially cause argument dependent lookup to be performed. <br>**Rationale:** It was problematic to call a function template with an explicit template argument list via argument dependent lookup because of the need to have a template with the same name visible via normal lookup. <br>**Effect on original feature:** Previously valid code that uses a function name as the left operand of a `<` operator would become ill-formed.

```cpp
struct A {};
bool operator<(void (*fp)(), A);
void f() {}
int main() {
  A a;
  f < a;    // ill-formed; previously well-formed
  (f) < a;  // still well formed
}
```
