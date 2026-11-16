# 28.4.5.4 Class template `time_put_byname` [locale.time.put.byname]

[Contents](../README.md) · up: [28.4.5 The time category \[category.time\]](category.time.md) · prev: [28.4.5.3.2 Virtual functions \[locale.time.put.virtuals\]](locale.time.put.virtuals.md) · next: [28.4.6 The monetary category \[category.monetary\]](category.monetary.md)

```cpp
namespace std {
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class time_put_byname : public time_put<charT, OutputIterator> {
    public:
      using char_type = charT;
      using iter_type = OutputIterator;

      explicit time_put_byname(const char*, size_t refs = 0);
      explicit time_put_byname(const string&, size_t refs = 0);

    protected:
      ~time_put_byname();
    };
}
```
