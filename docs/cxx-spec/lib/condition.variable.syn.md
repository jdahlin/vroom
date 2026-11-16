# 32.6.1 Header `<condition_variable>` synopsis [condition.variable.syn]

[Contents](../README.md) · up: [32.6 Condition variables \[thread.condition\]](thread.condition.md) · prev: [32.6 Condition variables \[thread.condition\]](thread.condition.md) · next: [32.6.2 Non-member functions \[thread.condition.nonmember\]](thread.condition.nonmember.md)

```cpp
namespace std {
  class condition_variable;
  class condition_variable_any;

  void notify_all_at_thread_exit(condition_variable& cond, unique_lock<mutex> lk);

  enum class cv_status { no_timeout, timeout };
}
```
