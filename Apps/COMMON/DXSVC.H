/* ROMLESS-SVC/1 shared service client for dcc applications.
 * DXSVC.C includes the required Z80 assembly transport and validated timer APIs.
 */
#pragma once

/* Physical I/O ports. */
#define SERVICE_COMMAND_PORT 2
#define SERVICE_REQUEST_PORT 3
#define SERVICE_RESPONSE_PORT 4
#define INTERRUPT_TIMER_PORT 7
#define TIMER_IO_PORT 23

/* Shared-service command groups, not physical port numbers. */
#define SERVICE_GROUP_TIMERS 16
#define SERVICE_GROUP_STOPWATCHES 17
#define SERVICE_GROUP_CLOCK 32
#define SERVICE_GROUP_UTILITY 33
#define SERVICE_GROUP_ENVIRONMENT 48
#define SERVICE_GROUP_WEATHER 64
#define SERVICE_GROUP_CHAT 80
#define SERVICE_GROUP_SENSE_HAT 96
#define SERVICE_GROUP_HTTP 112
#define SERVICE_MAX_PAYLOAD 2048

/*
 * One shared transaction may be outstanding at a time.
 * Generic helpers return a byte count (0 also means COMPLETE) or a negative
 * service/transport error. Failures print a diagnostic by default and RESET the channel;
 * RESET does not undo service side effects. Binary replies are not terminated.
 * Timer, stopwatch and random convenience helpers exit(1) after an error.
 * Timer/stopwatch helpers use independent port 23, not the shared channel.
 * Their multi-byte operations must not be interleaved with other port-23 users.
 * A fresh program may recover an unknown parser state with inp(TIMER_IO_PORT) followed
 * by outp(TIMER_IO_PORT, 0); this resets only the interface, not running timers.
 */

/**
 * Enable (nonzero) or suppress (0) console error diagnostics; return the previous
 * setting. Restore it after requests whose errors are displayed by the caller.
 * Error returns, channel recovery and fatal timer behavior are unchanged.
 */
int svc_error_output(int enabled);

/**
 * Begin a low-level request to a service group, writing its length prefix.
 * request_length includes the operation byte (1..SERVICE_MAX_PAYLOAD).
 * Returns 0 or a negative error. On success, write exactly request_length bytes using
 * outp(SERVICE_REQUEST_PORT, byte), then call svc_finish_response(). Prefer svc_call_service()/svc_operation_send() normally.
 */
int svc_begin_request(int service_group, int request_length);

/**
 * Finish a fully transmitted request: wait for completion and read its reply.
 * response must hold response_capacity bytes (0..SERVICE_MAX_PAYLOAD).
 * NULL is allowed only when response_capacity is 0.
 * Returns the reply length, 0 for COMPLETE, or a negative error. An oversized
 * reply fills the available space, drains the excess, then returns -19.
 * Error replies are drained without copying into reply. Polling is limited to
 * 6,553,500 status reads, not a calibrated wall-clock timeout.
 */
int svc_finish_response(void *response, int response_capacity);

/**
 * Send a complete raw payload and receive a binary reply through svc_finish_response().
 * request is non-NULL and contains request_length bytes (1..SERVICE_MAX_PAYLOAD),
 * starting with the operation byte. Response rules follow svc_finish_response().
 */
int svc_call_service(int service_group, const void *request, int request_length,
                 void *response, int response_capacity);

/**
 * Send an operation followed by request_length bytes, without a combined buffer.
 * request_length is 0..SERVICE_MAX_PAYLOAD-1; request may be NULL only when
 * request_length is 0.
 * Response rules and return values follow svc_finish_response().
 */
int svc_operation_send(int service_group, int operation, const void *request,
                   int request_length, void *response, int response_capacity);

/**
 * Send an operation with no extra data and receive locally NUL-terminated text.
 * response is non-NULL; response_capacity includes the terminator
 * (1..SERVICE_MAX_PAYLOAD+1).
 * Returns the received byte count excluding NUL, or a negative error.
 * After a failed transaction, a valid destination is cleared to an empty string.
 */
int svc_get_text(int service_group, int operation, char *response,
             int response_capacity);

