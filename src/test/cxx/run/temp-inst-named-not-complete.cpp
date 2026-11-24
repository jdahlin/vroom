// [temp.inst]/2: a specialization named where no completely-defined type is
// needed is not instantiated: the definition would be ill-formed for the
// argument (clang accepts); sizeof, a member access or an object completes it.
template <class T> struct Error { typename T::x y; };
template <class T> struct S { T t; };
template <class T> struct B { T *p; };
extern S<int> *p;
void f(S<int &>);
B<S<void>> b;
Error<float> *e;
void g(Error<float> *);
int h() { return sizeof(S<int>); }

int main() { return h() != 4; }
