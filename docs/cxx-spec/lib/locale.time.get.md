# 28.4.5.1 Class template `time_get` [locale.time.get]

[Contents](../README.md) · up: [28.4.5 The time category \[category.time\]](category.time.md) · prev: [28.4.5 The time category \[category.time\]](category.time.md) · next: [28.4.5.1.1 Members \[locale.time.get.members\]](locale.time.get.members.md)

```cpp
namespace std {
  class time_base {
  public:
    enum dateorder { no_order, dmy, mdy, ymd, ydm };
  };

  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class time_get : public locale::facet, public time_base {
    public:
      using char_type = charT;
      using iter_type = InputIterator;

      explicit time_get(size_t refs = 0);

      dateorder date_order()  const { return do_date_order(); }
      iter_type get_time(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t)  const;
      iter_type get_date(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t)  const;
      iter_type get_weekday(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t) const;
      iter_type get_monthname(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t) const;
      iter_type get_year(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t) const;
      iter_type get(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t, char format, char modifier = 0) const;
      iter_type get(iter_type s, iter_type end, ios_base& f,
                         ios_base::iostate& err, tm* t, const char_type* fmt,
                         const char_type* fmtend) const;

      static locale::id id;

    protected:
      ~time_get();
      virtual dateorder do_date_order()  const;
      virtual iter_type do_get_time(iter_type s, iter_type end, ios_base&,
                                    ios_base::iostate& err, tm* t) const;
      virtual iter_type do_get_date(iter_type s, iter_type end, ios_base&,
                                    ios_base::iostate& err, tm* t) const;
      virtual iter_type do_get_weekday(iter_type s, iter_type end, ios_base&,
                                       ios_base::iostate& err, tm* t) const;
      virtual iter_type do_get_monthname(iter_type s, iter_type end, ios_base&,
                                         ios_base::iostate& err, tm* t) const;
      virtual iter_type do_get_year(iter_type s, iter_type end, ios_base&,
                                    ios_base::iostate& err, tm* t) const;
      virtual iter_type do_get(iter_type s, iter_type end, ios_base& f,
                               ios_base::iostate& err, tm* t, char format, char modifier) const;
    };
}
```

**1** `time_get` is used to parse a character sequence, extracting components of a time or date into a `struct tm` object. Each `get` member parses a format as produced by a corresponding format specifier to `time_put<>::put`. If the sequence being parsed matches the correct format, the corresponding members of the `struct tm` argument are set to the values used to produce the sequence; otherwise either an error is reported or unspecified values are assigned.[^1]

**2** If the end iterator is reached during parsing by any of the `get()` member functions, the member sets `ios_base::eofbit` in `err`.

## Subclauses

- [28.4.5.1.1 Members \[locale.time.get.members\]](locale.time.get.members.md)
- [28.4.5.1.2 Virtual functions \[locale.time.get.virtuals\]](locale.time.get.virtuals.md)

[^1]: In other words, user confirmation is required for reliable parsing of user-entered dates and times, but machine-generated formats can be parsed reliably. This allows parsers to be aggressive about interpreting user variations on standard formats.
