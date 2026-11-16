// [class.mem] Example 3 (paragraph 18), docs/cxx-spec/class/class.mem.md
// needs: P5
// status: well-formed (as far as the example says)

struct tnode {
  char tword[20];
  int count;
  tnode* left;
  tnode* right;
};
