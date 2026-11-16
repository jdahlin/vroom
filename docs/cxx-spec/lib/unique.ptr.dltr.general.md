# 20.11.1.1.1 In general [unique.ptr.dltr.general]

[Contents](../README.md) · up: [20.11.1.1 Default deleters \[unique.ptr.dltr\]](unique.ptr.dltr.md) · prev: [20.11.1.1 Default deleters \[unique.ptr.dltr\]](unique.ptr.dltr.md) · next: [20.11.1.1.2 `default_delete` \[unique.ptr.dltr.dflt\]](unique.ptr.dltr.dflt.md)

**1** The class template `default_delete` serves as the default deleter (destruction policy) for the class template `unique_ptr`.

**2** The template parameter `T` of `default_delete` may be an incomplete type.
