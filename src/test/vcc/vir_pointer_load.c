/* Pointer-only loads can use one typed value. Integer observations, stores,
 * calls changing the pointer's home, and volatile reads must retain meaning. */
#include <stdio.h>
#define NI __attribute__((noinline))
typedef struct node { struct node *next; long value; } node;
static volatile unsigned seen;
NI void advance(node **root) { *root=(*root)->next; seen++; }
NI long chain(node *const *root, unsigned n)
{
 node *p=*root; long sum=0;
 for(unsigned i=0;i<n;i++) {sum+=p->value; p=p->next;}
 return sum;
}
NI unsigned long bits(node *const *root) {return (unsigned long)*root;}
NI long mixed(const unsigned long *raw, int use_pointer)
{
 unsigned long v=*raw;
 return use_pointer?((node *)v)->value:(long)(v&255);
}
NI long alias(node **root)
{
 node *p=*root; *root=p->next;
 return p->value*3+(*root)->value;
}
NI long across(node **root, unsigned n)
{
 node *p=*root; long sum=0;
 for(unsigned i=0;i<n;i++) {
  sum+=p->value; advance(root);
  if(i&1)p=*root;
  sum+=p->value;
 }
 return sum;
}
NI long volatile_root(node *volatile *root) {return (*root)->value+(*root)->value;}
int main(void)
{
 node a[5];long sum=0;
 for(unsigned i=0;i<5;i++){a[i].next=&a[(i+1)%5];a[i].value=(long)i*19-23;}
 for(unsigned i=0;i<30;i++) {
  node *root=&a[i%5];node *volatile vol=root;
  unsigned long raw=(unsigned long)root;
  if(bits(&root)!=raw || mixed(&raw,0)!=(long)(raw&255) || mixed(&raw,1)!=root->value)return 1;
  sum+=chain(&root,i)+volatile_root(&vol);
  sum+=alias(&root)+across(&root,i);
 }
 printf("%ld %u\n",sum,seen);
 return 0;
}
