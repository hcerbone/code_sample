#include <cstdio>
#include <cctype>
unsigned long l;
unsigned long w;
unsigned long b;
bool inspace = true;
bool lastSpace = false;
int main()
{
    long c;
    while(true)
    {
        c = fgetc(stdin);
        if(c == EOF)
            break;
        if(c == '\n')
            ++l;
        bool currSpace = isspace(c);
        //solution casts to char
        if(inspace && !currSpace)
            ++w;
        inspace = currSpace;
        ++b;
    }
    fprintf(stdout, "%8lu\t%8lu\t%8lu\n", l, w, b);
    return 0;
}
