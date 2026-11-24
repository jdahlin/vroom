/* [temp.explicit]/11, /14: an explicit instantiation declaration of a class
   template specialization already implicitly instantiated. */
template <class T> struct box {
    T v;
    T get() const;
    T twice() const { return v + v; }
};
template <class T> T box<T>::get() const { return v; }

int use(const box<int> &b) { return b.get() + b.twice(); }

extern template struct box<int>;
template struct box<int>;

int main()
{
    box<int> b = {3};
    return use(b) - 9;
}
