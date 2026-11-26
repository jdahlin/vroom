/* [dcl.link]/1 [impl.clang]: a function of C language linkage in an unnamed
   namespace keeps external linkage, so it is emitted though unused. */
namespace {
extern "C" void unused_c(void) {}
extern "C" int used_c(void) { return 3; }
}
extern "C" {
namespace {
void braced_c() {}
int braced_value = 4;
}
}
int main() { return used_c() + braced_value - 7; }
