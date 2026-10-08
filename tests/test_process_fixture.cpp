#include <windows.h>
#include <cstdio>
int main(){std::puts("fixture stdout");std::fprintf(stderr,"fixture stderr\n");std::fflush(stdout);std::fflush(stderr);Sleep(60000);return 0;}
