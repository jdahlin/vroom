template <class _T1, class _T2> struct pair {
  template <class _U1, class _U2>
  explicit(true) pair(pair<_U1, _U2> const& __p) noexcept(true) : first(__p.first) {}
  _T1 first;
};
