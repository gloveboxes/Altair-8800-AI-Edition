#include <stdio.h>
#include "../../COMMON/DXSVC.H"

extern int getch(void);
extern int kbhit(void);

#define LED_OPERATION 4
#define BLINK_INTERVAL_MS 250
#define ESCAPE_KEY 27
#define CTRL_C_KEY 3

static int set_led(int state)
{
    return svc_operation_send(SERVICE_GROUP_UTILITY, LED_OPERATION,
                             &state, 1, NULL, 0) >= 0;
}

static int quit_requested(void)
{
    while (kbhit())
    {
        int key = getch();
        if (key == ESCAPE_KEY || key == CTRL_C_KEY)
            return 1;
    }
    return 0;
}

int main(void)
{
    int state = 0;

    printf("BLINK: press Escape or Ctrl+C to quit.\n");
    for (;;)
    {
        state = !state;
        if (!set_led(state))
        {
            set_led(0);
            return 1;
        }

        svc_start_timer(0, BLINK_INTERVAL_MS);
        while (svc_timer_active(0))
        {
            if (quit_requested())
            {
                set_led(0);
                return 0;
            }
        }
    }
}
