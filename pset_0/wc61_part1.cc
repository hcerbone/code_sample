#include <cstdio>
long c;
//solution doesn't use placeholder for input
int n;
//unsigned long in solution
int main()
{
    while(true)
    {
        c = fgetc(stdin);
        if(c == EOF)
            break;
        ++n;
    }
    printf("%d\n", n);
    /*
    fprintf(stdout, "%8lu\n", n); is the line from solution
    */
    return 0;
}
