# 28.4.7.2 Class template `messages_byname` [locale.messages.byname]

[Contents](../README.md) · up: [28.4.7 The message retrieval category \[category.messages\]](category.messages.md) · prev: [28.4.7.1.2 Virtual functions \[locale.messages.virtuals\]](locale.messages.virtuals.md) · next: [28.5 C library locales \[c.locales\]](c.locales.md)

```cpp
namespace std {
  template<class charT>
    class messages_byname : public messages<charT> {
    public:
      using catalog     = messages_base::catalog;
      using string_type = basic_string<charT>;

      explicit messages_byname(const char*, size_t refs = 0);
      explicit messages_byname(const string&, size_t refs = 0);

    protected:
      ~messages_byname();
    };
}
```
