// [namespace.udecl] Example 14 (paragraph 19), docs/cxx-spec/dcl/namespace.udecl.md
// needs: P5
// status: annotated

class A {
private:
    void f(char);
public:
    void f(int);
protected:
    void g();
};

class B : public A {
  using A::f;       // error: A::f(char) is inaccessible // semantic
public:
  using A::g;       // B::g is a public synonym for A::g
};
