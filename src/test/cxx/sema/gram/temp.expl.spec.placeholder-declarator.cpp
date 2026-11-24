// [temp.expl.spec], [temp.explicit]: the declarator of an explicit
// specialization has a trailing-return-type, and its decl-specifier-seq a
// placeholder.
template <typename T> auto g(T t) -> T { return t; }
template <> auto g<int>(int t) -> int { return t + 1; }
template auto g<long>(long t) -> long;
