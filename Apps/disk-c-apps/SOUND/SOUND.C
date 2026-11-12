/* SOUND: self-checking tour of the port-21 speaker engine through DXSVC. */
#include <stdio.h>
#include <stdlib.h>
#include "DXSVC.H"

#define ACCEPTED 1
#define QUEUED 2
#define PRIORITY 3
#define QUEUE_FULL 4
#define NO_MATCH 5
#define UPDATED 6
#define STOPPED 7
#define CLEARED 8

static unsigned char st[SND_STATUS_BYTES];
static int failures;

static char *events[] = {
    "?", "started", "queued", "completed", "interrupted",
    "rejected", "updated", "stopped", "cancelled"
};

static char *presets[] = {
    "coin", "laser", "explosion", "engine", "alarm", "heartbeat",
    "treasure", "level complete", "pellet munch", "ricochet", "robot",
    "jump", "teleport", "shield", "extra life", "fanfare", "game over",
    "power pellet", "enemy capture", "paddle bounce", "wall bounce",
    "brick shatter", "ball lost", "invader footsteps", "UFO flyby",
    "countdown", "door unlock", "sonar ping", "combo multiplier",
    "player hit", "player death", "checkpoint", "item pickup",
    "menu move", "menu confirm", "menu cancel", "pause", "resume",
    "low health warning", "time running out", "enemy spawn",
    "enemy destroyed", "weapon charge", "boss appears", "falling whistle",
    "shield hit", "power-up expires", "empty weapon", "secret discovered",
    "round start", "bomb armed", "reload", "springboard", "splash",
    "door slam", "switch click", "landing thud", "skid", "warp failed",
    "magic cast", "freeze", "bonus tally", "objective failed"
};

/* Sequence tables: Hz, ms pairs; Hz 0 is a rest. */
static unsigned rests[] = {2000, 80, 0, 60, 2500, 80, 3200, 120};
static unsigned do_re_mi[] = {
    1047, 250, 1175, 250, 1319, 250, 1397, 250,
    1568, 250, 1760, 250, 1976, 250, 2093, 500
};
/* Segment table: end Hz, ms pairs joined from the start frequency. */
static unsigned envelope[] = {1800, 600, 2600, 200, 1400, 200, 3200, 400};

static void poll_key(void)
{
    if (kbhit()) {
        getch();
        snd_stop();
        printf("\nStopped by keypress.\n");
        exit(1);
    }
}

/* Let a sound run; the engine keeps playing while the Z80 polls timer 0. */
static void hold(unsigned ms)
{
    svc_start_timer(0, ms);
    while (svc_timer_active(0))
        poll_key();
}

static void wait_idle(void)
{
    while (snd_busy())
        poll_key();
}

static int expect(int id, int result)
{
    snd_read(st);
    if (st[SND_S_REQUEST_ID] == id && st[SND_S_RESULT] == result)
        return 1;
    printf("  FAIL: request %d gave result %d, expected %d\n",
           st[SND_S_REQUEST_ID], st[SND_S_RESULT], result);
    ++failures;
    return 0;
}

static void drain(void)
{
    while (snd_event())
        ;
}

static void show_events(void)
{
    int event;

    printf("  events:");
    while ((event = snd_event()) != 0)
        printf(" %s#%d", events[SND_EVENT_TYPE(event)], SND_EVENT_ID(event));
    printf("\n");
}

static void finish(int id)
{
    if (expect(id, ACCEPTED))
        wait_idle();
    hold(200);
}

static unsigned completed(void)
{
    snd_read(st);
    return ((unsigned)st[SND_S_COMPLETED] << 8) | st[SND_S_COMPLETED + 1];
}

static void primitives(void)
{
    printf("Timed tone.\n");
    finish(snd_tone(2, 2000, 100));
    printf("Power-up sweep.\n");
    finish(snd_sweep(2, 1000, 4000, 300));
    printf("Finite siren.\n");
    finish(snd_siren(2, 1500, 3500, 100, 150, 750));
    printf("Notes and rests, twice.\n");
    finish(snd_sequence(2, 4, 2, rests));
    printf("Do re mi: manual note sequence.\n");
    finish(snd_sequence(2, 8, 1, do_re_mi));
    printf("Joined pitch envelope.\n");
    finish(snd_segments(2, 4, 1, 300, envelope));
    printf("Accelerating countdown.\n");
    finish(snd_pulses(2, 12, 3000, 40, 240, 20));
    printf("Slowing radar.\n");
    finish(snd_pulses(2, 8, 3800, 60, 20, 200));
    printf("Rising UFO vibrato.\n");
    finish(snd_vibrato(2, 1200, 3200, 1500, 180, 120));
    printf("Major arpeggio.\n");
    finish(snd_arpeggio(2, 1047, 100, 1200, 0));
    printf("Minor arpeggio.\n");
    finish(snd_arpeggio(2, 1047, 100, 1200, 1));
    printf("Gated machine-gun buzz.\n");
    finish(snd_gate(2, 800, 30, 50, 800));
}

