// [expr.await] Example 1 (paragraph 6), docs/cxx-spec/expr/expr.await.md
// needs: P7
// status: annotated

template <typename T>
struct my_future {
  /* ... */
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  T await_resume();
};

template <class Rep, class Period>
auto operator co_await(std::chrono::duration<Rep, Period> d) {
  struct awaiter {
    std::chrono::system_clock::duration duration;
    /* ... */
    awaiter(std::chrono::system_clock::duration d) : duration(d) {}
    bool await_ready() const { return duration.count() <= 0; }
    void await_resume() {}
    void await_suspend(std::coroutine_handle<> h) { /* ... */ }
  };
  return awaiter{d};
}

using namespace std::chrono;

my_future<int> h();

my_future<void> g() {
  std::cout << "just about go to sleep...\n";
  co_await 10ms;
  std::cout << "resumed\n";
  co_await h();
}

auto f(int x = co_await h());   // error: await-expression outside of function suspension context // semantic
int a[] = { co_await h() };     // error: await-expression outside of function suspension context // semantic