/**
 * Send [operation, one-byte selector] and receive NUL-terminated text.
 * Response rules, return values and transaction-failure clearing follow svc_get_text().
 * Valid operation/selector combinations depend on the selected service group.
 */
int svc_query_text(int service_group, int operation, int selector, char *response,
                int response_capacity);

/**
 * Start or replace countdown timer_id: IDs 0..2 use duration in milliseconds
 * (0..65535); ID 3 uses whole seconds (0..255). Zero expires immediately.
 * Returns 0 once armed; reports an error and exits(1) on failure.
 * Uses 3 OUTs for IDs 0..2 and 2 OUTs for ID 3, with no worker/status polling.
 */
int svc_start_timer(int timer_id, unsigned duration);

/**
 * Query timer_id (0..3): returns 1 while running, 0 when expired or unset.
 * Reports an error and exits(1) on failure or an invalid reply.
 * Uses one OUT to select the timer and one IN; works while services are BUSY.
 */
int svc_timer_active(int timer_id);

/**
 * Access stopwatch_id (0..2): operation 0 resets it, 1 reads elapsed seconds,
 * and 2 reads elapsed milliseconds. For operation 0, response may be NULL;
 * returns 0. For operation 1/2, response must hold four bytes; returns 4.
 * Those bytes are an unsigned little-endian 32-bit count, not text;
 * decode them explicitly.
 * Reads do not reset the stopwatch. Reports an error and exits(1) on failure.
 * Reset uses one OUT; reads use one OUT plus four INs from port 23.
 * Replies are latched at the command, so all four bytes share one timestamp.
 */
int svc_access_stopwatch(int stopwatch_id, int operation, void *response);

/**
 * Return a random unsigned 16-bit value (0..65535); zero is a valid result.
 * Reports an error and exits(1) on failure or an invalid reply length.
 */
unsigned svc_random_u16(void);

/**
 * @name Speaker sound API
 *
 * Speaker on port 21 (0x15): the Stage 11 ATtiny85 sound engine.
 * Requests are fire-and-forget packets; the engine owns timing, so a game sends
 * one request per sound and keeps running. Every request helper first issues an
 * IN to restart framing, then writes the packet with immediate OUTs, and returns
 * the request ID it assigned (1..255, wrapping and skipping 0). Status helpers
 * latch with OUT 0 and read only the bytes they need. Pitch and speed are
 * percentages (25..200, 100 = as written). Higher priority interrupts lower;
 * equal priority replaces; lower is rejected unless SND_ENQUEUE is set.
 * @{
 */
/** Dedicated speaker command/status I/O port (0x15); independent of services. */
#define SOUND_PORT 21
/** Expected version byte at SND_S_VERSION; checked by snd_init(). */
#define SOUND_PROTOCOL_VERSION 1

/* Effects for snd_fx(); prefer the wrapper macros below. */
/** Fixed-pitch timed beep; use for hits, clicks and custom musical notes. */
#define SND_FX_TONE 1
/** Linear glide between two pitches; use for jumps, falls and laser shots. */
#define SND_FX_SWEEP 2
/** Repeated rising/falling pitch sweep; use for alarms and emergency warnings. */
#define SND_FX_SIREN 3
/** Timed notes and rests; use for short tunes, jingles and rhythmic patterns. */
#define SND_FX_SEQUENCE 4
/** Joined pitch glides; use for custom ricochets, bounces and charge envelopes. */
#define SND_FX_SEGMENTS 5
/** Finite beeps with changing gaps; use for accelerating countdowns or radar. */
#define SND_FX_PULSES 6
/** Pitch sweep with periodic wobble; use for UFOs, unstable power or engines. */
#define SND_FX_VIBRATO 7
/** Rapid major/minor chord notes; use for treasure, magic and reward flourishes. */
#define SND_FX_ARPEGGIO 8
/** Repeated tone/silence gate; use for motor buzz, gunfire or steady alerts. */
#define SND_FX_GATE 9
/** Built-in sound selected by an SND_* preset ID; no note table is needed. */
#define SND_FX_PRESET 10

