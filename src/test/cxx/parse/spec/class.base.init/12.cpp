// [class.base.init] Example 12 (paragraph 18), docs/cxx-spec/class/class.base.init.md
// needs: P7
// status: well-formed (as far as the example says)

template<class... Mixins>
class X : public Mixins... {
public:
  X(const Mixins&... mixins) : Mixins(mixins)... { }
};
