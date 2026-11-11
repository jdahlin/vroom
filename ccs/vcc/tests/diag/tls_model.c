int f(void) __attribute__((tls_model("global-dynamic")));
int x __attribute__((tls_model("global-dynamic")));
static __thread int good __attribute__((tls_model("local-exec")));
static __thread int a __attribute__((tls_model("local", "dynamic")));
static __thread int b __attribute__((tls_model(123)));
static __thread int c __attribute__((tls_model("foobar")));
static __thread int d __attribute__((tls_model()));
static __thread int e __attribute__((tls_model));
static __thread int extra __attribute__((tls_model(123, 456)));
