#include <stdio.h>

#include "../COMMON/DXSVC.H"

int main(void)
{
    char date[80];

    if (svc_get_text(SERVICE_GROUP_CLOCK, 4, date, sizeof(date)) < 0)
        return 1;
    printf("%s", date);

    putchar('\r');
    putchar('\n');
    return 0;
}