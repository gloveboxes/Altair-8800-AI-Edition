#include <stdio.h>

#include "../COMMON/DXSVC.H"

int main(void)
{
    char date[80];
    const char *cursor;

    if (svc_get_text(SERVICE_GROUP_CLOCK, 4, date, sizeof(date)) < 0)
        return 1;
    for (cursor = date; *cursor; ++cursor)
        putchar(*cursor);

    putchar('\r');
    putchar('\n');
    return 0;
}