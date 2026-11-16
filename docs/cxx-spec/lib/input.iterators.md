# 23.3.5.2 Input iterators [input.iterators]

[Contents](../README.md) · up: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · prev: [23.3.5.1 *Cpp17Iterator* \[iterator.iterators\]](iterator.iterators.md) · next: [23.3.5.3 Output iterators \[output.iterators\]](output.iterators.md)

**1** A class or pointer type `X` meets the requirements of an input iterator for the value type `T` if `X` meets the *Cpp17Iterator* ([[iterator.iterators]](iterator.iterators.md)) and *Cpp17EqualityComparable* ([Table 25](utility.arg.requirements.md#tab:cpp17.equalitycomparable)) requirements and the expressions in [Table 85](input.iterators.md#tab:inputiterator) are valid and have the indicated semantics.

**2** In [Table 85](input.iterators.md#tab:inputiterator), the term *the domain of `==`* is used in the ordinary mathematical sense to denote the set of values over which `==` is (required to be) defined. This set can change over time. Each algorithm places additional requirements on the domain of `==` for the iterator values it uses. These requirements can be inferred from the uses that algorithm makes of `==` and `!=`. [*Example 1*: The call `find(a,b,x)` is defined only if the value of `a` has the property *p* defined as follows: `b` has property *p* and a value `i` has property *p* if (`*i==x`) or if (`*i!=x` and `++i` has property *p*). — *end example*]

<a id="tab:inputiterator"></a>
**Table 85: *Cpp17InputIterator* requirements (in addition to *Cpp17Iterator*)** [tab:inputiterator]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `a != b` | contextually convertible to `bool` | `!(a == b)` | *Preconditions:* `(a, b)` is in the domain of `==`. |
| `*a` | `reference`, convertible to `T` |  | *Preconditions:* `a` is dereferenceable.<br>The expression<br>`(void)*a, *a` is equivalent to `*a`.<br>If `a == b` and `(a, b)` is in the domain of `==` then `*a` is equivalent to `*b`. |
| `a->m` |  | `(*a).m` | *Preconditions:* `a` is dereferenceable. |
| `++r` | `X&` |  | *Preconditions:* `r` is dereferenceable.<br>*Postconditions:* `r` is dereferenceable or `r` is past-the-end;<br>any copies of the previous value of `r` are no longer required to be dereferenceable nor to be in the domain of `==`. |
| `(void)r++` |  |  | equivalent to `(void)++r` |
| `*r++` | convertible to `T` | `{ T tmp = *r;`<br>`++r;`<br>`return tmp; }` |  |

**3** [*Note 1*: For input iterators, `a == b` does not imply `++a == ++b`. (Equality does not guarantee the substitution property or referential transparency.) Algorithms on input iterators should never attempt to pass through the same iterator twice. They should be *single pass* algorithms. Value type `T` is not required to be a *Cpp17CopyAssignable* type ([Table 31](utility.arg.requirements.md#tab:cpp17.copyassignable)). These algorithms can be used with istreams as the source of the input data through the `istream_iterator` class template. — *end note*]
