template <bool> struct _OrImpl;
template <> struct _OrImpl<true> {
  template <class _Res, class _First, class... _Rest>
  using _Result [[__gnu__::__nodebug__]] = typename _OrImpl<!bool(_First::value) && sizeof...(_Rest) != 0>::template _Result<_First, _Rest...>;
};
