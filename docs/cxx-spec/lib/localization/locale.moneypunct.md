# 28.4.6.3 Class template `moneypunct` [locale.moneypunct]

[Contents](../../README.md) · up: [28.4.6 The monetary category \[category.monetary\]](category.monetary.md) · prev: [28.4.6.2.2 Virtual functions \[locale.money.put.virtuals\]](locale.money.put.virtuals.md) · next: [28.4.6.3.1 Members \[locale.moneypunct.members\]](locale.moneypunct.members.md)

```cpp
namespace std {
  class money_base {
  public:
    enum part { none, space, symbol, sign, value };
    struct pattern { char field[4]; };
  };

  template<class charT, bool International = false>
    class moneypunct : public locale::facet, public money_base {
    public:
      using char_type   = charT;
      using string_type = basic_string<charT>;

      explicit moneypunct(size_t refs = 0);

      charT        decimal_point() const;
      charT        thousands_sep() const;
      string       grouping()      const;
      string_type  curr_symbol()   const;
      string_type  positive_sign() const;
      string_type  negative_sign() const;
      int          frac_digits()   const;
      pattern      pos_format()    const;
      pattern      neg_format()    const;

      static locale::id id;
      static const bool intl = International;

    protected:
      ~moneypunct();
      virtual charT        do_decimal_point() const;
      virtual charT        do_thousands_sep() const;
      virtual string       do_grouping()      const;
      virtual string_type  do_curr_symbol()   const;
      virtual string_type  do_positive_sign() const;
      virtual string_type  do_negative_sign() const;
      virtual int          do_frac_digits()   const;
      virtual pattern      do_pos_format()    const;
      virtual pattern      do_neg_format()    const;
    };
}
```

**1** The `moneypunct<>` facet defines monetary formatting parameters used by `money_get<>` and `money_put<>`. A monetary format is a sequence of four components, specified by a `pattern` value `p`, such that the `part` value `static_cast<part>(p.field[i])` determines the `i`<sup></sup>*th* component of the format[^1] In the `field` member of a `pattern` object, each value `symbol`, `sign`, `value`, and either `space` or `none` appears exactly once. The value `none`, if present, is not first; the value `space`, if present, is neither first nor last.

**2** Where `none` or `space` appears, white space is permitted in the format, except where `none` appears at the end, in which case no white space is permitted. The value `space` indicates that at least one space is required at that position. Where `symbol` appears, the sequence of characters returned by `curr_symbol()` is permitted, and can be required. Where `sign` appears, the first (if any) of the sequence of characters returned by `positive_sign()` or `negative_sign()` (respectively as the monetary value is non-negative or negative) is required. Any remaining characters of the sign sequence are required after all other format components. Where `value` appears, the absolute numeric monetary value is required.

**3** The format of the numeric monetary value is a decimal number:

```bnf
value:
    units fractionalₒₚₜ
    decimal-point digits
```

```bnf
fractional:
    decimal-point digitsₒₚₜ
```

if `frac_digits()` returns a positive value, or

```bnf
value:
    units
```

otherwise. The symbol *decimal-point* indicates the character returned by `decimal_point()`. The other symbols are defined as follows:

```bnf
units:
    digits
    digits thousands-sep units
```

```bnf
digits:
    adigit digitsₒₚₜ
```

In the syntax specification, the symbol *adigit* is any of the values `ct.widen(c)` for `c` in the range `'0'` through `'9'` (inclusive) and `ct` is a reference of type `const ctype<charT>&` obtained as described in the definitions of `money_get<>` and `money_put<>`. The symbol *thousands-sep* is the character returned by `thousands_sep()`. The space character used is the value `ct.widen(' ')`. White space characters are those characters `c` for which `ci.is(space, c)` returns `true`. The number of digits required after the decimal point (if any) is exactly the value returned by `frac_digits()`.

**4** The placement of thousands-separator characters (if any) is determined by the value returned by `grouping()`, defined identically as the member `numpunct<>::do_grouping()`.

## Subclauses

- [28.4.6.3.1 Members \[locale.moneypunct.members\]](locale.moneypunct.members.md)
- [28.4.6.3.2 Virtual functions \[locale.moneypunct.virtuals\]](locale.moneypunct.virtuals.md)

[^1]: An array of `char`, rather than an array of `part`, is specified for `pattern::field` purely for efficiency.
