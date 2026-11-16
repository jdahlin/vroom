# 17.3.2 Header `<version>` synopsis [version.syn]

[Contents](../README.md) · up: [17.3 Implementation properties \[support.limits\]](support.limits.md) · prev: [17.3.1 General \[support.limits.general\]](support.limits.general.md) · next: [17.3.3 Header `<limits>` synopsis \[limits.syn\]](limits.syn.md)

**1** The header `<version>` supplies implementation-dependent information about the C++ standard library (e.g., version number and release date).

**2** Each of the macros defined in `<version>` is also defined after inclusion of any member of the set of library headers indicated in the corresponding comment in this synopsis. [*Note 1*: Future versions of this International Standard might replace the values of these macros with greater values. — *end note*]

```cpp
#define __cpp_lib_addressof_constexpr               201603L // also in <memory>
#define __cpp_lib_allocator_traits_is_always_equal  201411L
  // also in <memory>, <scoped_allocator>, <string>, <deque>, <forward_list>, <list>, <vector>,
  // <map>, <set>, <unordered_map>, <unordered_set>
#define __cpp_lib_any                               201606L // also in <any>
#define __cpp_lib_apply                             201603L // also in <tuple>
#define __cpp_lib_array_constexpr                   201811L // also in <iterator>, <array>
#define __cpp_lib_as_const                          201510L // also in <utility>
#define __cpp_lib_assume_aligned                    201811L // also in <memory>
#define __cpp_lib_atomic_flag_test                  201907L // also in <atomic>
#define __cpp_lib_atomic_float                      201711L // also in <atomic>
#define __cpp_lib_atomic_is_always_lock_free        201603L // also in <atomic>
#define __cpp_lib_atomic_lock_free_type_aliases     201907L // also in <atomic>
#define __cpp_lib_atomic_ref                        201806L // also in <atomic>
#define __cpp_lib_atomic_shared_ptr                 201711L // also in <memory>
#define __cpp_lib_atomic_value_initialization       201911L // also in <atomic>, <memory>
#define __cpp_lib_atomic_wait                       201907L // also in <atomic>
#define __cpp_lib_barrier                           201907L // also in <barrier>
#define __cpp_lib_bind_front                        201907L // also in <functional>
#define __cpp_lib_bit_cast                          201806L // also in <bit>
#define __cpp_lib_bitops                            201907L // also in <bit>
#define __cpp_lib_bool_constant                     201505L // also in <type_traits>
#define __cpp_lib_bounded_array_traits              201902L // also in <type_traits>
#define __cpp_lib_boyer_moore_searcher              201603L // also in <functional>
#define __cpp_lib_byte                              201603L // also in <cstddef>
#define __cpp_lib_char8_t                           201907L
  // also in <atomic>, <filesystem>, <istream>, <limits>, <locale>, <ostream>, <string>, <string_view>
#define __cpp_lib_chrono                            201907L // also in <chrono>
#define __cpp_lib_chrono_udls                       201304L // also in <chrono>
#define __cpp_lib_clamp                             201603L // also in <algorithm>
#define __cpp_lib_complex_udls                      201309L // also in <complex>
#define __cpp_lib_concepts                          202002L // also in <concepts>
#define __cpp_lib_constexpr_algorithms              201806L // also in <algorithm>
#define __cpp_lib_constexpr_complex                 201711L // also in <complex>
#define __cpp_lib_constexpr_dynamic_alloc           201907L // also in <memory>
#define __cpp_lib_constexpr_functional              201907L // also in <functional>
#define __cpp_lib_constexpr_iterator                201811L // also in <iterator>
#define __cpp_lib_constexpr_memory                  201811L // also in <memory>
#define __cpp_lib_constexpr_numeric                 201911L // also in <numeric>
#define __cpp_lib_constexpr_string                  201907L // also in <string>
#define __cpp_lib_constexpr_string_view             201811L // also in <string_view>
#define __cpp_lib_constexpr_tuple                   201811L // also in <tuple>
#define __cpp_lib_constexpr_utility                 201811L // also in <utility>
#define __cpp_lib_constexpr_vector                  201907L // also in <vector>
#define __cpp_lib_coroutine                         201902L // also in <coroutine>
#define __cpp_lib_destroying_delete                 201806L // also in <new>
#define __cpp_lib_enable_shared_from_this           201603L // also in <memory>
#define __cpp_lib_endian                            201907L // also in <bit>
#define __cpp_lib_erase_if                          202002L
  // also in <string>, <deque>, <forward_list>, <list>, <vector>, <map>, <set>, <unordered_map>,
  // <unordered_set>
#define __cpp_lib_exchange_function                 201304L // also in <utility>
#define __cpp_lib_execution                         201902L // also in <execution>
#define __cpp_lib_filesystem                        201703L // also in <filesystem>
#define __cpp_lib_format                            201907L // also in <format>
#define __cpp_lib_gcd_lcm                           201606L // also in <numeric>
#define __cpp_lib_generic_associative_lookup        201304L // also in <map>, <set>
#define __cpp_lib_generic_unordered_lookup          201811L
  // also in <unordered_map>, <unordered_set>
#define __cpp_lib_hardware_interference_size        201703L // also in <new>
#define __cpp_lib_has_unique_object_representations 201606L // also in <type_traits>
#define __cpp_lib_hypot                             201603L // also in <cmath>
#define __cpp_lib_incomplete_container_elements     201505L
  // also in <forward_list>, <list>, <vector>
#define __cpp_lib_int_pow2                          202002L // also in <bit>
#define __cpp_lib_integer_comparison_functions      202002L // also in <utility>
#define __cpp_lib_integer_sequence                  201304L // also in <utility>
#define __cpp_lib_integral_constant_callable        201304L // also in <type_traits>
#define __cpp_lib_interpolate                       201902L // also in <cmath>, <numeric>
#define __cpp_lib_invoke                            201411L // also in <functional>
#define __cpp_lib_is_aggregate                      201703L // also in <type_traits>
#define __cpp_lib_is_constant_evaluated             201811L // also in <type_traits>
#define __cpp_lib_is_final                          201402L // also in <type_traits>
#define __cpp_lib_is_invocable                      201703L // also in <type_traits>
#define __cpp_lib_is_layout_compatible              201907L // also in <type_traits>
#define __cpp_lib_is_nothrow_convertible            201806L // also in <type_traits>
#define __cpp_lib_is_null_pointer                   201309L // also in <type_traits>
#define __cpp_lib_is_pointer_interconvertible       201907L // also in <type_traits>
#define __cpp_lib_is_swappable                      201603L // also in <type_traits>
#define __cpp_lib_jthread                           201911L // also in <stop_token>, <thread>
#define __cpp_lib_latch                             201907L // also in <latch>
#define __cpp_lib_launder                           201606L // also in <new>
#define __cpp_lib_list_remove_return_type           201806L // also in <forward_list>, <list>
#define __cpp_lib_logical_traits                    201510L // also in <type_traits>
#define __cpp_lib_make_from_tuple                   201606L // also in <tuple>
#define __cpp_lib_make_reverse_iterator             201402L // also in <iterator>
#define __cpp_lib_make_unique                       201304L // also in <memory>
#define __cpp_lib_map_try_emplace                   201411L // also in <map>
#define __cpp_lib_math_constants                    201907L // also in <numbers>
#define __cpp_lib_math_special_functions            201603L // also in <cmath>
#define __cpp_lib_memory_resource                   201603L // also in <memory_resource>
#define __cpp_lib_node_extract                      201606L
  // also in <map>, <set>, <unordered_map>, <unordered_set>
#define __cpp_lib_nonmember_container_access        201411L
  // also in <array>, <deque>, <forward_list>, <iterator>, <list>, <map>, <regex>, <set>, <string>,
  // <unordered_map>, <unordered_set>, <vector>
#define __cpp_lib_not_fn                            201603L // also in <functional>
#define __cpp_lib_null_iterators                    201304L // also in <iterator>
#define __cpp_lib_optional                          201606L // also in <optional>
#define __cpp_lib_parallel_algorithm                201603L // also in <algorithm>, <numeric>
#define __cpp_lib_polymorphic_allocator             201902L // also in <memory>
#define __cpp_lib_quoted_string_io                  201304L // also in <iomanip>
#define __cpp_lib_ranges                            201911L
  // also in <algorithm>, <functional>, <iterator>, <memory>, <ranges>
#define __cpp_lib_raw_memory_algorithms             201606L // also in <memory>
#define __cpp_lib_remove_cvref                      201711L // also in <type_traits>
#define __cpp_lib_result_of_sfinae                  201210L // also in <functional>, <type_traits>
#define __cpp_lib_robust_nonmodifying_seq_ops       201304L // also in <algorithm>
#define __cpp_lib_sample                            201603L // also in <algorithm>
#define __cpp_lib_scoped_lock                       201703L // also in <mutex>
#define __cpp_lib_semaphore                         201907L // also in <semaphore>
#define __cpp_lib_shared_mutex                      201505L // also in <shared_mutex>
#define __cpp_lib_shared_ptr_arrays                 201707L // also in <memory>
#define __cpp_lib_shared_ptr_weak_type              201606L // also in <memory>
#define __cpp_lib_shared_timed_mutex                201402L // also in <shared_mutex>
#define __cpp_lib_shift                             201806L // also in <algorithm>
#define __cpp_lib_smart_ptr_for_overwrite           202002L // also in <memory>
#define __cpp_lib_source_location                   201907L // also in <source_location>
#define __cpp_lib_span                              202002L // also in <span>
#define __cpp_lib_ssize                             201902L // also in <iterator>
#define __cpp_lib_starts_ends_with                  201711L // also in <string>, <string_view>
#define __cpp_lib_string_udls                       201304L // also in <string>
#define __cpp_lib_string_view                       201803L // also in <string>, <string_view>
#define __cpp_lib_syncbuf                           201803L // also in <syncstream>
#define __cpp_lib_three_way_comparison              201907L // also in <compare>
#define __cpp_lib_to_address                        201711L // also in <memory>
#define __cpp_lib_to_array                          201907L // also in <array>
#define __cpp_lib_to_chars                          201611L // also in <charconv>
#define __cpp_lib_transformation_trait_aliases      201304L // also in <type_traits>
#define __cpp_lib_transparent_operators             201510L // also in <memory>, <functional>
#define __cpp_lib_tuple_element_t                   201402L // also in <tuple>
#define __cpp_lib_tuples_by_type                    201304L // also in <utility>, <tuple>
#define __cpp_lib_type_identity                     201806L // also in <type_traits>
#define __cpp_lib_type_trait_variable_templates     201510L // also in <type_traits>
#define __cpp_lib_uncaught_exceptions               201411L // also in <exception>
#define __cpp_lib_unordered_map_try_emplace         201411L // also in <unordered_map>
#define __cpp_lib_unwrap_ref                        201811L // also in <type_traits>
#define __cpp_lib_variant                           201606L // also in <variant>
#define __cpp_lib_void_t                            201411L // also in <type_traits>
```
