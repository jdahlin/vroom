# 29.5.4 Class template `fpos` [fpos]

[Contents](../../README.md) · up: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md) · prev: [29.5.3.7 Constructors and destructor \[ios.base.cons\]](ios.base.cons.md) · next: [29.5.4.1 Members \[fpos.members\]](fpos.members.md)

```cpp
namespace std {
  template<class stateT> class fpos {
  public:
    // [fpos.members], members
    stateT state() const;
    void state(stateT);
  private;
    stateT st;                  // exposition only
  };
}
```

## Subclauses

- [29.5.4.1 Members \[fpos.members\]](fpos.members.md)
- [29.5.4.2 Requirements \[fpos.operations\]](fpos.operations.md)
