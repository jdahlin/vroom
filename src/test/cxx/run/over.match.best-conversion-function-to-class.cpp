/* [over.match.best], [ext:clang] (CWG2327): direct-initialization from a
   class with a conversion function returning the target class calls it,
   though another constructor is reached through another conversion */
struct ID {
    constexpr ID() = default;
    constexpr ID(const ID &) = default;
    constexpr ID(ID &&) = default;
    explicit constexpr ID(const unsigned char &value) : value(value) {}
    unsigned char value{};
};
struct WN {
    int v;
    operator int() const { return v; }
    operator ID() const { return ID(static_cast<unsigned char>(v)); }
};
int f(WN t)
{
    ID table(t);
    return table.value;
}
int main()
{
    WN t{7};
    return f(t) - 7;
}
