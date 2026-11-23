// [temp.arg.nontype]/2, [temp.dep.constexpr]/2: an argument for a non-type
// template-parameter whose type depends on an earlier one (as libc++'s
// __bit_iterator<_Cp, _IsConst, typename _Cp::__storage_type = 0>), written
// in a template where that type is still dependent (declared only: calling
// it needs a mangled default argument, not done yet).
struct storage {
    typedef unsigned type;
};

template <class C, bool B, typename C::type V = 0>
struct box {
    int get() const { return B ? 7 : 3; }
};

template <class C, bool B>
int read(box<C, B> b);

int main() {
    box<storage, true> b;
    return b.get() == 7 ? 0 : 1;
}