/* Frequencies are Hz and times are milliseconds, all 1..65535 unless noted. */
/** Play a fixed @p hz tone for @p ms milliseconds. */
#define snd_tone(priority, hz, ms) snd_fx(priority, SND_FX_TONE, hz, ms)
/** Glide from @p from_hz to @p to_hz over @p ms milliseconds. */
#define snd_sweep(priority, from_hz, to_hz, ms) \
    snd_fx(priority, SND_FX_SWEEP, from_hz, to_hz, ms)
/** Cycle between low/high Hz using rise/fall times; total_ms 0 repeats forever. */
#define snd_siren(priority, low_hz, high_hz, rise_ms, fall_ms, total_ms) \
    snd_fx(priority, SND_FX_SIREN, low_hz, high_hz, rise_ms, fall_ms, total_ms)
/** Play count (1..8) {Hz, ms} pairs; Hz 0 rests, repeats 0 loops forever. */
#define snd_sequence(priority, count, repeats, notes) \
    snd_fx(priority, SND_FX_SEQUENCE, count, repeats, notes)
/** Glide from start_hz through count (1..7) {target Hz, ms} pairs; repeats 0 loops. */
#define snd_segments(priority, count, repeats, start_hz, steps) \
    snd_fx(priority, SND_FX_SEGMENTS, count, repeats, start_hz, steps)
/** Emit count (1..64) beeps; gaps interpolate from start_gap_ms to end_gap_ms.
 * Either gap may be zero; on_ms must be nonzero.
 */
#define snd_pulses(priority, count, hz, on_ms, start_gap_ms, end_gap_ms) \
    snd_fx(priority, SND_FX_PULSES, count, hz, on_ms, start_gap_ms, end_gap_ms)
/** Sweep with +/-depth_hz wobble every period_ms (>= 2).
 * Both endpoint frequencies must exceed depth_hz and sum with it to <= 65535.
 */
#define snd_vibrato(priority, from_hz, to_hz, ms, depth_hz, period_ms) \
    snd_fx(priority, SND_FX_VIBRATO, from_hz, to_hz, ms, depth_hz, period_ms)
/** Arpeggiate a root_hz (1..32767) triad, changing note every step_ms.
 * minor 0 selects major, 1 minor; total_ms 0 repeats forever.
 */
#define snd_arpeggio(priority, root_hz, step_ms, total_ms, minor) \
    snd_fx(priority, SND_FX_ARPEGGIO, root_hz, step_ms, total_ms, minor)
/** Gate a fixed tone with nonzero on/off times; total_ms 0 repeats forever. */
#define snd_gate(priority, hz, on_ms, off_ms, total_ms) \
    snd_fx(priority, SND_FX_GATE, hz, on_ms, off_ms, total_ms)
/** Play a built-in preset (1..SND_PRESET_COUNT) with current pitch/speed options. */
#define snd_preset(priority, preset) snd_fx(priority, SND_FX_PRESET, preset)

/* snd_options() flags and snd_update() masks. */
/** Queue behind the active sound instead of replacing/rejecting it (two slots).
 * Stop or replace an indefinitely repeating sound to let queued playback start.
 */
#define SND_ENQUEUE 1
/** snd_update() mask: apply pitch percentage (25..200); 100 is original pitch. */
#define SND_PITCH 1
/** snd_update() mask: apply speed percentage (25..200); higher plays faster. */
#define SND_SPEED 2
/** snd_update() mask: set vibrato depth/period, preserving modulation phase. */
#define SND_MODULATION 4

/**
 * @par Built-in game sounds
 * Use with snd_preset(). Descriptions assume pitch/speed 100%.
 * Every preset is finite except ENGINE, HEARTBEAT, LOW_HEALTH and TIME_WARNING;
 * those repeat until snd_stop() or replacement by another sound.
 */
/** Two bright ascending notes (2600, 3900 Hz); coin collection or small reward. */
#define SND_COIN 1
/** Fast falling 4000-to-1000 Hz zap; laser shot, blaster or projectile launch. */
#define SND_LASER 2
/** Jagged eight-note burst ending in a low rumble; explosion or heavy destruction.
 * Synthesized pitched tones, not sampled noise.
 */
#define SND_EXPLOSION 3
/** Continuous 600 Hz gated buzz (40 ms on, 20 ms off); motor or vehicle idle.
 * Repeats until stopped/replaced; use snd_update() to vary revs during play.
 */
