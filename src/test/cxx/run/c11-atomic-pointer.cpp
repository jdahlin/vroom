/* [ext:clang_atomic] _Atomic as a qualifier after the *: T *_Atomic is
   _Atomic(T *), a pointer with C11's atomic layout and operations (vcc's
   declaration-cache.cpp: struct node *_Atomic children) */
struct node {
    int value;
    struct node *_Atomic next;
    const int *const _Atomic link;
};
static_assert(sizeof(node *_Atomic) == sizeof(node *), "same size");
static node a = {1, nullptr, nullptr};
static node b = {2, &a, nullptr};
int main()
{
    node *_Atomic head = &b;
    node *_Atomic *where = &head;
    node *first = *where;
    first->next = &a;
    return first->next->value + first->value - 3;
}
