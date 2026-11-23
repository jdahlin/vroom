/* [abi:mangle.decltype]: a bare identifier or member access uses Dt.
   Parentheses around the operand select DT. */
struct record {
    int value;
};

template <class T> auto copy(T value) -> decltype(value) { return value; }
template <class T> auto member(T object) -> decltype(object.value)
{
    return object.value;
}
template <class T> auto pointed_member(T *object) -> decltype(object->value)
{
    return object->value;
}
template <class T> auto alias(T &value) -> decltype((value)) { return value; }

/* [abi:mangling-compression]: identical signatures use separate candidates
   for free functions and for member functions of distinct classes. */
struct A {
    int get() { return 1; }
};
struct B {
    int get() { return 2; }
};

int get() { return 3; }

int invoke(int (*free_function)(), int (A::*first)(), int (B::*second)(),
           int (A::*again)(), int (*free_again)())
{
    A a;
    B b;
    return free_function() + (a.*first)() + (b.*second)() +
           (a.*again)() + free_again();
}

int main()
{
    record object{4};
    alias(object.value) = 5;
    return copy(object.value) == 5 && member(object) == 5 &&
           pointed_member(&object) == 5 &&
           invoke(get, &A::get, &B::get, &A::get, get) == 10 ? 0 : 1;
}
