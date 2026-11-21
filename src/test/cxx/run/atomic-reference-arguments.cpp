// [ext:clang_atomic] Atomic lvalues bind to references during overload
// resolution and are loaded when passed by value.
template<class T> T* address(T& x) { return __builtin_addressof(x); }
template<class T> T* address(const T&&) = delete;
template<class T> struct Box { _Atomic(T) value; };
template<class T> _Atomic(T)* get(Box<T>* b) { return address(b->value); }
int read(int value) { return value; }
int main() {
    Box<int> box;
    box.value = 42;
    return get(&box) != &box.value || read(box.value) != 42;
}
