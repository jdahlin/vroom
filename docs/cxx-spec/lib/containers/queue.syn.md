# 22.6.2 Header `<queue>` synopsis [queue.syn]

[Contents](../../README.md) · up: [22.6 Container adaptors \[container.adaptors\]](container.adaptors.md) · prev: [22.6.1 In general \[container.adaptors.general\]](container.adaptors.general.md) · next: [22.6.3 Header `<stack>` synopsis \[stack.syn\]](stack.syn.md)

```cpp
#include <compare>              // see [compare.syn]
#include <initializer_list>     // see [initializer.list.syn]

namespace std {
  template<class T, class Container = deque<T>> class queue;

  template<class T, class Container>
    bool operator==(const queue<T, Container>& x, const queue<T, Container>& y);
  template<class T, class Container>
    bool operator!=(const queue<T, Container>& x, const queue<T, Container>& y);
  template<class T, class Container>
    bool operator< (const queue<T, Container>& x, const queue<T, Container>& y);
  template<class T, class Container>
    bool operator> (const queue<T, Container>& x, const queue<T, Container>& y);
  template<class T, class Container>
    bool operator<=(const queue<T, Container>& x, const queue<T, Container>& y);
  template<class T, class Container>
    bool operator>=(const queue<T, Container>& x, const queue<T, Container>& y);
  template<class T, three_way_comparable Container>
    compare_three_way_result_t<Container>
      operator<=>(const queue<T, Container>& x, const queue<T, Container>& y);

  template<class T, class Container>
    void swap(queue<T, Container>& x, queue<T, Container>& y) noexcept(noexcept(x.swap(y)));
  template<class T, class Container, class Alloc>
    struct uses_allocator<queue<T, Container>, Alloc>;

  template<class T, class Container = vector<T>,
           class Compare = less<typename Container::value_type>>
    class priority_queue;

  template<class T, class Container, class Compare>
    void swap(priority_queue<T, Container, Compare>& x,
              priority_queue<T, Container, Compare>& y) noexcept(noexcept(x.swap(y)));
  template<class T, class Container, class Compare, class Alloc>
    struct uses_allocator<priority_queue<T, Container, Compare>, Alloc>;
}
```
