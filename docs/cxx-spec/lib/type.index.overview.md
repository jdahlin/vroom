# 20.17.2 `type_index` overview [type.index.overview]

[Contents](../README.md) · up: [20.17 Class `type_index` \[type.index\]](type.index.md) · prev: [20.17.1 Header `<typeindex>` synopsis \[type.index.synopsis\]](type.index.synopsis.md) · next: [20.17.3 `type_index` members \[type.index.members\]](type.index.members.md)

```cpp
namespace std {
  class type_index {
  public:
    type_index(const type_info& rhs) noexcept;
    bool operator==(const type_index& rhs) const noexcept;
    bool operator< (const type_index& rhs) const noexcept;
    bool operator> (const type_index& rhs) const noexcept;
    bool operator<=(const type_index& rhs) const noexcept;
    bool operator>=(const type_index& rhs) const noexcept;
    strong_ordering operator<=>(const type_index& rhs) const noexcept;
    size_t hash_code() const noexcept;
    const char* name() const noexcept;

  private:
    const type_info* target;    // exposition only
    // Note that the use of a pointer here, rather than a reference,
    // means that the default copy/move constructor and assignment
    // operators will be provided and work as expected.
  };
}
```

**1** The class `type_index` provides a simple wrapper for `type_info` which can be used as an index type in associative containers ([[associative]](associative.md)) and in unordered associative containers ([[unord]](unord.md)).
