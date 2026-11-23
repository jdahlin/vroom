# 26.9.1 Header `<numbers>` synopsis [numbers.syn]

[Contents](../../README.md) · up: [26.9 Numbers \[numbers\]](numbers.md) · prev: [26.9 Numbers \[numbers\]](numbers.md) · next: [26.9.2 Mathematical constants \[math.constants\]](math.constants.md)

```cpp
namespace std::numbers {
  template<class T> inline constexpr T e_v          = unspecified;
  template<class T> inline constexpr T log2e_v      = unspecified;
  template<class T> inline constexpr T log10e_v     = unspecified;
  template<class T> inline constexpr T pi_v         = unspecified;
  template<class T> inline constexpr T inv_pi_v     = unspecified;
  template<class T> inline constexpr T inv_sqrtpi_v = unspecified;
  template<class T> inline constexpr T ln2_v        = unspecified;
  template<class T> inline constexpr T ln10_v       = unspecified;
  template<class T> inline constexpr T sqrt2_v      = unspecified;
  template<class T> inline constexpr T sqrt3_v      = unspecified;
  template<class T> inline constexpr T inv_sqrt3_v  = unspecified;
  template<class T> inline constexpr T egamma_v     = unspecified;
  template<class T> inline constexpr T phi_v        = unspecified;

  template<floating_point T> inline constexpr T e_v<T>          = see below;
  template<floating_point T> inline constexpr T log2e_v<T>      = see below;
  template<floating_point T> inline constexpr T log10e_v<T>     = see below;
  template<floating_point T> inline constexpr T pi_v<T>         = see below;
  template<floating_point T> inline constexpr T inv_pi_v<T>     = see below;
  template<floating_point T> inline constexpr T inv_sqrtpi_v<T> = see below;
  template<floating_point T> inline constexpr T ln2_v<T>        = see below;
  template<floating_point T> inline constexpr T ln10_v<T>       = see below;
  template<floating_point T> inline constexpr T sqrt2_v<T>      = see below;
  template<floating_point T> inline constexpr T sqrt3_v<T>      = see below;
  template<floating_point T> inline constexpr T inv_sqrt3_v<T>  = see below;
  template<floating_point T> inline constexpr T egamma_v<T>     = see below;
  template<floating_point T> inline constexpr T phi_v<T>        = see below;

  inline constexpr double e          = e_v<double>;
  inline constexpr double log2e      = log2e_v<double>;
  inline constexpr double log10e     = log10e_v<double>;
  inline constexpr double pi         = pi_v<double>;
  inline constexpr double inv_pi     = inv_pi_v<double>;
  inline constexpr double inv_sqrtpi = inv_sqrtpi_v<double>;
  inline constexpr double ln2        = ln2_v<double>;
  inline constexpr double ln10       = ln10_v<double>;
  inline constexpr double sqrt2      = sqrt2_v<double>;
  inline constexpr double sqrt3      = sqrt3_v<double>;
  inline constexpr double inv_sqrt3  = inv_sqrt3_v<double>;
  inline constexpr double egamma     = egamma_v<double>;
  inline constexpr double phi        = phi_v<double>;
}
```
