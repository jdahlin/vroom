/* Linux pthread stress for atomic record snapshots (two writers, two readers).
 * Build with vpp -O1 -S plus target libc include paths, assemble/link with
 * clang -pthread; run natively on x86-64 or qemu-aarch64 -L /usr/aarch64-linux-gnu.
 * Each loaded 8-byte record must be one complete store: hi == ~lo.
 */
#include <pthread.h>
#include <stdint.h>
struct pair { uint32_t lo, hi; };
_Atomic(struct pair) shared = { 0x12345678, 0xedcba987 };
_Atomic(int) failed;
static void *writer(void *arg) {
 uint32_t salt = (uintptr_t)arg;
 for (uint32_t i=0;i<200000;i++) {
  uint32_t n = i ^ salt;
  struct pair x = {n, ~n}; shared = x;
 }
 return 0;
}
static void *reader(void *unused) {
 for (int i=0;i<200000;i++) {
  struct pair x = shared;
  if (x.hi != ~x.lo) failed = 1;
 }
 return 0;
}
int main(void) {
 pthread_t t[4];
 pthread_create(&t[0], 0, writer, (void*)(uintptr_t)0x55555555);
 pthread_create(&t[1], 0, writer, (void*)(uintptr_t)0xaaaaaaaa);
 pthread_create(&t[2], 0, reader, 0); pthread_create(&t[3], 0, reader, 0);
 for(int i=0;i<4;i++) pthread_join(t[i],0);
 return failed;
}
