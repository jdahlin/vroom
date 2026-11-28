/* [dcl.init.ref]/5.4: no rvalue reference binds the lvalue a conversion
   function yields, so f(T &&) = delete beside f(T &) is not viable
   (std::reference_wrapper's constructor) */
struct S { int v; };
struct R { S *p; operator S &() const { return *p; } };
void fun(S &) {}
void fun(S &&) = delete;
template <class T> concept c = requires(T t) { fun(static_cast<T &&>(t)); };
static_assert(c<R> && c<R &>);
template <class T> struct RW { T *p; operator T &() const { return *p; } static void f(T &); static void f(T &&) = delete; };
template <class T, class U> concept d = requires(U u) { RW<T>::f(static_cast<U &&>(u)); };
static_assert(d<S, RW<S>> && d<S, RW<S> &>);
int main() { S s{1}; R r{&s}; fun(r); return 0; }
