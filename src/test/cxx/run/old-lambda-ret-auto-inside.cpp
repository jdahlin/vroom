// a lambda with no return whose body is the first use of a function with a deduced return type: that
// function's returns are not the lambda's (the lambda returns void; libc++'s __tree::__insert_range_unique)
template <class T> auto twice(T v) { return v * 2; }
template <class F> void call(F f, int &out) { f(out); }
int main() {
  int r = 0;
  call([](int &o) { o = twice(21); }, r);
  return r == 42 ? 0 : 1;
}
