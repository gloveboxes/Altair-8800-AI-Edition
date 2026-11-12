/* Link every speaker helper; the host fixture calls them directly. */
#include "../DXSVC.H"

int main(void)
{
    static unsigned notes[2] = {440, 10};
    unsigned char status[SND_STATUS_BYTES];
    int result;

    snd_options(0, 100, 100);
    result = snd_sequence(0, 1, 1, notes);
    result += snd_stop() + snd_clear() + snd_update(1, SND_PITCH, 100, 100, 0, 2);
    snd_read(status);
    result += snd_busy() + snd_event() + snd_init();
    snd_wait();
    return result;
}
