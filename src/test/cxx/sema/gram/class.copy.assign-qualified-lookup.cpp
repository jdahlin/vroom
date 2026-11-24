// [class.copy.assign]/2: the implicitly-declared assignment operator of a class is named by T::operator= (not analysed when it is trivial: refused, not rejected)
struct D { int x; };
void t() {
  D &(D::*h)(const D &) = &D::operator=;
}
