/* [temp.inst]/4: a member function's definition is instantiated when that
   member is needed, not when another overload of its name is: libc++'s
   vector::resize(n) and resize(n, const T&), both defined outside the
   class; resize(n) of a move-only T must not instantiate the copying one
   (Stockfish's vector<unique_ptr<T>>::resize in thread.h, after which
   allocator::construct found no viable constructor). */
struct move_only {
    int v = 1;
    move_only() = default;
    move_only(const move_only &) = delete;
    move_only(move_only &&) = default;
    move_only &operator=(move_only &&) = default;
};
template <class T> struct vec {
    T items[4];
    int size = 0;
    void resize(int n);
    void resize(int n, const T &value);
    int count() const;
    int count(int) const;
};
template <class T> void vec<T>::resize(int n)
{
    for (int i = size; i < n; i++)
        items[i] = T();
    size = n;
}
template <class T> void vec<T>::resize(int n, const T &value)
{
    for (int i = size; i < n; i++) {
        T copy(value);
        items[i] = static_cast<T &&>(copy);
    }
    size = n;
}
template <class T> int vec<T>::count() const { return size; }
template <class T> int vec<T>::count(int) const { return -1; }
int main()
{
    vec<move_only> v;
    v.resize(3);
    if (v.count() != 3 || v.items[2].v != 1)
        return 1;
    vec<int> w;
    w.resize(2, 7);
    if (w.count() != 2 || w.items[1] != 7 || w.count(0) != -1)
        return 2;
    return 0;
}
