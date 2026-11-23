// [except.handle] Example 1 (paragraph 3), docs/cxx-spec/except/except.handle.md
// needs: P6
// status: well-formed (as far as the example says)

class Matherr { /* ... */ virtual void vf(); };
class Overflow: public Matherr { /* ... */ };
class Underflow: public Matherr { /* ... */ };
class Zerodivide: public Matherr { /* ... */ };

void f() {
  try {
    g();
  } catch (Overflow oo) {
    // ...
  } catch (Matherr mm) {
    // ...
  }
}
