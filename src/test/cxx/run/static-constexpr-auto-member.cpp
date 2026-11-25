template <class T>
struct traits {
    static constexpr auto is_int = sizeof(T) == sizeof(int);
    static constexpr auto twice = sizeof(T) * 2;
};

int main() {
    if (!traits<int>::is_int)
        return 1;
    if (traits<char>::is_int)
        return 2;
    if (traits<int>::twice != sizeof(int) * 2)
        return 3;
    return 0;
}
