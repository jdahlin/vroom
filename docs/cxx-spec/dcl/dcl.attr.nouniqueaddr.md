# 9.12.10 No unique address attribute [dcl.attr.nouniqueaddr]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.9 Noreturn attribute \[dcl.attr.noreturn\]](dcl.attr.noreturn.md) · next: [10 Modules \[module\]](../module/module.md)

**1** The *attribute-token* `no_unique_address` specifies that a non-static data member is a potentially-overlapping subobject ([[intro.object]](../basic/intro.object.md)). It shall appear at most once in each *attribute-list* and no *attribute-argument-clause* shall be present. The attribute may appertain to a non-static data member other than a bit-field.

**2** [*Note 1*: The non-static data member can share the address of another non-static data member or that of a base class, and any padding that would normally be inserted at the end of the object can be reused as storage for other members. — *end note*] [*Example 1*:

```cpp
template<typename Key, typename Value,
         typename Hash, typename Pred, typename Allocator>
class hash_map {
  [[no_unique_address]] Hash hasher;
  [[no_unique_address]] Pred pred;
  [[no_unique_address]] Allocator alloc;
  Bucket *buckets;
  // ...
public:
  // ...
};
```

Here, `hasher`, `pred`, and `alloc` could have the same address as `buckets` if their respective types are all empty. — *end example*]
