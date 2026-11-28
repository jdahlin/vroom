/* [class.base.init]/2: a constructor template's type parameter naming a
   virtual base in its mem-initializer. */
static int made;
struct Y {
    Y() { made++; }
};
template <typename T> struct X : public virtual Y {
};
template <typename T> struct A : public X<T> {
    template <typename S> A(S) : S() {}
};

int main()
{
    Y y;
    A<int> a(y);
    return made != 2;
}
