/* A call of a builtin that only computes its value (the classifications,
 * the comparisons, the bit counts, __builtin_object_size of a literal) has
 * the effects of its arguments, which are none here: clang folds it into a
 * constant file-scope array bound (a bound that is not an integer constant
 * expression, folded, is its GNU extension); a bound with a call of a
 * function in it stays a variable length array, which is an error here. */
char isnan_bound[1 + 0 * (long)__builtin_isnan((double)1.5)];
char compare_bound[__builtin_isgreater(2.0, 1.0) + 1];
char signbit_bound[__builtin_signbit(-1.0) + 1];
char clz_bound[__builtin_clz(8u)];
char size_bound[__builtin_object_size("abcde", 1)];
char classify_bound[__builtin_classify_type(1.5) + 1];

int main(void)
{
    if (sizeof isnan_bound != 1 || sizeof compare_bound != 2 ||
        sizeof signbit_bound != 2 || sizeof clz_bound != 28 ||
        sizeof size_bound != 6 || sizeof classify_bound != 9)
        return 1;
    return 0;
}