#define SND_ENGINE 4
/** Rising/falling 1500-3500 Hz siren for 2.4 seconds; danger or security alert. */
#define SND_ALARM 5
/** Low double beat with a long rest; heartbeat, tension or creature presence.
 * Repeats until stopped/replaced.
 */
#define SND_HEARTBEAT 6
/** Bright major arpeggio lasting about a second; treasure chest or major pickup. */
#define SND_TREASURE 7
/** Four ascending musical notes with a held finish; level clear or Tetris clear. */
#define SND_LEVEL 8
/** Three short cycles of alternating chirps and rests; pellet eating or chewing. */
#define SND_MUNCH 9
/** Three repeated down/up/down glides; ricocheting shot or bouncing energy bolt. */
#define SND_RICOCHET 10
/** Irregular high/low bleeps separated by rests; robot speech or computer chatter. */
#define SND_ROBOT 11
/** Quick upward 800-to-2800 Hz glide; jump, hop or small upward launch. */
#define SND_JUMP 12
/** Three cycles of ascending electronic chirps; teleport or phase transition. */
#define SND_TELEPORT 13
/** Smooth 1200-to-4200 Hz rise over 750 ms; shield activation or power build-up. */
#define SND_SHIELD 14
/** Rising reward notes ending in a high sustained ping; extra life or milestone. */
#define SND_EXTRA_LIFE 15
/** Rhythmic bright victory phrase with a high finish; win, trophy or boss defeat. */
#define SND_FANFARE 16
/** Five descending notes ending low; game over or end-of-run failure. */
#define SND_GAME_OVER 17
/** Three quick rising triplets with short rests; temporary power-up activation. */
#define SND_POWER_PELLET 18
/** Very fast 700-to-4200 Hz rise; enemy capture or successful target collection. */
#define SND_CAPTURE 19
/** Two short rising blips; paddle contact, ball return or light bat impact. */
#define SND_PADDLE 20
/** Single 35 ms high beep; wall bounce, collision edge or movement boundary. */
#define SND_WALL 21
/** Three sharply descending notes; brick break, tile destruction or block clear. */
#define SND_BRICK 22
/** Long 1800-to-300 Hz downward glide; lost ball, dropped piece or missed catch. */
#define SND_BALL_LOST 23
/** Four descending steps with shrinking rests; approaching invaders or footsteps. */
#define SND_FOOTSTEPS 24
/** Rising pitch with a regular wobble; UFO flyby or moving science-fiction object. */
#define SND_UFO 25
/** Twelve high beeps with shrinking gaps; short countdown or imminent launch.
 * Finite automatic acceleration, not a timer synchronized to game seconds.
 */
#define SND_COUNTDOWN 26
/** Separated rising bleeps followed by an unlock flourish; door opening or access. */
#define SND_DOOR 27
/** Two high pings separated by long rests; sonar, radar contact or distant beacon. */
#define SND_SONAR 28
/** Eight rapidly ascending notes; combo reward, chain reaction or score multiplier. */
#define SND_COMBO 29
/** Short down/up/down pitch jolt; player damage, knockback or glancing impact. */
#define SND_PLAYER_HIT 30
/** Wobbling descent from 2200 to 350 Hz; player death or dramatic life loss. */
#define SND_PLAYER_DEATH 31
/** Three ascending reward notes; checkpoint, saved progress or stage milestone. */
#define SND_CHECKPOINT 32
/** Four quick high notes with an upward finish; item, key or ammunition pickup. */
#define SND_PICKUP 33
/** Tiny 12 ms mid-high tick; menu navigation, cursor movement or Tetris rotation. */
#define SND_MENU_MOVE 34
/** Two rising notes with a longer finish; confirm selection or accept an action. */
#define SND_MENU_CONFIRM 35
/** Two falling notes; cancel, back out of a menu or decline an action. */
#define SND_MENU_CANCEL 36
/** Three descending musical notes; pause gameplay or enter a suspended state. */
#define SND_PAUSE 37
/** Three ascending musical notes; resume gameplay or leave a suspended state. */
#define SND_RESUME 38
/** Repeating high double beep with a long rest; low health or critical resource.
 * Repeats until stopped/replaced; stop when the warning condition clears.
 */
