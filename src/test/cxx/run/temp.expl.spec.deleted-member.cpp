// [temp.expl.spec]: deleted explicit specialization of a member function
// (including a conversion function) of a class template specialization
// (e.g. `template <> constexpr std::optional<FileHandle>::operator bool() const noexcept = delete;`).

template <class T> struct Box {
    T v;
    constexpr explicit operator bool() const noexcept { return v != 0; }
    int get() const { return 1; }
};

struct Handle {
    int fd;
};

template <> constexpr Box<Handle>::operator bool() const noexcept = delete;
template <> int Box<Handle>::get() const = delete;

int main()
{
    Box<int> a{1};
    if (!a || a.get() != 1)
        return 1;
    Box<Handle> h{{42}};
    if (h.v.fd != 42)
        return 2;
    return 0;
}
