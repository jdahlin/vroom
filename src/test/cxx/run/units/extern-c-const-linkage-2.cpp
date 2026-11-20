extern "C" const int x;
extern "C" const int c;
extern "C" const int arr[3];
extern "C" __attribute__((visibility("hidden"))) const unsigned char tab[4];
extern "C" const int in_ns;
extern "C" int w;

int other()
{
    return x + c + arr[2] + tab[3] + in_ns + w;
}
