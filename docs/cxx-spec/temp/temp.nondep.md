# 13.8.3 Non-dependent names [temp.nondep]

[Contents](../README.md) · up: [13.8 Name resolution \[temp.res\]](temp.res.md) · prev: [13.8.2.4 Dependent template arguments \[temp.dep.temp\]](temp.dep.temp.md) · next: [13.8.4 Dependent name resolution \[temp.dep.res\]](temp.dep.res.md)

**1** Non-dependent names used in a template definition are found using the usual name lookup and bound at the point they are used. [*Example 1*:

```cpp
void g(double);
void h();

template<class T> class Z {
public:
  void f() {
    g(1);           // calls g(double)
    h++;            // ill-formed: cannot increment function; this could be diagnosed
                    // either here or at the point of instantiation
  }
};

void g(int);        // not in scope at the point of the template definition, not considered for the call g(1)
```

— *end example*]
