# 20.18.2 Header `<execution>` synopsis [execution.syn]

[Contents](../README.md) · up: [20.18 Execution policies \[execpol\]](execpol.md) · prev: [20.18.1 In general \[execpol.general\]](execpol.general.md) · next: [20.18.3 Execution policy type trait \[execpol.type\]](execpol.type.md)

```cpp
namespace std {
  // [execpol.type], execution policy type trait
  template<class T> struct is_execution_policy;
  template<class T> inline constexpr bool is_execution_policy_v = is_execution_policy<T>::value;
}

namespace std::execution {
  // [execpol.seq], sequenced execution policy
  class sequenced_policy;

  // [execpol.par], parallel execution policy
  class parallel_policy;

  // [execpol.parunseq], parallel and unsequenced execution policy
  class parallel_unsequenced_policy;

  // [execpol.unseq], unsequenced execution policy
  class unsequenced_policy;

  // [execpol.objects], execution policy objects
  inline constexpr sequenced_policy            seq{ unspecified };
  inline constexpr parallel_policy             par{ unspecified };
  inline constexpr parallel_unsequenced_policy par_unseq{ unspecified };
  inline constexpr unsequenced_policy          unseq{ unspecified };
}
```
