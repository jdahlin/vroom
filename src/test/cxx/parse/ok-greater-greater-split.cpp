template <class T> using __remove_cv_t = __remove_cv(T);
template <class> inline const bool __is_floating_point_impl = false;
template <> inline const bool __is_floating_point_impl<float> = true;
template <class _Tp> inline constexpr bool is_floating_point_v = __is_floating_point_impl<__remove_cv_t<_Tp>>;
