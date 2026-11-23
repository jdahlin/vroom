# 26.6.2.6 Random number distribution requirements [rand.req.dist]

[Contents](../../README.md) · up: [26.6.2 Requirements \[rand.req\]](rand.req.md) · prev: [26.6.2.5 Random number engine adaptor requirements \[rand.req.adapt\]](rand.req.adapt.md) · next: [26.6.3 Random number engine class templates \[rand.eng\]](rand.eng.md)

**1** A *random number distribution* (commonly shortened to *distribution*) `d` of type `D` is a function object returning values that are distributed according to an associated mathematical *probability density function* *p*(*z*) or according to an associated *discrete probability function* *P*(*z*<sub>*i*</sub>). A distribution’s specification identifies its associated probability function *p*(*z*) or *P*(*z*<sub>*i*</sub>).

**2** An associated probability function is typically expressed using certain externally-supplied quantities known as the *parameters of the distribution*. Such distribution parameters are identified in this context by writing, for example, *p*(*z*|*a*,*b*) or *P*(*z*<sub>*i*</sub>|*a*,*b*), to name specific parameters, or by writing, for example, $p(z\,|\left\{\texttt{p}\right\})$ or $P(z_i\,|\left\{\texttt{p}\right\})$, to denote a distribution’s parameters `p` taken as a whole.

**3** A class `D` meets the requirements of a *random number distribution* if the expressions shown in [Table 95](rand.req.dist.md#tab:rand.req.dist) are valid and have the indicated semantics, and if `D` and its associated types also meet all other requirements of this subclause [[rand.req.dist]](rand.req.dist.md). In that Table and throughout this subclause,

- `T` is the type named by `D`’s associated `result_type`;
- `P` is the type named by `D`’s associated `param_type`;
- `d` is a value of `D`, and `x` and `y` are (possibly `const`) values of `D`;
- `glb` and `lub` are values of `T` respectively corresponding to the greatest lower bound and the least upper bound on the values potentially returned by `d`’s `operator()`, as determined by the current values of `d`’s parameters;
- `p` is a (possibly `const`) value of `P`;
- `g`, `g1`, and `g2` are lvalues of a type meeting the requirements of a uniform random bit generator ([[rand.req.urng]](rand.req.urng.md));
- `os` is an lvalue of the type of some class template specialization `basic_ostream<charT,` `traits>`; and
- `is` is an lvalue of the type of some class template specialization `basic_istream<charT,` `traits>`;

where `charT` and `traits` are constrained according to [[strings]](../strings/strings.md) and [[input.output]](../io/input.output.md).

<a id="tab:rand.req.dist"></a>
**Table 95: Random number distribution requirements** [tab:rand.req.dist]

| Expression | Return type | Pre/post-condition | Complexity |
| --- | --- | --- | --- |
| `D::result_type` | `T` | `T` is an arithmetic type ([[basic.fundamental]](../../basic/basic.fundamental.md)). | compile-time |
| `D::param_type` | `P` |  | compile-time |
| `D()` |  | Creates a distribution whose behavior is indistinguishable from that of any other newly default-constructed distribution of type `D`. | constant |
| `D(p)` |  | Creates a distribution whose behavior is indistinguishable from that of a distribution newly constructed directly from the values used to construct `p`. | same as `p`’s construction |
| `d.reset()` | `void` | Subsequent uses of `d` do not depend on values produced by any engine prior to invoking `reset`. | constant |
| `x.param()` | `P` | Returns a value `p` such that `D(p).param() == p`. | no worse than the complexity of `D(p)` |
| `d.param(p)` | `void` | *Postconditions:* `d.param() == p`. | no worse than the complexity of `D(p)` |
| `d(g)` | `T` | With `p` = `d.param()`, the sequence of numbers returned by successive invocations with the same object `g` is randomly distributed according to the associated $p(z\,\|\left\{\texttt{p}\right\})$ or $P(z_i\,\|\left\{\texttt{p}\right\})$ function. | amortized constant number of invocations of `g` |
| `d(g,p)` | `T` | The sequence of numbers returned by successive invocations with the same objects `g` and `p` is randomly distributed according to the associated $p(z\,\|\left\{\texttt{p}\right\})$ or $P(z_i\,\|\left\{\texttt{p}\right\})$ function. | amortized constant number of invocations of `g` |
| `x.min()` | `T` | Returns `glb`. | constant |
| `x.max()` | `T` | Returns `lub`. | constant |
| `x == y` | `bool` | This operator is an equivalence relation. Returns `true` if `x.param() == y.param()` and *S*<sub>1</sub> = *S*<sub>2</sub>, where *S*<sub>1</sub> and *S*<sub>2</sub> are the infinite sequences of values that would be generated, respectively, by repeated future calls to `x(g1)` and `y(g2)` whenever `g1 == g2`. Otherwise returns `false`. | constant |
| `x != y` | `bool` | `!(x == y)`. | same as `x == y`. |
| `os << x` | reference to the type of `os` | Writes to `os` a textual representation for the parameters and the additional internal data of `x`.<br>*Postconditions:* The `os.`*fmtflags* and fill character are unchanged. |  |
| `is >> d` | reference to the type of `is` | Restores from `is` the parameters and additional internal data of the lvalue `d`. If bad input is encountered, ensures that `d` is unchanged by the operation and calls `is.setstate(ios_base::failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](../io/iostate.flags.md))).<br>*Preconditions:* `is` provides a textual representation that was previously written using an `os` whose imbued locale and whose type’s template specialization arguments `charT` and `traits` were the same as those of `is`.<br>*Postconditions:* The `is.`*fmtflags* are unchanged. |  |

**4** `D` shall meet the *Cpp17CopyConstructible* ([Table 29](../library/utility.arg.requirements.md#tab:cpp17.copyconstructible)) and *Cpp17CopyAssignable* ([Table 31](../library/utility.arg.requirements.md#tab:cpp17.copyassignable)) requirements.

**5** The sequence of numbers produced by repeated invocations of `d(g)` shall be independent of any invocation of `os << d` or of any `const` member function of `D` between any of the invocations `d(g)`.

**6** If a textual representation is written using `os << x` and that representation is restored into the same or a different object `y` of the same type using `is >> y`, repeated invocations of `y(g)` shall produce the same sequence of numbers as would repeated invocations of `x(g)`.

**7** It is unspecified whether `D::param_type` is declared as a (nested) `class` or via a `typedef`. In this subclause [[rand]](rand.md), declarations of `D::param_type` are in the form of `typedef`s for convenience of exposition only.

**8** `P` shall meet the *Cpp17CopyConstructible* ([Table 29](../library/utility.arg.requirements.md#tab:cpp17.copyconstructible)), *Cpp17CopyAssignable* ([Table 31](../library/utility.arg.requirements.md#tab:cpp17.copyassignable)), and *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) requirements.

**9** For each of the constructors of `D` taking arguments corresponding to parameters of the distribution, `P` shall have a corresponding constructor subject to the same requirements and taking arguments identical in number, type, and default values. Moreover, for each of the member functions of `D` that return values corresponding to parameters of the distribution, `P` shall have a corresponding member function with the identical name, type, and semantics.

**10** `P` shall have a declaration of the form

```cpp
using distribution_type =  D;
```
