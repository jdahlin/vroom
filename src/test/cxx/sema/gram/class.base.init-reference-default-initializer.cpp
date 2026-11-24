// [class.base.init]/9.1, /11: a reference member with a default member
// initializer is bound by the constructor; a temporary bound to it is
// ill-formed; an rvalue reference member deletes the copy constructor
// ([class.copy.ctor]/10)
struct W {
    int n = 1;
    int &r = n;
    W() {}
};
W w;
struct R {
    int n;
    int &&q = static_cast<int &&>(n);
};
R r;
