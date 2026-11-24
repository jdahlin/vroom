// [ext:gnu_vector_size], clang: a floating constant out of the range of the
// vector's element type cannot be converted by an operator (truncation); one
// in range can.
typedef float float4 __attribute__((vector_size(16)));
constexpr double small = 1.5;
constexpr double huge = 1340282346638528859811704183484516925443.0;
float4 ok(float4 x) { return small * x; }
float4 bad(float4 x) { return huge * x; } // expected-error {{as implicit conversion would cause truncation}}
