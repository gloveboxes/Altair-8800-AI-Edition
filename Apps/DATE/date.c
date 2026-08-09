#include <stdio.h>

extern int inp(unsigned port);
extern void outp(unsigned port, unsigned value);

int main(void)
{
    unsigned character;

    outp(31, 0);
    while ((character = (unsigned)inp(200)) != 0)
        putchar((int)character);

    putchar('\r');
    putchar('\n');
    return 0;
}