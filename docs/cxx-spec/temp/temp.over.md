# 13.10.3 Overload resolution [temp.over]

[Contents](../README.md) · up: [13.10 Function template specializations \[temp.fct.spec\]](temp.fct.spec.md) · prev: [13.10.2.6 Deducing template arguments from a function declaration \[temp.deduct.decl\]](temp.deduct.decl.md) · next: [14 Exception handling \[except\]](../except/except.md)

**1** When a call to the name of a function or function template is written (explicitly, or implicitly using the operator notation), template argument deduction ([[temp.deduct]](temp.deduct.md)) and checking of any explicit template arguments ([[temp.arg]](temp.arg.md)) are performed for each function template to find the template argument values (if any) that can be used with that function template to instantiate a function template specialization that can be invoked with the call arguments. For each function template, if the argument deduction and checking succeeds, the *template-argument*s (deduced and/or explicit) are used to synthesize the declaration of a single function template specialization which is added to the candidate functions set to be used in overload resolution. If, for a given function template, argument deduction fails or the synthesized function template specialization would be ill-formed, no such function is added to the set of candidate functions for that template. The complete set of candidate functions includes all the synthesized declarations and all of the non-template overloaded functions of the same name. The synthesized declarations are treated like any other functions in the remainder of overload resolution, except as explicitly noted in [[over.match.best]](../over/over.match.best.md).[^1]

**2** [*Example 1*:

```cpp
template<class T> T max(T a, T b) { return a>b?a:b; }

void f(int a, int b, char c, char d) {
  int m1 = max(a,b);            // max(int a, int b)
  char m2 = max(c,d);           // max(char a, char b)
  int m3 = max(a,c);            // error: cannot generate max(int,char)
}
```

Adding the non-template function

```cpp
int max(int,int);
```

to the example above would resolve the third call, by providing a function that could be called for `max(a,c)` after using the standard conversion of `char` to `int` for `c`. — *end example*]

**3** [*Example 2*: Here is an example involving conversions on a function argument involved in *template-argument* deduction:

```cpp
template<class T> struct B { /* ... */ };
template<class T> struct D : public B<T> { /* ... */ };
template<class T> void f(B<T>&);

void g(B<int>& bi, D<int>& di) {
  f(bi);            // f(bi)
  f(di);            // f((B<int>&)di)
}
```

— *end example*]

**4** [*Example 3*: Here is an example involving conversions on a function argument not involved in *template-parameter* deduction:

```cpp
template<class T> void f(T*,int);       // #1
template<class T> void f(T,char);       // #2

void h(int* pi, int i, char c) {
  f(pi,i);          // #1: f<int>(pi,i)
  f(pi,c);          // #2: f<int*>(pi,c)

  f(i,c);           // #2: f<int>(i,c);
  f(i,i);           // #2: f<int>(i,char(i))
}
```

— *end example*]

**5** Only the signature of a function template specialization is needed to enter the specialization in a set of candidate functions. Therefore only the function template declaration is needed to resolve a call for which a template specialization is a candidate. [*Example 4*:

```cpp
template<class T> void f(T);    // declaration

void g() {
  f("Annemarie");               // call of f<const char*>
}
```

The call of `f` is well-formed even if the template `f` is only declared and not defined at the point of the call. The program will be ill-formed unless a specialization for `f<const char*>`, either implicitly or explicitly generated, is present in some translation unit. — *end example*]

[^1]: The parameters of function template specializations contain no template parameter types. The set of conversions allowed on deduced arguments is limited, because the argument deduction process produces function templates with parameters that either match the call arguments exactly or differ only in ways that can be bridged by the allowed limited conversions. Non-deduced arguments allow the full range of conversions. Note also that [[over.match.best]](../over/over.match.best.md) specifies that a non-template function will be given preference over a template specialization if the two functions are otherwise equally good candidates for an overload match.
