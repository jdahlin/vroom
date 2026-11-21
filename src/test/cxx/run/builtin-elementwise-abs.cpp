// [ext:clang]: __builtin_elementwise_abs of a scalar and of a vector.
typedef int v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((vector_size(16)));

int main() {
    v4 a = {-1, 2, -3, 4};
    v4 b = __builtin_elementwise_abs(a);
    f4 f = {-1.5f, 2.0f, -0.5f, 0.0f};
    f4 g = __builtin_elementwise_abs(f);
    int s = __builtin_elementwise_abs(-5);
    return b[0] != 1 || b[1] != 2 || b[2] != 3 || b[3] != 4 || g[0] != 1.5f ||
           g[2] != 0.5f || s != 5;
}
