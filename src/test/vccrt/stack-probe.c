/*
 * The test of ___chkstk_ms (bench/vccrt-test.py chkstk), run under Wine
 * linked with libvccrt.a and with compiler-rt's builtins:
 *
 *  1. registers: stack-probe-check.S sets every general register to a
 *     pattern, calls ___chkstk_ms with %rax = SIZE, and stores them all
 *     again; each must be unchanged (rax too: the caller subtracts it next),
 *     and %rsp must be where it was;
 *  2. pages: with %rsp switched to the top of a write-watched region
 *     (VirtualAlloc MEM_WRITE_WATCH), ___chkstk_ms must write every page
 *     that overlaps [top - SIZE, top) and no other (GetWriteWatch).  This
 *     is the check that holds under Wine, which commits a thread's stack
 *     itself and so does not fault when a probe skips its guard page.  It
 *     sees writes only: compiler-rt's probe reads (test), libvccrt's and
 *     libgcc's write (or $0), so it counts as a failure only with the
 *     argument "strict" (the harness passes it for libvccrt);
 *  3. frames: on a thread whose stack is a small reservation (CreateThread
 *     with STACK_SIZE_PARAM_IS_A_RESERVATION, 1 MiB, so only the guard page
 *     mechanism commits it), functions with frames from one page to most of
 *     the reservation, fixed (the compiler calls the probe in the prologue)
 *     and alloca'd (the probe before a dynamic allocation), each touching
 *     the far (lowest) end of its frame first.  Without a probe of every
 *     page, that first touch skips the guard page and faults.
 *
 * Prints one line per check and "ok" at the end; exit status 0 if all pass.
 */
#include <stdio.h>
#include <string.h>

typedef void *HANDLE;
typedef unsigned long DWORD;
typedef DWORD (*thread_start)(void *);
__declspec(dllimport) HANDLE __stdcall CreateThread(void *, unsigned long long, thread_start, void *, DWORD, DWORD *);
__declspec(dllimport) DWORD __stdcall WaitForSingleObject(HANDLE, DWORD);
__declspec(dllimport) int __stdcall GetExitCodeThread(HANDLE, DWORD *);
__declspec(dllimport) void *__stdcall VirtualAlloc(void *, unsigned long long, DWORD, DWORD);
__declspec(dllimport) unsigned __stdcall GetWriteWatch(DWORD, void *, unsigned long long, void **, unsigned long long *, DWORD *);
__declspec(dllimport) unsigned __stdcall ResetWriteWatch(void *, unsigned long long);
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_WRITE_WATCH 0x200000
#define PAGE_READWRITE 0x04
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define INFINITE 0xffffffff

/* stack-probe-check.S: registers[0..15] before and after the call, by
   number (rax rcx rdx rbx rsp rbp rsi rdi r8..r15) */
void stack_probe_check(unsigned long long size, unsigned long long *before, unsigned long long *after);

void stack_probe_on(char *top, unsigned long long size);

static int failures, strict;

static void check_registers(void)
{
	static const unsigned long long sizes[] = { 0, 1, 8, 4095, 4096, 4097, 8191, 8192, 12288, 65536, 100000, 250000 };
	static const char *const names[16] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
					       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
	for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
		unsigned long long before[16], after[16];
		int bad = 0;
		stack_probe_check(sizes[i], before, after);
		for (int r = 0; r < 16; r++)
			if (before[r] != after[r]) {
				printf("registers size %llu: %s %016llx -> %016llx\n", sizes[i], names[r], before[r], after[r]);
				bad = 1;
			}
		if (!bad)
			printf("registers size %llu: preserved\n", sizes[i]);
		failures += bad;
	}
}

#define REGION (2u << 20)
#define PAGE 4096u

static void check_pages(void)
{
	static const unsigned long long sizes[] = { 1, 100, 4095, 4096, 4097, 8000, 8192, 8193, 40000, 65536, 300000, 1000000, 2000000 };
	static void *written[REGION / PAGE];
	char *base = VirtualAlloc(NULL, REGION, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
	if (!base) {
		printf("pages: VirtualAlloc with MEM_WRITE_WATCH failed\n");
		failures++;
		return;
	}
	for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
		char *top = base + REGION - 64;
		unsigned long long count = REGION / PAGE, first, last, expected;
		DWORD granularity;
		int bad = 0;
		ResetWriteWatch(base, REGION);
		stack_probe_on(top, sizes[i]);
		if (GetWriteWatch(0, base, REGION, written, &count, &granularity)) {
			printf("pages: GetWriteWatch failed\n");
			failures++;
			return;
		}
		/* the pages that overlap [top - size, top), the return address's included */
		first = (unsigned long long)(top - sizes[i] - base) / PAGE;
		last = (unsigned long long)(top - 1 - base) / PAGE;
		expected = last - first + 1;
		if (count != expected)
			bad = 1;
		for (unsigned long long k = 0; !bad && k < count; k++)
			if ((char *)written[k] != base + (first + k) * PAGE)
				bad = 1;
		printf("pages size %llu: %llu written of %llu %s\n", sizes[i], count, expected, bad ? "WRONG" : "(each once)");
		failures += bad && strict;
	}
}

/* a frame of SIZE bytes whose lowest byte is written first */
#define FIXED_FRAME(SIZE)                                                \
	__attribute__((noinline)) static int fixed_##SIZE(int seed)      \
	{                                                                \
		volatile char frame[SIZE];                               \
		frame[0] = (char)seed;                                   \
		frame[SIZE - 1] = (char)(seed + 1);                      \
		return frame[0] + frame[SIZE - 1];                       \
	}
FIXED_FRAME(4096)
FIXED_FRAME(4097)
FIXED_FRAME(8192)
FIXED_FRAME(12345)
FIXED_FRAME(65536)
FIXED_FRAME(200000)
FIXED_FRAME(500000)
FIXED_FRAME(900000)

__attribute__((noinline)) static int dynamic_frame(unsigned long long size, int seed)
{
	volatile char *frame = __builtin_alloca(size);
	frame[0] = (char)seed;
	frame[size - 1] = (char)(seed + 1);
	return frame[0] + frame[size - 1];
}

static DWORD frames(void *unused)
{
	static const unsigned long long sizes[] = { 1, 4095, 4096, 4097, 5000, 8191, 8193, 30000, 65537, 131072,
						    300001, 512000, 800000, 950000 };
	int sum = 0;
	(void)unused;
	sum += fixed_4096(1) + fixed_4097(2) + fixed_8192(3) + fixed_12345(4) + fixed_65536(5);
	sum += fixed_200000(6) + fixed_500000(7) + fixed_900000(8);
	for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++)
		sum += dynamic_frame(sizes[i], (int)i);
	return (DWORD)sum;
}

int main(int argc, char **argv)
{
	DWORD result = 0, id;
	HANDLE thread;
	strict = argc > 1 && !strcmp(argv[1], "strict");
	check_registers();
	check_pages();
	thread = CreateThread(NULL, 1 << 20, frames, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, &id);
	if (!thread) {
		printf("CreateThread failed\n");
		return 1;
	}
	WaitForSingleObject(thread, INFINITE);
	GetExitCodeThread(thread, &result);
	printf("frames on a 1 MiB thread: %lu\n", result);
	if (result != (DWORD)frames(NULL))	/* the same again on the main thread */
		failures++;
	printf(failures ? "FAILED\n" : "ok\n");
	return failures != 0;
}
