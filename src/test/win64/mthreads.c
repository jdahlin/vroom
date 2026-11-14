/* vcc-flags: -mthreads */
/* -mthreads (PLAN-win64.md WIN6): libmingwthrd before libmingw32, as clang's MinGW driver; a
   thread-key destructor registered with __mingwthr_key_dtor runs when the thread exits. */
#include <stdio.h>
#include "winapi.h"

extern int __mingwthr_key_dtor(u32 key, void (*dtor)(void *));
__declspec(dllimport) u32 TlsAlloc(void);
__declspec(dllimport) int TlsSetValue(u32 key, void *value);

static volatile int ran;
static u32 key;

static void dtor(void *p) { ran += *(int *)p; }

static u32 worker(void *arg)
{
	TlsSetValue(key, arg);
	return 0;
}

int main(void)
{
	static int seven = 7;
	u32 id;

	key = TlsAlloc();
	printf("registered %d\n", __mingwthr_key_dtor(key, dtor));
	void *h = CreateThread(0, 0, worker, &seven, 0, &id);
	WaitForSingleObject(h, 0xffffffff);
	CloseHandle(h);
	printf("destructor ran: %d\n", ran);
	return 0;
}
