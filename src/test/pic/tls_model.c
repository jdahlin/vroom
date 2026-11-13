__thread int a; extern __thread int b;
__attribute__((tls_model("initial-exec"))) __thread int c;
__attribute__((tls_model("local-exec"))) extern __thread int d;
__attribute__((tls_model("global-dynamic"))) __thread int e;
int f(void){return a+b+c+d+e;}
