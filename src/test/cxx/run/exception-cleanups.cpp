// destructors run when an exception unwinds their scope (landing pads, .gcc_except_table), innermost first,
// through functions with no handler; not for an object whose constructor threw; a destructor that throws
// while unwinding terminates, after the cleanups below it ran (llvm-test-suite UnitTests/EH/cleanup-N)
namespace std { typedef void (*terminate_handler)(); terminate_handler set_terminate(terminate_handler) noexcept; }
#include <stdlib.h>
static int order[16], n;
struct T { int k; T(int x) : k(x) { if (x == 9) throw 9; } ~T() { order[n++] = k; } };
void thr() { T a(1); T b(2); throw 'c'; }
void mid() { T c(3); thr(); T d(4); }
void ctor() { T e(5); T f(9); T g(6); }
struct Bad { ~Bad() { throw 1; } };
static void done() { exit(n == 6 && order[5] == 8 ? 0 : 3); }
int main() {
	try { mid(); } catch (char) { order[n++] = 0; }
	try { ctor(); } catch (int) { order[n++] = 7; }
	if (!(n == 6 && order[0] == 2 && order[1] == 1 && order[2] == 3 && order[3] == 0 && order[4] == 5 && order[5] == 7))
		return 1;
	n = 5;
	std::set_terminate(done);
	try { Bad x; T h(8); throw 'x'; } catch (char) { }
	return 2;
}
