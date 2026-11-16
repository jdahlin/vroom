# 28.2 Header `<locale>` synopsis [locale.syn]

[Contents](../README.md) · up: [28 Localization library \[localization\]](localization.md) · prev: [28.1 General \[localization.general\]](localization.general.md) · next: [28.3 Locales \[locales\]](locales.md)

```cpp
namespace std {
  // [locale], locale
  class locale;
  template<class Facet> const Facet& use_facet(const locale&);
  template<class Facet> bool         has_facet(const locale&) noexcept;

  // [locale.convenience], convenience interfaces
  template<class charT> bool isspace (charT c, const locale& loc);
  template<class charT> bool isprint (charT c, const locale& loc);
  template<class charT> bool iscntrl (charT c, const locale& loc);
  template<class charT> bool isupper (charT c, const locale& loc);
  template<class charT> bool islower (charT c, const locale& loc);
  template<class charT> bool isalpha (charT c, const locale& loc);
  template<class charT> bool isdigit (charT c, const locale& loc);
  template<class charT> bool ispunct (charT c, const locale& loc);
  template<class charT> bool isxdigit(charT c, const locale& loc);
  template<class charT> bool isalnum (charT c, const locale& loc);
  template<class charT> bool isgraph (charT c, const locale& loc);
  template<class charT> bool isblank (charT c, const locale& loc);
  template<class charT> charT toupper(charT c, const locale& loc);
  template<class charT> charT tolower(charT c, const locale& loc);

  // [category.ctype], ctype
  class ctype_base;
  template<class charT> class ctype;
  template<>            class ctype<char>;      // specialization
  template<class charT> class ctype_byname;
  class codecvt_base;
  template<class internT, class externT, class stateT> class codecvt;
  template<class internT, class externT, class stateT> class codecvt_byname;

  // [category.numeric], numeric
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class num_get;
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class num_put;
  template<class charT>
    class numpunct;
  template<class charT>
    class numpunct_byname;

  // [category.collate], collation
  template<class charT> class collate;
  template<class charT> class collate_byname;

  // [category.time], date and time
  class time_base;
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class time_get;
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class time_get_byname;
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class time_put;
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class time_put_byname;

  // [category.monetary], money
  class money_base;
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class money_get;
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class money_put;
  template<class charT, bool Intl = false>
    class moneypunct;
  template<class charT, bool Intl = false>
    class moneypunct_byname;

  // [category.messages], message retrieval
  class messages_base;
  template<class charT> class messages;
  template<class charT> class messages_byname;
}
```

**1** The header `<locale>` defines classes and declares functions that encapsulate and manipulate the information peculiar to a locale.[^1]

[^1]: In this subclause, the type name `struct tm` is an incomplete type that is defined in `<ctime>` ([[ctime.syn]](ctime.syn.md)).
