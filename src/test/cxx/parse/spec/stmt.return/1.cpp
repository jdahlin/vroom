// [stmt.return] Example 1 (paragraph 2), docs/cxx-spec/stmt/stmt.return.md
// needs: P7
// status: well-formed (as far as the example says)

std::pair<std::string,int> f(const char* p, int x) {
  return {p,x};
}
