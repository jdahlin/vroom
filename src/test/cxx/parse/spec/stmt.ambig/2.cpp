// [stmt.ambig] Example 2 (paragraph 2), docs/cxx-spec/stmt/stmt.ambig.md
// needs: P5
// status: well-formed (as far as the example says)

class T {
  // ...
public:
  T();
  T(int);
  T(int, int);
};
T(a);               //  declaration
T(*b)();            //  declaration
T(c)=7;             //  declaration
T(d),e,f=3;         //  declaration
extern int h;
T(g)(h,2);          //  declaration
