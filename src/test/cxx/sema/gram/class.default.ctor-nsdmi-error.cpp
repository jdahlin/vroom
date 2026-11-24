// [class.mem]/6, [class.default.ctor]/2: a default member initializer that
// is ill-formed is an error of the class, even when its defaulted default
// constructor is never used; a deleted one (a reference member) is not.
struct W {
  int a = 1;
  int n = undeclared();
};
struct V {
  int a = 1;
  int n = 2;
};
struct Z {
  int &r;
  int n = 2;
};
