// std: c++20
// pointers to members as elements of an array or members of an aggregate: null, and const-qualified
// from a non-const member's (Skia's SkSLModuleLoader: static constexpr BuiltinTypePtr kRootTypes[])
struct BT {
	int fA, fB;
};
using P = const int BT::*;
static constexpr P kRoot[] = {&BT::fA, &BT::fB, nullptr};
struct S {
	P m;
	int n;
};
static S s = {&BT::fB, 3};
int main()
{
	BT b{4, 7};
	P local[] = {&BT::fB, nullptr};
	if (b.*kRoot[0] != 4 || b.*kRoot[1] != 7 || kRoot[2] != nullptr)
		return 1;
	if (b.*s.m != 7 || s.n != 3 || b.*local[0] != 7 || local[1] != nullptr)
		return 2;
	return 0;
}
