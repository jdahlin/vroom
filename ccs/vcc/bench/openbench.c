/* openbench LIST NTHR: per-file kernel cost of open/read/fstat/close and stat, N threads */
#include <fcntl.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static double now(clockid_t c){struct timespec t;clock_gettime(c,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static char **p; static int n, mode; static _Atomic int next; static double cpu[64];
static const char *names[]={"open (no close)","open+close","open+read+close","open+fstat+read+close","stat"};
static void *w(void *a){ pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
 int id=(int)(long)a; char *buf=malloc(8<<20); double c0=now(CLOCK_THREAD_CPUTIME_ID); int i;
 while((i=atomic_fetch_add(&next,1))<n){
  if(mode==4){struct stat st;stat(p[i],&st);continue;}
  int fd=open(p[i],O_RDONLY|O_CLOEXEC); if(fd<0){perror("open");exit(1);}
  if(mode==3){struct stat st;fstat(fd,&st);}
  if(mode>=2) read(fd,buf,8<<20);
  if(mode>=1) close(fd);}
 cpu[id]=now(CLOCK_THREAD_CPUTIME_ID)-c0; return 0;}
int main(int c,char**v){FILE*f=fopen(v[1],"r");char l[4096];p=malloc(8*8192);
 while(fgets(l,sizeof l,f)){l[strcspn(l,"\n")]=0;p[n++]=strdup(l);}
 struct rlimit rl={20000,20000}; setrlimit(RLIMIT_NOFILE,&rl);
 int T=atoi(v[2]);
 for(mode=0;mode<5;mode++){ pthread_t th[64]; next=0; double t=now(CLOCK_MONOTONIC);
  for(int i=0;i<T;i++) pthread_create(&th[i],0,w,(void*)(long)i);
  for(int i=0;i<T;i++) pthread_join(th[i],0);
  t=now(CLOCK_MONOTONIC)-t; double cs=0; for(int i=0;i<T;i++) cs+=cpu[i];
  printf("T=%d %-22s wall %5.1f ms  %6.2f us cpu/file\n",T,names[mode],t*1e3,cs*1e6/n);
  if(mode==0) for(int fd=3;fd<n+16;fd++) close(fd);}}
