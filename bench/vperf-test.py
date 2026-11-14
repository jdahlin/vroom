#!/usr/bin/env python3
"""Linux performance-counter descriptor lifecycle, with mocked perf syscalls."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if not sys.platform.startswith('linux'):
    raise SystemExit('vperf-test: Linux only')
root = Path(__file__).resolve().parent.parent
source = r'''
#define _GNU_SOURCE
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <linux/perf_event.h>
static atomic_int opened, closed;
static _Thread_local int calls;
static int fail_member;
static long fake_syscall(long nr, ...) {
 va_list ap; va_start(ap,nr);
 (void)va_arg(ap,void*); (void)va_arg(ap,int); (void)va_arg(ap,int); (void)va_arg(ap,int);
 assert(va_arg(ap,int)==PERF_FLAG_FD_CLOEXEC); va_end(ap);
 if (++calls==2 && fail_member) return -1;
 return atomic_fetch_add(&opened,1)+100;
}
static int fake_close(int fd) { assert(fd>=100); atomic_fetch_add(&closed,1); return 0; }
static ssize_t fake_read(int fd,void *p,size_t n) { unsigned long long *v=p; v[0]=2;v[1]=10;v[2]=20;return n; }
#define syscall fake_syscall
#define close fake_close
#define read fake_read
#include "port/performance-counters.h"
static void *worker(void *p) { vperf v=vperformance_now(); assert(v.ins==(fail_member?0:10)); vperformance_now();return 0; }
int main(void) { for (fail_member=0;fail_member<2;fail_member++) for(int i=0;i<20;i++){pthread_t t;assert(!pthread_create(&t,0,worker,0));pthread_join(t,0);assert(opened==closed);} }
'''
with tempfile.TemporaryDirectory(prefix='vperf-test-') as tmp:
    src = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    src.write_text(source)
    subprocess.run([os.environ.get('CC', 'clang'), '-pthread', '-I' + str(root / 'src/include'),
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('vperf-test: thread cleanup, partial-open cleanup and CLOEXEC pass')
