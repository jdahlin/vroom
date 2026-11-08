#define cat(a,b) a##b
cat(.,.) cat(/,/) cat(cat,(1,2)) cat(1,2,3)
#define VA(fmt, ...) printf(fmt, ## __VA_ARGS__)
VA()
