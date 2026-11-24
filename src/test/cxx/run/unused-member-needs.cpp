/* [temp.inst]/4, [class.copy.assign]/12, [class.virtual]/17: what the body
   of a member nobody uses would need (here ~Owner<T>, which wants a
   complete T) is not instantiated; the implicit assignment operators and
   destructor of a class holding an Owner<Incomplete>, and the virtual
   destructor of a class derived from a polymorphic base, are not used
   before the class is complete */
template <class T> struct Owner {
    T *p = nullptr;
    Owner() = default;
    Owner(const Owner &) = delete;
    Owner &operator=(const Owner &) = delete;
    ~Owner()
    {
        static_assert(sizeof(T) > 0, "T must be complete");
        delete p;
    }
};

template <class T> struct Array {
    Owner<T> *data = nullptr;
    int size = 0;
    ~Array() { clear(); }
    void clear()
    {
        for (int i = 0; i < size; i++)
            data[i].~Owner<T>();
        size = 0;
    }
    Array &operator=(const Array &other)
    {
        clear();
        copy(other);
        return *this;
    }
    void copy(const Array &other)
    {
        for (int i = 0; i < size; i++)
            new (data + i) Owner<T>(other.data[i]);
    }
};

struct Incomplete;

struct Base {
    virtual ~Base() {}
};

struct Holder : Base {
    Array<Incomplete> items;
    Holder();
};

struct Incomplete {
    int v;
};

Holder::Holder() {}

struct Plain {
    Array<Incomplete> items;
};

int main()
{
    Holder h;
    Plain q;
    if (h.items.size != 0 || q.items.size != 0)
        return 1;
    return 0;
}
