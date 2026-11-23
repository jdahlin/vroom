// if constexpr: [stmt.if]/2, discarded statements of a template
template <class T> struct is_char { static const bool value = false; };
template <> struct is_char<char> { static const bool value = true; };

template <class T> int kind(const T *p) {
    if constexpr (is_char<T>::value)
        return 1;
    else
        return p->nonexistent();
}

template <class T> int both(T v) {
    if constexpr (is_char<T>::value) {
        return v + 10;
    } else if constexpr (sizeof(T) == 4) {
        return v + 20;
    } else {
        return v.missing;
    }
}

int plain() {
    if constexpr (sizeof(int) == 4)
        return 3;
    else
        return 4;
}

int init_statement(int n) {
    if constexpr (true)
        n += 1;
    if (int m = n; m > 2)
        return m;
    return 0;
}

int main() {
    char c = 0;
    int i = 0;
    int r = kind(&c) + both(c) + both(i) + plain() + init_statement(5);
    if (r == 1 + 10 + 20 + 3 + 6)
        return 0;
    return 1;
}
