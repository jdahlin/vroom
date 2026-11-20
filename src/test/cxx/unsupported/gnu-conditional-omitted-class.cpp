// [ext:gnu_conditional_omitted]: a first operand of class type (converted to bool by a function) is not analysed
struct flag {
    explicit operator bool() const;
};
flag pick(flag f, flag g) {
    return f ?: g;
}
