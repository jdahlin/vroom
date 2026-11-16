// [class.friend] Example 1 (paragraph 1), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: well-formed (as far as the example says)

class X {
  int a;
  friend void friend_set(X*, int);
public:
  void member_set(int);
};

void friend_set(X* p, int i) { p->a = i; }
void X::member_set(int i) { a = i; }

void f() {
  X obj;
  friend_set(&obj,10);
  obj.member_set(10);
}
