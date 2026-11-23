# 6.4.1 Declarative regions and scopes [basic.scope.declarative]

[Contents](../README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4 Scope \[basic.scope\]](basic.scope.md) · next: [6.4.2 Point of declaration \[basic.scope.pdecl\]](basic.scope.pdecl.md)

**1** Every name is introduced in some portion of program text called a *declarative region*, which is the largest part of the program in which that name is valid, that is, in which that name may be used as an unqualified name to refer to the same entity. In general, each particular name is valid only within some possibly discontiguous portion of program text called its *scope*. To determine the scope of a declaration, it is sometimes convenient to refer to the *potential scope* of a declaration. The scope of a declaration is the same as its potential scope unless the potential scope contains another declaration of the same name. In that case, the potential scope of the declaration in the inner (contained) declarative region is excluded from the scope of the declaration in the outer (containing) declarative region.

**2** [*Example 1*: In

```cpp
int j = 24;
int main() {
  int i = j, j;
  j = 42;
}
```

the identifier `j` is declared twice as a name (and used twice). The declarative region of the first `j` includes the entire example. The potential scope of the first `j` begins immediately after that `j` and extends to the end of the program, but its (actual) scope excludes the text between the `,` and the `}`. The declarative region of the second declaration of `j` (the `j` immediately before the semicolon) includes all the text between `{` and `}`, but its potential scope excludes the declaration of `i`. The scope of the second declaration of `j` is the same as its potential scope. — *end example*]

**3** The names declared by a declaration are introduced into the scope in which the declaration occurs, except that the presence of a `friend` specifier ([[class.friend]](../class/class.friend.md)), certain uses of the *elaborated-type-specifier* ([[dcl.type.elab]](../dcl/dcl.type.elab.md)), and *using-directive*s ([[namespace.udir]](../dcl/namespace.udir.md)) alter this general behavior.

**4** Given a set of declarations in a single declarative region, each of which specifies the same unqualified name,

- they shall all refer to the same entity, or all refer to functions and function templates; or
- exactly one declaration shall declare a class name or enumeration name that is not a typedef name and the other declarations shall all refer to the same variable, non-static data member, or enumerator, or all refer to functions and function templates; in this case the class name or enumeration name is hidden ([[basic.scope.hiding]](basic.scope.hiding.md)). [*Note 1*: A structured binding ([[dcl.struct.bind]](../dcl/dcl.struct.bind.md)), namespace name ([[basic.namespace]](../dcl/basic.namespace.md)), or class template name ([[temp.pre]](../temp/temp.pre.md)) must be unique in its declarative region. — *end note*]

[*Note 2*: These restrictions apply to the declarative region into which a name is introduced, which is not necessarily the same as the region in which the declaration occurs. In particular, *elaborated-type-specifier*s ([[dcl.type.elab]](../dcl/dcl.type.elab.md)) and friend declarations ([[class.friend]](../class/class.friend.md)) may introduce a (possibly not visible) name into an enclosing namespace; these restrictions apply to that region. Local extern declarations ([[basic.link]](basic.link.md)) may introduce a name into the declarative region where the declaration appears and also introduce a (possibly not visible) name into an enclosing namespace; these restrictions apply to both regions. — *end note*]

**5** For a given declarative region *R* and a point *P* outside *R*, the set of *intervening* declarative regions between *P* and *R* comprises all declarative regions that are or enclose *R* and do not enclose *P*.

**6** [*Note 3*: The name lookup rules are summarized in [[basic.lookup]](basic.lookup.md). — *end note*]