#define SND_LOW_HEALTH 39
/** Repeating low/high warning pair; running out of time or sustained urgency.
 * Repeats until stopped/replaced; stop when the warning condition clears.
 */
#define SND_TIME_WARNING 40
/** Stepped rising glides with brief dips; enemy materialization or new threat. */
#define SND_ENEMY_SPAWN 41
/** Short jagged descending burst; enemy destruction or small explosive hit. */
#define SND_ENEMY_DESTROYED 42
/** Wobbling 600-to-3600 Hz rise over 1.6 seconds; weapon or ability charging.
 * Finite effect; game logic still decides when the charged action is ready.
 */
#define SND_CHARGE 43
/** Low dramatic repeated notes rising to a held octave; boss entrance or threat. */
#define SND_BOSS 44
/** Long high-to-low whistle (4200 to 250 Hz); falling object or deep drop. */
#define SND_FALLING 45
/** Brief sparkling high-note impact; shield absorbs a hit or energy deflection. */
#define SND_SHIELD_HIT 46
/** Five descending musical notes; temporary power expires or protection ends. */
#define SND_POWER_EXPIRES 47
/** Very short low/high click pair; empty weapon, dry fire or unavailable action. */
#define SND_EMPTY_WEAPON 48
/** Uneven rising discovery phrase with a high finish; secret room or hidden item. */
#define SND_SECRET 49
/** Two short repeated notes then a held octave above; round start or ready/go. */
#define SND_ROUND_START 50
/** Sixteen short high beeps with shrinking gaps; armed bomb or accelerating fuse.
 * Finite effect, not a timer synchronized to the game's detonation deadline.
 */
#define SND_BOMB_ARMED 51
/** Three rising mechanical-style clicks with rests; reload or equipment reset. */
#define SND_RELOAD 52
/** Elastic rise, dip and higher launch; springboard, trampoline or strong bounce. */
#define SND_SPRINGBOARD 53
/** Irregular descending splash-like bleeps; water entry or liquid impact. */
#define SND_SPLASH 54
/** Low down/up/down slam envelope; heavy door closure or crushing impact. */
#define SND_DOOR_SLAM 55
/** Crisp two-note descending click; switch toggle, lever or interface state change. */
#define SND_SWITCH 56
/** Short low 450-to-100 Hz drop; landing thud, Tetris hard drop or heavy contact. */
#define SND_LANDING 57
/** Fast-wobbling descending squeal; skid, braking or sliding friction. */
#define SND_SKID 58
/** Rising warp attempt that dips, recovers and collapses; failed teleport or spell. */
#define SND_WARP_FAIL 59
/** Fast bright major arpeggio; spell cast, magical item or enchanted interaction. */
#define SND_MAGIC 60
/** High rising/dipping glides with a held finish; freeze ray, ice or stasis. */
#define SND_FREEZE 61
/** Twenty-four short beeps with shrinking gaps; bonus counting or score tally.
 * Automatic finite pattern; does not track individual points awarded by the game.
 */
#define SND_BONUS_TALLY 62
/** Slow descending phrase ending an octave low; failed objective or round loss. */
#define SND_OBJECTIVE_FAILED 63
/** Number of valid built-in presets; IDs run from 1 through this value. */
#define SND_PRESET_COUNT 63

/* snd_event() types; SND_EVENT_TYPE/ID split its return value. */
/** Playback began; ID identifies the sound, including a dequeued request. */
#define SND_STARTED 1
/** Request entered the playback FIFO; it has not started sounding yet. */
#define SND_QUEUED 2
/** Sound finished naturally; use to advance game-side audio sequencing. */
#define SND_COMPLETED 3
/** Active sound was replaced by another request; its playback did not finish. */
#define SND_INTERRUPTED 4
/** Request was refused; consult SND_S_RESULT for the latest request's reason. */
#define SND_REJECTED 5
/** Live settings changed; event ID identifies the updated playing sound. */
#define SND_UPDATED 6
/** Active playback ended via snd_stop(), not natural completion. */
#define SND_STOPPED 7
/** Queued sound was removed by stop/clear before playback began. */
#define SND_CANCELLED 8
/** Extract an SND_STARTED..SND_CANCELLED type from snd_event()'s packed value. */
#define SND_EVENT_TYPE(event) ((unsigned)(event) >> 8)
/** Extract the associated request ID (low byte) from snd_event()'s packed value. */
#define SND_EVENT_ID(event) ((event) & 255)

