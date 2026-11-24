/* cxx-run-flags: -include src/test/cxx/run/units/forced-include.h */
/* [cpp.include], clang's -include FILE: read as an #include at the top of
   the main file, so the names the file declares are the parser's: forced::,
   in a class template's alias, before any declaration of this file
   (vcpkg-tool's pch.h).  The file's tokens were once dropped, and the
   alias was a name the parser could not classify. */
template <class T> struct Span {
    using reference = forced::identity_t<T &>;
    constexpr reference operator[](unsigned long i) const noexcept {
        return m[i];
    }
    T *m;
};
int main() {
    int v[3] = {1, 2, 3};
    Span<int> s{v};
    return s[2] - 3;
}
