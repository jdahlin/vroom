# 7.3.5 Qualification conversions [conv.qual]

[Contents](../README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3.4 Temporary materialization conversion \[conv.rval\]](conv.rval.md) · next: [7.3.6 Integral promotions \[conv.prom\]](conv.prom.md)

**1** A *cv-decomposition* of a type `T` is a sequence of *cv*<sub>*i*</sub> and *P*<sub>*i*</sub> such that `T` is “*cv*<sub>0</sub> *P*<sub>0</sub> *cv*<sub>1</sub> *P*<sub>1</sub> … *cv*<sub>*n*-1</sub> *P*<sub>*n*-1</sub> *cv*<sub>*n*</sub> `U`” for *n* ≥ 0, where each *cv*<sub>*i*</sub> is a set of cv-qualifiers ([[basic.type.qualifier]](../basic/basic.type.qualifier.md)), and each *P*<sub>*i*</sub> is “pointer to” ([[dcl.ptr]](../dcl/dcl.ptr.md)), “pointer to member of class *C*<sub>*i*</sub> of type” ([[dcl.mptr]](../dcl/dcl.mptr.md)), “array of *N*<sub>*i*</sub>”, or “array of unknown bound of” ([[dcl.array]](../dcl/dcl.array.md)). If *P*<sub>*i*</sub> designates an array, the cv-qualifiers *cv*<sub>*i*+1</sub> on the element type are also taken as the cv-qualifiers *cv*<sub>*i*</sub> of the array. [*Example 1*: The type denoted by the *type-id* `const int **` has three cv-decompositions, taking `U` as “`int`”, as “pointer to `const int`”, and as “pointer to pointer to `const int`”. — *end example*] The *n*-tuple of cv-qualifiers after the first one in the longest cv-decomposition of `T`, that is, *cv*<sub>1</sub>, *cv*<sub>2</sub>, …, *cv*<sub>*n*</sub>, is called the *cv-qualification signature* of `T`.

**2** Two types `T1` and `T2` are *similar* if they have cv-decompositions with the same *n* such that corresponding *P*<sub>*i*</sub> components are either the same or one is “array of *N*<sub>*i*</sub>” and the other is “array of unknown bound of”, and the types denoted by `U` are the same.

**3** The *cv-combined type* of two types `T1` and `T2` is the type `T3` similar to `T1` whose cv-decomposition is such that:

- for every *i* > 0, *cv*<sup>3</sup><sub>*i*</sub> is the union of *cv*<sup>1</sup><sub>*i*</sub> and *cv*<sup>2</sup><sub>*i*</sub>;
- if either *P*<sup>1</sup><sub>*i*</sub> or *P*<sup>2</sup><sub>*i*</sub> is “array of unknown bound of”, *P*<sup>3</sup><sub>*i*</sub> is “array of unknown bound of”, otherwise it is *P*<sup>1</sup><sub>*i*</sub>;
- if the resulting *cv*<sup>3</sup><sub>*i*</sub> is different from *cv*<sup>1</sup><sub>*i*</sub> or *cv*<sup>2</sup><sub>*i*</sub>, or the resulting *P*<sup>3</sup><sub>*i*</sub> is different from *P*<sup>1</sup><sub>*i*</sub> or *P*<sup>2</sup><sub>*i*</sub>, then `const` is added to every *cv*<sup>3</sup><sub>*k*</sub> for 0 &lt; *k* &lt; *i*.

where *cv*<sup>*j*</sup><sub>*i*</sub> and *P*<sup>*j*</sup><sub>*i*</sub> are the components of the cv-decomposition of `T`*j*. A prvalue of type `T1` can be converted to type `T2` if the cv-combined type of `T1` and `T2` is `T2`. [*Note 1*: If a program could assign a pointer of type `T**` to a pointer of type `const` `T**` (that is, if line #1 below were allowed), a program could inadvertently modify a const object (as it is done on line #2). For example,

```cpp
int main() {
  const char c = 'c';
  char* pc;
  const char** pcc = &pc;       // #1: not allowed
  *pcc = &c;
  *pc = 'C';                    // #2: modifies a const object
}
```

— *end note*] [*Note 2*: Given similar types `T1` and `T2`, this construction ensures that both can be converted to the cv-combined type of `T1` and `T2`. — *end note*]

**4** [*Note 3*: A prvalue of type “pointer to *cv1* `T`” can be converted to a prvalue of type “pointer to *cv2* `T`” if “*cv2* `T`” is more cv-qualified than “*cv1* `T`”. A prvalue of type “pointer to member of `X` of type *cv1* `T`” can be converted to a prvalue of type “pointer to member of `X` of type *cv2* `T`” if “*cv2* `T`” is more cv-qualified than “*cv1* `T`”. — *end note*]

**5** [*Note 4*: Function types (including those used in pointer-to-member-function types) are never cv-qualified ([[dcl.fct]](../dcl/dcl.fct.md)). — *end note*]
