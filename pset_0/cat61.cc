#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

void transfer(char* fname)
{
    f = fopen(fname)
}
int main(int argc, char** argv[])
{
    if(argc == 1)
    {
        transfer("-");
    }
    for(int i = 1; i < argc; ++i)
    {
        transfer(argv[i]);
    }
}
