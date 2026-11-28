/* [conv.mem]: a pointer to member converts to no pointer, so the function
   taking one is not viable (catch2's makeTestInvoker) */
struct I {};
I make(void (*)()) { return {}; }
template <class C> int make(void (C::*)()) { return 2; }
struct S : I { void t() {} };
int main() { return make(&S::t) - 2; }
