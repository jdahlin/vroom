// [temp.class.spec.mfunc]/1: out-of-class member function definitions of a
// class template partial specialization when the primary template is only
// declared and the specialization has a template parameter pack.
template <class Sig, class... Extra> struct Callable;

template <class R, class... Extra>
struct Callable<R*, Extra...> {
    int count() const;
};

template <class R, class... Extra>
int Callable<R*, Extra...>::count() const {
    return sizeof(R) + sizeof...(Extra);
}

int main() {
    Callable<int*, char, short> c;
    return c.count() != 6;
}
