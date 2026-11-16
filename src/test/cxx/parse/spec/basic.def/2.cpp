// [basic.def] Example 2 (paragraph 3), docs/cxx-spec/basic/basic.def.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

#include <string>

struct C {
  std::string s;                // std::string is the standard library class ([string.classes])
};

int main() {
  C a;
  C b = a;
  b = a;
}
