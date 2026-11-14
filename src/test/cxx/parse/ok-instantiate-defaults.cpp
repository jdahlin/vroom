// Default template arguments given on an earlier declaration; an explicit specialization found with
// its defaults completed (ser<char> and ser<char, void> are one class).
namespace n { template <typename T = void, typename S = void> struct ser; }
namespace n { template <typename V, typename> struct ser { V v; }; }
namespace n { template <> struct ser<char> { double d; }; }
n::ser<int> a; n::ser<char, void> c2; n::ser<char> c3;
