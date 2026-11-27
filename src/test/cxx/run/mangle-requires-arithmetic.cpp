// [abi:mangle.template-arg]: a trailing requires-clause with arithmetic, and
// a type-constraint naming a namespace's concept.
template <typename T, unsigned long Alignment = alignof(T)>
int pod(int n)
    requires(sizeof(T) % Alignment == 0)
{
    return n + int(Alignment);
}
namespace C
{
template <class T> concept G = sizeof(T) > 1;
}
template <C::G G> int acc() { return int(sizeof(G)); }
int main() { return pod<int>(1) + acc<int>() - 9; }
