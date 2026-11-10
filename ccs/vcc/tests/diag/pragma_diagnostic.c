/* #pragma clang/GCC diagnostic and _Pragma: push, pop, ignored, warning, error, a group,
   -Weverything; the state is by location (clang's DiagStateMap): a warning given at a
   scope's end follows the pragmas before its location, not those read since */
/* diag-flags: -Wall */
#define DIAG_PUSH _Pragma("clang diagnostic push")
#define DIAG_POP _Pragma("clang diagnostic pop")
#define DIAG_IGNORE(w) _Pragma(#w)
int printf(const char *, ...);

DIAG_PUSH
DIAG_IGNORE(clang diagnostic ignored "-Wformat")
void quiet(long l) { printf("%d", l); }
DIAG_POP
void loud(long l) { printf("%d", l); }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused"
void grouped(void) { int x; }
#pragma GCC diagnostic pop

#pragma clang diagnostic error "-Wunused-variable"
void err(void) { int e; }
#pragma clang diagnostic warning "-Wunused-variable"

void late(void) {
	int before;
#pragma clang diagnostic ignored "-Wunused-variable"
	int after;
}

#pragma clang diagnostic ignored "-Weverything"
void nothing(long l) { printf("%d", l); int z; }