static void preset_tour(void)
{
    int preset;
    int id;

    for (preset = 1; preset <= SND_PRESET_COUNT; ++preset) {
        printf("Preset %d: %s.\n", preset, presets[preset - 1]);
        id = snd_preset(2, preset);
        if (preset == SND_ENGINE || preset == SND_HEARTBEAT ||
            preset == SND_LOW_HEALTH || preset == SND_TIME_WARNING) {
            if (expect(id, ACCEPTED))
                hold(1200);
            snd_stop();
            hold(200);
        } else {
            finish(id);
        }
    }
    printf("Scaled coin: lower/slower, then higher/faster.\n");
    snd_options(0, 75, 75);
    finish(snd_preset(2, SND_COIN));
    snd_options(0, 150, 150);
    finish(snd_preset(2, SND_COIN));
    snd_options(0, 100, 100);
}

static void feedback(void)
{
    unsigned before;
    int id;

    printf("Priority: alarm interrupts engine; coin rejected; laser replaces.\n");
    drain();
    expect(snd_preset(1, SND_ENGINE), ACCEPTED);
    expect(snd_preset(5, SND_ALARM), ACCEPTED);
    expect(snd_preset(1, SND_COIN), PRIORITY);
    expect(snd_preset(5, SND_LASER), ACCEPTED);
    wait_idle();
    show_events();

    printf("Queue: two sounds behind alarm; third is rejected.\n");
    before = completed();
    expect(snd_preset(5, SND_ALARM), ACCEPTED);
    snd_options(SND_ENQUEUE, 100, 100);
    expect(snd_preset(1, SND_COIN), QUEUED);
    expect(snd_preset(1, SND_LASER), QUEUED);
    expect(snd_preset(1, SND_ROBOT), QUEUE_FULL);
    snd_options(0, 100, 100);
    wait_idle();
    if (completed() - before != 3) {
        printf("  FAIL: queue did not complete three sounds\n");
        ++failures;
    }
    show_events();

    printf("Live engine: raise pitch and speed without restarting.\n");
    id = snd_preset(1, SND_ENGINE);
    expect(id, ACCEPTED);
    hold(500);
    expect(snd_update(id, SND_PITCH | SND_SPEED, 180, 160, 0, 0), UPDATED);
    hold(700);
    expect(snd_update(id == 255 ? 1 : id + 1, SND_PITCH, 180, 0, 0, 0),
           NO_MATCH);
    snd_options(SND_ENQUEUE, 100, 100);
    expect(snd_preset(1, SND_COIN), QUEUED);
    snd_options(0, 100, 100);
    expect(snd_clear(), CLEARED);
    expect(snd_stop(), STOPPED);
    show_events();

    printf("Live vibrato: deepen and quicken mid-sound.\n");
    id = snd_vibrato(2, 1200, 2400, 3000, 80, 200);
    expect(id, ACCEPTED);
    hold(800);
    expect(snd_update(id, SND_MODULATION, 0, 0, 200, 100), UPDATED);
    wait_idle();
    show_events();

    printf("Stop cancels the active sound and the queue.\n");
    expect(snd_preset(1, SND_HEARTBEAT), ACCEPTED);
    snd_options(SND_ENQUEUE, 100, 100);
    expect(snd_preset(1, SND_COIN), QUEUED);
    snd_options(0, 100, 100);
    expect(snd_stop(), STOPPED);
    wait_idle();
    show_events();
}

int main(void)
{
    if (!snd_init()) {
        printf("No protocol-%d speaker on port %d.\n",
               SOUND_PROTOCOL_VERSION, SOUND_PORT);
        return 1;
    }
    printf("Speaker engine ready; press any key to stop.\n");
    primitives();
    preset_tour();
    feedback();
    snd_read(st);
    printf("Done: %u completed, %u rejected, %d check failures.\n",
           ((unsigned)st[SND_S_COMPLETED] << 8) | st[SND_S_COMPLETED + 1],
           ((unsigned)st[SND_S_REJECTED] << 8) | st[SND_S_REJECTED + 1],
           failures);
    return failures != 0;
}
