// Member function template inside a nested class of a class template
// ([temp.mem]/1, [temp.mem]/2), as in libc++'s std::optional::_CheckOptionalArgsConstructor.

template <class T>
struct Outer {
    struct Helper {
        template <class U>
        static constexpr bool check() {
            return sizeof(T) == sizeof(U);
        }

        template <class U>
        static U add(U a, U b) {
            return a + b;
        }
    };

    template <class U>
    static constexpr bool same_size() {
        return Helper::template check<U>();
    }
};

static_assert(Outer<int>::same_size<unsigned int>());
static_assert(!Outer<int>::same_size<char>());

int main() {
    return Outer<int>::Helper::add<int>(20, 22) == 42 ? 0 : 1;
}
