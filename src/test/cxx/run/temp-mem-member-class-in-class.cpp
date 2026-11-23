/* [temp.mem]/1, [temp.mem.class]: a member class template defined in its class, its member function and static member */
struct O {
    template <class T> struct In {
        T v;
        T get() const { return v; }
        static constexpr int size = sizeof(T);
    };
};
int main()
{
    O::In<int> a = {4};
    return a.get() - 4 + O::In<long>::size - 8;
}
