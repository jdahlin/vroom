# 28.4.7.1 Class template `messages` [locale.messages]

[Contents](../README.md) · up: [28.4.7 The message retrieval category \[category.messages\]](category.messages.md) · prev: [28.4.7 The message retrieval category \[category.messages\]](category.messages.md) · next: [28.4.7.1.1 Members \[locale.messages.members\]](locale.messages.members.md)

```cpp
namespace std {
  class messages_base {
  public:
    using catalog = unspecified signed integer type;
  };

  template<class charT>
    class messages : public locale::facet, public messages_base {
    public:
      using char_type   = charT;
      using string_type = basic_string<charT>;

      explicit messages(size_t refs = 0);

      catalog open(const string& fn, const locale&) const;
      string_type get(catalog c, int set, int msgid,
                       const string_type& dfault) const;
      void close(catalog c) const;

      static locale::id id;

    protected:
      ~messages();
      virtual catalog do_open(const string&, const locale&) const;
      virtual string_type do_get(catalog, int set, int msgid,
                                 const string_type& dfault) const;
      virtual void do_close(catalog) const;
    };
}
```

**1** Values of type `messages_base::catalog` usable as arguments to members `get` and `close` can be obtained only by calling member `open`.

## Subclauses

- [28.4.7.1.1 Members \[locale.messages.members\]](locale.messages.members.md)
- [28.4.7.1.2 Virtual functions \[locale.messages.virtuals\]](locale.messages.virtuals.md)
