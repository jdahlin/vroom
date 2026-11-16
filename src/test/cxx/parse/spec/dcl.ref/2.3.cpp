// [dcl.ref] Example 2, part 3 (paragraph 3), docs/cxx-spec/dcl/dcl.ref.md
// needs: P5
// status: well-formed (as far as the example says)

struct link {
  link* next;
};

link* first;

void h(link*& p) {  // p is a reference to pointer
  p->next = first;
  first = p;
  p = 0;
}

void k() {
   link* q = new link;
   h(q);
}
