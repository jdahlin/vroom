template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };

struct error_code {
    int val;
    const void *cat;

    template <class Ep, typename enable_if<sizeof(Ep) == 0, int>::type = 0>
    error_code &operator=(Ep) {
        val = -1;
        return *this;
    }
};

int main() {
    error_code a{1, nullptr};
    error_code b{42, nullptr};
    a = b;
    return a.val == 42 ? 0 : 1;
}
