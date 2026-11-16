# 23.5.1.2 Requirements [reverse.iter.requirements]

[Contents](../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1.1 Class template `reverse_iterator` \[reverse.iterator\]](reverse.iterator.md) · next: [23.5.1.3 Construction and assignment \[reverse.iter.cons\]](reverse.iter.cons.md)

**1** The template parameter `Iterator` shall either meet the requirements of a *Cpp17BidirectionalIterator* ([[bidirectional.iterators]](bidirectional.iterators.md)) or model `bidirectional_iterator` ([[iterator.concept.bidir]](iterator.concept.bidir.md)).

**2** Additionally, `Iterator` shall either meet the requirements of a *Cpp17RandomAccessIterator* ([[random.access.iterators]](random.access.iterators.md)) or model `random_access_iterator` ([[iterator.concept.random.access]](iterator.concept.random.access.md)) if the definitions of any of the members

- `operator+`, `operator-`, `operator+=`, `operator-=` ([[reverse.iter.nav]](reverse.iter.nav.md)), or
- `operator[]` ([[reverse.iter.elem]](reverse.iter.elem.md)),

or the non-member operators ([[reverse.iter.cmp]](reverse.iter.cmp.md))

- `operator<`, `operator>`, `operator<=`, `operator>=`, `operator-`, or `operator+` ([[reverse.iter.nonmember]](reverse.iter.nonmember.md))

are instantiated ([[temp.inst]](../temp.inst.md)).
