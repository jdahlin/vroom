# 29.11.7.5 Iterators [fs.path.itr]

[Contents](../../README.md) · up: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md) · prev: [29.11.7.4.11 Generation \[fs.path.gen\]](fs.path.gen.md) · next: [29.11.7.6 Inserter and extractor \[fs.path.io\]](fs.path.io.md)

**1** Path iterators iterate over the elements of the pathname in the generic format ([[fs.path.generic]](fs.path.generic.md)).

**2** A `path::iterator` is a constant iterator meeting all the requirements of a bidirectional iterator ([[bidirectional.iterators]](../iterators/bidirectional.iterators.md)) except that, for dereferenceable iterators `a` and `b` of type `path::iterator` with `a == b`, there is no requirement that `*a` and `*b` are bound to the same object. Its `value_type` is `path`.

**3** Calling any non-const member function of a `path` object invalidates all iterators referring to elements of that object.

**4** For the elements of the pathname in the generic format, the forward traversal order is as follows:

- The *root-name* element, if present.
- The *root-directory* element, if present. [*Note 1*: The generic format is required to ensure lexicographical comparison works correctly. — *end note*]
- Each successive *filename* element, if present.
- An empty element, if a trailing non-root *directory-separator* is present.

**5** The backward traversal order is the reverse of forward traversal.

```cpp
iterator begin() const;
```

**6** *Returns:* An iterator for the first present element in the traversal list above. If no elements are present, the end iterator.

```cpp
iterator end() const;
```

**7** *Returns:* The end iterator.
