# 17.6 Dynamic memory management [support.dynamic]

[Contents](../../README.md) · up: [17 Language support library \[support\]](support.md) · prev: [17.5 Startup and termination \[support.start.term\]](support.start.term.md) · next: [17.6.1 Header `<new>` synopsis \[new.syn\]](new.syn.md)

**1** The header `<new>` defines several functions that manage the allocation of dynamic storage in a program. It also defines components for reporting storage management errors.

## Subclauses

- [17.6.1 Header `<new>` synopsis \[new.syn\]](new.syn.md)
- [17.6.2 Storage allocation and deallocation \[new.delete\]](new.delete.md)
  - [17.6.2.1 Single-object forms \[new.delete.single\]](new.delete.single.md)
  - [17.6.2.2 Array forms \[new.delete.array\]](new.delete.array.md)
  - [17.6.2.3 Non-allocating forms \[new.delete.placement\]](new.delete.placement.md)
  - [17.6.2.4 Data races \[new.delete.dataraces\]](new.delete.dataraces.md)
- [17.6.3 Storage allocation errors \[alloc.errors\]](alloc.errors.md)
  - [17.6.3.1 Class `bad_alloc` \[bad.alloc\]](bad.alloc.md)
  - [17.6.3.2 Class `bad_array_new_length` \[new.badlength\]](new.badlength.md)
  - [17.6.3.3 Type `new_handler` \[new.handler\]](new.handler.md)
  - [17.6.3.4 `set_new_handler` \[set.new.handler\]](set.new.handler.md)
  - [17.6.3.5 `get_new_handler` \[get.new.handler\]](get.new.handler.md)
- [17.6.4 Pointer optimization barrier \[ptr.launder\]](ptr.launder.md)
- [17.6.5 Hardware interference size \[hardware.interference\]](hardware.interference.md)