/* snd_read() status offsets (19 bytes); 16-bit fields are high byte first. */
/** Byte offset: engine protocol version, expected to equal SOUND_PROTOCOL_VERSION. */
#define SND_S_VERSION 0
/** Byte offset: bit 0 playing, bit 1 command pending, bit 2 event FIFO overflow. */
#define SND_S_FLAGS 1
/** Byte offset: currently playing request ID; zero when idle. */
#define SND_S_ACTIVE_ID 2
/** Byte offset: active sound's priority; meaningful while playing. */
#define SND_S_PRIORITY 3
/** Byte offset: active primitive's SND_FX_* ID; presets report their expanded effect. */
#define SND_S_EFFECT 4
/** Byte offset: active SND_* preset ID, or zero for a manual effect/idle. */
#define SND_S_PRESET 5
/** Byte offset: number of queued sounds (0..2), excluding active playback. */
#define SND_S_QUEUED 6
/** Offset of two-byte big-endian output frequency in Hz; zero during silence. */
#define SND_S_FREQUENCY 7
/** Byte offset: ID of the latest processed play/stop/update/clear request. */
#define SND_S_REQUEST_ID 9
/** Byte offset: latest result: 0 none, 1 accepted, 2 queued, 3 priority rejection,
 * 4 queue full, 5 no matching active ID, 6 updated, 7 stopped, 8 cleared, 9 invalid.
 */
#define SND_S_RESULT 10
/** Offset of oldest event's two-byte big-endian acknowledgement sequence number. */
#define SND_S_EVENT_SEQUENCE 11
/** Byte offset: request ID associated with the oldest pending event. */
#define SND_S_EVENT_ID 13
/** Byte offset: oldest SND_STARTED..SND_CANCELLED event type, or zero if none. */
#define SND_S_EVENT_TYPE 14
/** Offset of two-byte big-endian natural-completion count; wraps at 65536. */
#define SND_S_COMPLETED 15
/** Offset of two-byte big-endian rejected-request count; wraps at 65536. */
#define SND_S_REJECTED 17
/** Required buffer size in bytes for snd_read(); not a field offset. */
#define SND_STATUS_BYTES 19

/**
 * Restart framing, reset options to (0, 100, 100), stop all sound and discard
 * pending events. Returns 1 when a protocol-1 speaker answers, otherwise 0.
 */
int snd_init(void);

/**
 * Set the flags (SND_ENQUEUE), pitch and speed sent with later snd_fx() calls.
 */
void snd_options(int flags, int pitch, int speed);

/**
 * Play effect at priority (0..255) with the effect's fields as int arguments;
 * a sequence/segment table is a pointer to unsigned 16-bit values.
 * Returns the request ID, or 0 without any I/O for an unknown effect. The
 * engine validates fields; check snd_read() or snd_event() when unsure.
 */
int snd_fx(int priority, int effect, ...);

/** Silence the current sound and clear the queue. Returns the request ID. */
int snd_stop(void);

/** Clear queued sounds, leaving the current one playing. Returns the request ID. */
int snd_clear(void);

/**
 * Change the playing sound only if its ID is target. mask selects SND_PITCH,
 * SND_SPEED and SND_MODULATION (vibrato depth_hz and period_ms >= 2).
 * Returns the request ID; a mismatch is reported as result 5 (no match).
 */
int snd_update(int target, int mask, int pitch, int speed, unsigned depth_hz,
               unsigned period_ms);

/** Latch and copy the 19-byte status. Uses 1 OUT and 20 INs. */
void snd_read(unsigned char *status);

/** Return the playing request ID, or 0 when silent. Uses 1 OUT and 4 INs. */
int snd_busy(void);

/** Poll snd_busy() until the current sound and its queue finish. */
void snd_wait(void);

/**
 * Return the oldest event as (type << 8) | id and acknowledge it, or 0 when
 * none is pending. Up to four events are kept; older ones set flag 4.
 */
int snd_event(void);
/** @} */
