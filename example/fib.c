#include <stdio.h>
int fib(int n)
{
    if(n <= 1) return n;
    return fib(n-1) + fib(n-2);
}

int main(int argc, char *argv[])
{
    int n;
    scanf("%d",&n);
    printf("%d",fib(n));
    return 0;
}
