/* [abi:mangling-structure]: what is mangled and what is not */
extern "C" void c_function(int) {}
int global_variable = 1;
static int internal_variable = 2;
int use_internal() { return internal_variable; }
namespace ns
{
int namespace_variable = 3;
extern "C" void c_in_namespace() {}
} // namespace ns
void plain(int, char) {}
static void internal_function() {}
void call_internal() { internal_function(); }
// MANGLE: c_function
// MANGLE: global_variable
// MANGLE: _ZL17internal_variable
// MANGLE: _Z12use_internalv
// MANGLE: _ZN2ns18namespace_variableE
// MANGLE: c_in_namespace
// MANGLE: _Z5plainic
// MANGLE: _ZL17internal_functionv
