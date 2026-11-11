typedef unsigned long word; struct __attribute__((aligned(64))) block { word x[8]; };
extern word probe(struct block,word,word,word,word,word,word,word);
int main(void) { struct block b={{7,0,0,0,0,0,0,11}}; return probe(b,1,2,3,4,5,6,7)!=53; }
