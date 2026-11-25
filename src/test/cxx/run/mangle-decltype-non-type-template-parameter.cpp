// [abi:mangle.template-param] a non-type template parameter in a dependent decltype of a
// function template's signature: its <template-param> (json's input_adapter(T (&array)[N]) ->
// decltype(input_adapter(array, array + N)), DTcl13input_adapterfp_plfp_T0_EE)
struct ad {
  const char *b, *e;
};
template <class I> ad input_adapter(I b, I e) { return {b, e}; }
template <class T, unsigned long N>
auto input_adapter(T (&array)[N]) -> decltype(input_adapter(array, array + N)) {
  return input_adapter(array, array + N);
}
int main() {
  ad a = input_adapter("abc");
  return a.e - a.b == 4 ? 0 : 1;
}
