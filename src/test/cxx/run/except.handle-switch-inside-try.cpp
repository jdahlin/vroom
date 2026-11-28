/* [except.handle]/2, [stmt.dcl]/3: case and default labels of a switch that
   is itself inside the try block or the handler are no jumps into it */
/* cxx-run-flags: -fexceptions */
/* nothing throws, and the test links no C++ runtime */
extern "C" {
void __gxx_personality_v0() {}
void *__cxa_begin_catch(void *) noexcept { return nullptr; }
void __cxa_end_catch() {}
void _Unwind_Resume() {}
}
static int run(int k)
{
    int n = 0;
    try {
        switch (k) {
        case 0:
            n = 10;
            break;
        case 1:
            n = 30;
            break;
        default:
            n = 20;
        }
    } catch (...) {
        int e = 1;
        switch (e) {
        case 1:
            n = 30;
            break;
        }
    }
    return n;
}
int main()
{
    return run(0) != 10 || run(1) != 30 || run(2) != 20;
}
