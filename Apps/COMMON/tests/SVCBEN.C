/* Link every public helper; the host fixture calls them directly. */
#include "../DXSVC.H"

int main(void)
{
    unsigned char data[2] = {1, 0};
    char reply[8];
    int result;

    result = svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 2, reply, 8);
    result += svc_operation_send(SERVICE_GROUP_CHAT, 1, data, 2, reply, 8);
    result += svc_get_text(SERVICE_GROUP_CLOCK, 0, reply, 8);
    result += svc_query_text(SERVICE_GROUP_UTILITY, 1, 0, reply, 8);
    result += svc_start_timer(0, 1);
    result += svc_timer_active(0);
    result += svc_access_stopwatch(0, 2, reply);
    result += svc_random_u16();
    return result;
}
