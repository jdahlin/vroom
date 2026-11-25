/* cxx-unsupported: lowering */
/* A function template specialization whose symbol needs a construct the mangler has no rule
   for (T{} in a decltype return type, clang's tl <type> E): the lowering error names the entity,
   the construct and the part of the symbol, and a note where the entity is declared. */
namespace dd {
struct Result {
  bool ok;
};
template <class L> struct E {
  template <class R> auto op(R &&r) -> decltype(r, Result{}) { return {}; }
};
} // namespace dd
int main() {
  dd::E<int> e;
  e.op(2);
  return 0;
}
