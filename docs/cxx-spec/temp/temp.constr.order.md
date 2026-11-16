# 13.5.4 Partial ordering by constraints [temp.constr.order]

[Contents](../README.md) · up: [13.5 Template constraints \[temp.constr\]](temp.constr.md) · prev: [13.5.3 Constraint normalization \[temp.constr.normal\]](temp.constr.normal.md) · next: [13.6 Type equivalence \[temp.type\]](temp.type.md)

**1** A constraint *P* *subsumes* a constraint *Q* if and only if, for every disjunctive clause *P*<sub>*i*</sub> in the disjunctive normal form[^1] of *P*, *P*<sub>*i*</sub> subsumes every conjunctive clause *Q*<sub>*j*</sub> in the conjunctive normal form[^2] of *Q*, where

- a disjunctive clause *P*<sub>*i*</sub> subsumes a conjunctive clause *Q*<sub>*j*</sub> if and only if there exists an atomic constraint *P*<sub>*ia*</sub> in *P*<sub>*i*</sub> for which there exists an atomic constraint *Q*<sub>*jb*</sub> in *Q*<sub>*j*</sub> such that *P*<sub>*ia*</sub> subsumes *Q*<sub>*jb*</sub>, and
- an atomic constraint *A* subsumes another atomic constraint *B* if and only if *A* and *B* are identical using the rules described in [[temp.constr.atomic]](temp.constr.atomic.md).

[*Example 3*: Let *A* and *B* be atomic constraints ([[temp.constr.atomic]](temp.constr.atomic.md)). The constraint *A* ∧ *B* subsumes *A*, but *A* does not subsume *A* ∧ *B*. The constraint *A* subsumes *A* ∨ *B*, but *A* ∨ *B* does not subsume *A*. Also note that every constraint subsumes itself. — *end example*]

**2** [*Note 1*: The subsumption relation defines a partial ordering on constraints. This partial ordering is used to determine

- the best viable candidate of non-template functions ([[over.match.best]](../over/over.match.best.md)),
- the address of a non-template function ([[over.over]](../over/over.over.md)),
- the matching of template template arguments ([[temp.arg.template]](temp.arg.template.md)),
- the partial ordering of class template specializations ([[temp.class.order]](temp.class.order.md)), and
- the partial ordering of function templates ([[temp.func.order]](temp.func.order.md)).

— *end note*]

**3** A declaration `D1` is *at least as constrained* as a declaration `D2` if

- `D1` and `D2` are both constrained declarations and `D1`’s associated constraints subsume those of `D2`; or
- `D2` has no associated constraints.

**4** A declaration `D1` is *more constrained* than another declaration `D2` when `D1` is at least as constrained as `D2`, and `D2` is not at least as constrained as `D1`. [*Example 4*:

```cpp
template<typename T> concept C1 = requires(T t) { --t; };
template<typename T> concept C2 = C1<T> && requires(T t) { *t; };

template<C1 T> void f(T);       // #1
template<C2 T> void f(T);       // #2
template<typename T> void g(T); // #3
template<C1 T> void g(T);       // #4

f(0);                           // selects #1
f((int*)0);                     // selects #2
g(true);                        // selects #3 because C1<bool> is not satisfied
g(0);                           // selects #4
```

— *end example*]

[^1]: A constraint is in disjunctive normal form when it is a disjunction of clauses where each clause is a conjunction of atomic constraints. [*Example 1*: For atomic constraints *A*, *B*, and *C*, the disjunctive normal form of the constraint *A* ∧ (*B* ∨ *C*) is (*A* ∧ *B*) ∨ (*A* ∧ *C*). Its disjunctive clauses are (*A* ∧ *B*) and (*A* ∧ *C*). — *end example*]
[^2]: A constraint is in conjunctive normal form when it is a conjunction of clauses where each clause is a disjunction of atomic constraints. [*Example 2*: For atomic constraints *A*, *B*, and *C*, the constraint *A* ∧ (*B* ∨ *C*) is in conjunctive normal form. Its conjunctive clauses are *A* and (*B* ∨ *C*). — *end example*]
