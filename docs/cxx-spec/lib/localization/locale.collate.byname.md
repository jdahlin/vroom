# 28.4.4.2 Class template `collate_byname` [locale.collate.byname]

[Contents](../../README.md) · up: [28.4.4 The collate category \[category.collate\]](category.collate.md) · prev: [28.4.4.1.2 Virtual functions \[locale.collate.virtuals\]](locale.collate.virtuals.md) · next: [28.4.5 The time category \[category.time\]](category.time.md)

```cpp
namespace std {
  template<class charT>
    class collate_byname : public collate<charT> {
    public:
      using string_type = basic_string<charT>;

      explicit collate_byname(const char*, size_t refs = 0);
      explicit collate_byname(const string&, size_t refs = 0);

    protected:
      ~collate_byname();
    };
}
```
