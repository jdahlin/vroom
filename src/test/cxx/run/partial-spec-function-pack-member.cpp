// [temp.class.spec.mfunc]/1, [temp.deduct.type]/9: out-of-class member
// function definition of a class template partial specialization matching
// a function type with a parameter pack (R(Args...)).
template <class Sig> struct Function;

template <class R, class... Args>
struct Function<R(Args...)> {
    int arity() const;
};

template <class R, class... Args>
int Function<R(Args...)>::arity() const {
    return sizeof...(Args);
}

int main() {
    Function<int(char, short, long)> f;
    return f.arity() != 3;
}
