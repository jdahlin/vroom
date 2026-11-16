# 13.5.1.1 Logical operations [temp.constr.op]

[Contents](../README.md) · up: [13.5.1 Constraints \[temp.constr.constr\]](temp.constr.constr.md) · prev: [13.5.1 Constraints \[temp.constr.constr\]](temp.constr.constr.md) · next: [13.5.1.2 Atomic constraints \[temp.constr.atomic\]](temp.constr.atomic.md)

**1** There are two binary logical operations on constraints: conjunction and disjunction. [*Note 1*: These logical operations have no corresponding C++ syntax. For the purpose of exposition, conjunction is spelled using the symbol ∧ and disjunction is spelled using the symbol ∨. The operands of these operations are called the left and right operands. In the constraint *A* ∧ *B*, *A* is the left operand, and *B* is the right operand. — *end note*]

**2** A *conjunction* is a constraint taking two operands. To determine if a conjunction is *satisfied*, the satisfaction of the first operand is checked. If that is not satisfied, the conjunction is not satisfied. Otherwise, the conjunction is satisfied if and only if the second operand is satisfied.

**3** A *disjunction* is a constraint taking two operands. To determine if a disjunction is *satisfied*, the satisfaction of the first operand is checked. If that is satisfied, the disjunction is satisfied. Otherwise, the disjunction is satisfied if and only if the second operand is satisfied.

**4** [*Example 1*:

```cpp
template<typename T>
  constexpr bool get_value() { return T::value; }

template<typename T>
  requires (sizeof(T) > 1) && (get_value<T>())
    void f(T);      // has associated constraint sizeof(T) > 1 ∧ get_value<T>()

void f(int);

f('a'); // OK: calls f(int)
```

In the satisfaction of the associated constraints ([[temp.constr.decl]](temp.constr.decl.md)) of `f`, the constraint `sizeof(char) > 1` is not satisfied; the second operand is not checked for satisfaction. — *end example*]

**5** [*Note 2*: A logical negation expression ([[expr.unary.op]](../expr/expr.unary.op.md)) is an atomic constraint; the negation operator is not treated as a logical operation on constraints. As a result, distinct negation *constraint-expression*s that are equivalent under [[temp.over.link]](temp.over.link.md) do not subsume one another under [[temp.constr.order]](temp.constr.order.md). Furthermore, if substitution to determine whether an atomic constraint is satisfied ([[temp.constr.atomic]](temp.constr.atomic.md)) encounters a substitution failure, the constraint is not satisfied, regardless of the presence of a negation operator. [*Example 2*:

```cpp
template <class T> concept sad = false;

template <class T> int f1(T) requires (!sad<T>);
template <class T> int f1(T) requires (!sad<T>) && true;
int i1 = f1(42);        // ambiguous, !sad<T> atomic constraint expressions ([temp.constr.atomic])
                        // are not formed from the same expression

template <class T> concept not_sad = !sad<T>;
template <class T> int f2(T) requires not_sad<T>;
template <class T> int f2(T) requires not_sad<T> && true;
int i2 = f2(42);        // OK, !sad<T> atomic constraint expressions both come from not_sad

template <class T> int f3(T) requires (!sad<typename T::type>);
int i3 = f3(42);        // error: associated constraints not satisfied due to substitution failure

template <class T> concept sad_nested_type = sad<typename T::type>;
template <class T> int f4(T) requires (!sad_nested_type<T>);
int i4 = f4(42);        // OK, substitution failure contained within sad_nested_type
```

Here, `requires (!sad<typename T::type>)` requires that there is a nested `type` that is not `sad`, whereas `requires (!sad_nested_type<T>)` requires that there is no `sad` nested `type`. — *end example*] — *end note*]
