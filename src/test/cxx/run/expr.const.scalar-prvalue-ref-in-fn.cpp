// [expr.const], [dcl.init.ref]/5.4.2: binding a const reference to a scalar
// prvalue inside a constexpr function (like OpenTTD's PoolID::Invalid()).
template <typename T, T V>
struct ID {
    T val;
    constexpr ID() = default;
    explicit constexpr ID(const T &v) : val(v) {}
    static constexpr ID Make() { return ID{static_cast<T>(V)}; }
    constexpr T base() const { return val; }
};

using MyID = ID<unsigned char, 0xFF>;
static constexpr MyID X = MyID::Make();
static constexpr unsigned char Y = MyID::Make().base();

int main()
{
    return (X.val == 0xFF && Y == 0xFF) ? 0 : 1;
}
