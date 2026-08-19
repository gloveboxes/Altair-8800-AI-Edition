/*
 * Emit a stable completion marker and echo arguments for MCP automation.
 * cputs/putchar preserve that simple output without linking printf's formatter.
 */
#include <stdio.h>

static void cputs(text)
const char *text;
{
    while (*text)
        putchar((unsigned char)*text++);
}

int main(argc, argv)
int argc;
char **argv;
{
    int i;

    cputs("MCP-TOOL-COMPLETED");
    for (i = 1; i < argc; i++)
    {
        putchar(' ');
        cputs(argv[i]);
    }
    putchar('\n');
    return 0;
}
