/* The few Windows x64 declarations the src/test/win64 tests use (the test runtime has no
   windows.h): kernel32 imports, the x64 CONTEXT by offset, RUNTIME_FUNCTION. */
#ifndef WIN64_TEST_WINAPI_H
#define WIN64_TEST_WINAPI_H
typedef unsigned long long u64;
typedef unsigned int u32;
typedef struct { u32 begin, end, unwind; } runtime_function;
/* CONTEXT: 1232 bytes, 16-aligned; the registers this test reads and writes by offset */
typedef struct __attribute__((aligned(16))) { unsigned char b[1232]; } context;
enum { CTX_FLAGS = 0x30, CTX_RAX = 0x78, CTX_RBX = 0x90, CTX_RSP = 0x98, CTX_RBP = 0xa0, CTX_RSI = 0xa8,
       CTX_RDI = 0xb0, CTX_R12 = 0xd8, CTX_R13 = 0xe0, CTX_R14 = 0xe8, CTX_R15 = 0xf0, CTX_RIP = 0xf8,
       CTX_XMM0 = 0x1a0 };
#define CTX(c, off) (*(u64 *)((c)->b + (off)))
typedef struct { u32 code, flags; void *record; void *address; u32 nparams; u64 info[15]; } exception_record;
typedef struct { exception_record *rec; context *ctx; } exception_pointers;

__declspec(dllimport) void RtlCaptureContext(context *);
__declspec(dllimport) runtime_function *RtlLookupFunctionEntry(u64 pc, u64 *base, void *history);
__declspec(dllimport) void *RtlVirtualUnwind(u32 type, u64 base, u64 pc, runtime_function *fn, context *ctx,
					   void **data, u64 *frame, void *pointers);
__declspec(dllimport) unsigned short RtlCaptureStackBackTrace(u32 skip, u32 n, void **frames, u32 *hash);
__declspec(dllimport) void *AddVectoredExceptionHandler(u32 first, long (*handler)(exception_pointers *));
__declspec(dllimport) u32 RemoveVectoredExceptionHandler(void *);
__declspec(dllimport) u32 GetCurrentThreadId(void);
__declspec(dllimport) void *CreateThread(void *attrs, u64 stack, u32 (*start)(void *), void *arg, u32 flags, u32 *id);
__declspec(dllimport) u32 WaitForSingleObject(void *h, u32 ms);
__declspec(dllimport) int CloseHandle(void *h);

/* the function a code address is in: its RUNTIME_FUNCTION's start, 0 for a leaf without one */
static inline u64 fn_start(u64 pc)
{
	u64 base;
	runtime_function *f = RtlLookupFunctionEntry(pc, &base, 0);

	return f ? base + f->begin : 0;
}
#endif
