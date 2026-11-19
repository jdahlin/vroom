// [dcl.enum]/2: an enumerator's attribute-specifier-seq without effect, as
// the macOS SDK's _time.h writes (availability on clockid_t's enumerators)
#define AVAILABLE __attribute__((availability(macos, introduced = 10.12)))
typedef enum {
    Realtime AVAILABLE = 0,
    Monotonic AVAILABLE = 6,
    Next AVAILABLE,
    Old [[deprecated]] = 20,
    Plain [[maybe_unused]]
} Clock;

int main() {
    Clock c = Next;
    return c == 7 && Plain == 21 && Realtime == 0 && Monotonic == 6 ? 0 : 1;
}
