// [class.mem]/6: a class whose member function body is ill-formed is still a complete class; a declaration after it uses it
class Base { int priv; };
class Test : public Base {
  void test() { priv++; }
};
void use(Test *t);
