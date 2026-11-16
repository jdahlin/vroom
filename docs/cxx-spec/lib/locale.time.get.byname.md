# 28.4.5.2 Class template `time_get_byname` [locale.time.get.byname]

[Contents](../README.md) · up: [28.4.5 The time category \[category.time\]](category.time.md) · prev: [28.4.5.1.2 Virtual functions \[locale.time.get.virtuals\]](locale.time.get.virtuals.md) · next: [28.4.5.3 Class template `time_put` \[locale.time.put\]](locale.time.put.md)

```cpp
namespace std {
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class time_get_byname : public time_get<charT, InputIterator> {
    public:
      using dateorder = time_base::dateorder;
      using iter_type = InputIterator;

      explicit time_get_byname(const char*, size_t refs = 0);
      explicit time_get_byname(const string&, size_t refs = 0);

    protected:
      ~time_get_byname();
    };
}
```
