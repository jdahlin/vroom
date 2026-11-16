# 23.6.3.1 Class `istreambuf_iterator::proxy` [istreambuf.iterator.proxy]

[Contents](../README.md) · up: [23.6.3 Class template `istreambuf_iterator` \[istreambuf.iterator\]](istreambuf.iterator.md) · prev: [23.6.3 Class template `istreambuf_iterator` \[istreambuf.iterator\]](istreambuf.iterator.md) · next: [23.6.3.2 Constructors \[istreambuf.iterator.cons\]](istreambuf.iterator.cons.md)

**1** Class `istreambuf_iterator<charT,traits>::proxy` is for exposition only. An implementation is permitted to provide equivalent functionality without providing a class with this name. Class `istreambuf_iterator<charT, traits>::proxy` provides a temporary placeholder as the return value of the post-increment operator (`operator++`). It keeps the character pointed to by the previous value of the iterator for some possible future access to get the character.

```cpp
namespace std {
  template<class charT, class traits>
  class istreambuf_iterator<charT, traits>::proxy { // exposition only
    charT keep_;
    basic_streambuf<charT,traits>* sbuf_;
    proxy(charT c, basic_streambuf<charT,traits>* sbuf)
      : keep_(c), sbuf_(sbuf) { }
  public:
    charT operator*() { return keep_; }
  };
}
```
