# 29.7.4.2.2 Arithmetic extractors [istream.formatted.arithmetic]

[Contents](../../README.md) · up: [29.7.4.2 Formatted input functions \[istream.formatted\]](istream.formatted.md) · prev: [29.7.4.2.1 Common requirements \[istream.formatted.reqmts\]](istream.formatted.reqmts.md) · next: [29.7.4.2.3 `basic_istream::operator>>` \[istream.extractors\]](istream.extractors.md)

```cpp
operator>>(unsigned short& val);
operator>>(unsigned int& val);
operator>>(long& val);
operator>>(unsigned long& val);
operator>>(long long& val);
operator>>(unsigned long long& val);
operator>>(float& val);
operator>>(double& val);
operator>>(long double& val);
operator>>(bool& val);
operator>>(void*& val);
```

**1** As in the case of the inserters, these extractors depend on the locale’s `num_get<>` ([[locale.num.get]](../localization/locale.num.get.md)) object to perform parsing the input stream data. These extractors behave as formatted input functions (as described in [[istream.formatted.reqmts]](istream.formatted.reqmts.md)). After a sentry object is constructed, the conversion occurs as if performed by the following code fragment:

```cpp
using numget = num_get<charT, istreambuf_iterator<charT, traits>>;
iostate err = iostate::goodbit;
use_facet<numget>(loc).get(*this, 0, *this, err, val);
setstate(err);
```

In the above fragment, `loc` stands for the private member of the `basic_ios` class. [*Note 1*: The first argument provides an object of the `istreambuf_iterator` class which is an iterator pointed to an input stream. It bypasses istreams and uses streambufs directly. — *end note*] Class `locale` relies on this type as its interface to `istream`, so that it does not need to depend directly on `istream`.

```cpp
operator>>(short& val);
```

**2** The conversion occurs as if performed by the following code fragment (using the same notation as for the preceding code fragment):

```cpp
using numget = num_get<charT, istreambuf_iterator<charT, traits>>;
iostate err = ios_base::goodbit;
long lval;
use_facet<numget>(loc).get(*this, 0, *this, err, lval);
if (lval < numeric_limits<short>::min()) {
  err |= ios_base::failbit;
  val = numeric_limits<short>::min();
} else if (numeric_limits<short>::max() < lval) {
  err |= ios_base::failbit;
  val = numeric_limits<short>::max();
}  else
  val = static_cast<short>(lval);
setstate(err);
```

```cpp
operator>>(int& val);
```

**3** The conversion occurs as if performed by the following code fragment (using the same notation as for the preceding code fragment):

```cpp
using numget = num_get<charT, istreambuf_iterator<charT, traits>>;
iostate err = ios_base::goodbit;
long lval;
use_facet<numget>(loc).get(*this, 0, *this, err, lval);
if (lval < numeric_limits<int>::min()) {
  err |= ios_base::failbit;
  val = numeric_limits<int>::min();
} else if (numeric_limits<int>::max() < lval) {
  err |= ios_base::failbit;
  val = numeric_limits<int>::max();
}  else
  val = static_cast<int>(lval);
setstate(err);
```
