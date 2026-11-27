// [class.pre]/2, [class.friend]: friend class B; of a base B of another
// namespace is that base.
struct GrOp {
    template <typename Op> static Op *Make() { return new Op(1); }
};
namespace s {
class ClearOp : public GrOp {
public:
    static ClearOp *m() { return GrOp::Make<ClearOp>(); }
private:
    friend class GrOp;
    ClearOp(int) {}
};
}
int main() { return !s::ClearOp::m(); }
