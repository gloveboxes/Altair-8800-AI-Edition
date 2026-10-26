/* The House of Tides: an original Zork-style adventure for dcc / CP/M.
 * 80x30 VT100 cabinet, raw BIOS output, deterministic puzzle/NPC rules.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../COMMON/DXSVC.H"

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 30
#define TEXT_COLUMN 4
#define TEXT_WIDTH 72
#define LOG_ROW 15
#define LOG_LINES 10
#define INPUT_ROW 27
#define INPUT_COLUMN 6
#define LINE_SIZE 70
#define CONSOLE_BUFFER_SIZE 1024
#define LAMP_LIFE 160
#define BATTERY_CHARGE 120
#define MAX_LAMP_FUEL (LAMP_LIFE + BATTERY_CHARGE)
#define MAX_SCORE 300
#define CARRIED (-1)
#define USED_UP (-2)
#define HIDDEN (-3)
#define BANKED (-4)

enum direction { NORTH, EAST, SOUTH, WEST, UP, DOWN, DIRECTION_COUNT };
enum room_id {
    START, HALL, LIBRARY, CELLAR, CRYPT, GALLERY, VAULT, GARDEN, TOWER,
    KITCHEN, PANTRY, WORKSHOP, ORCHARD, GREENHOUSE, WELL, TUNNEL,
    PUMP_ROOM, RESERVOIR, SHRINE, GUARDROOM, BRIDGE, MINE, ARCHIVE,
    OBSERVATORY, ROOM_COUNT
};
enum item_id {
    LAMP, KEY, COIN, IDOL, ROPE, BREAD, OIL, WRENCH, GEAR, LENS,
    CHALICE, CROWN, GEM, JOURNAL, FLUTE, BATTERY, ITEM_COUNT
};
enum warden_mode { WARDEN_PATROL, WARDEN_ALERT, WARDEN_ASLEEP, WARDEN_FRIENDLY };

static const char *room_names[ROOM_COUNT] = {
    "West of the House", "Entrance Hall", "Dusty Library", "Stone Cellar",
    "Quiet Crypt", "Portrait Gallery", "Treasure Vault", "Walled Garden",
    "Clock Tower", "Abandoned Kitchen", "Pantry", "Mechanic's Workshop",
    "Overgrown Orchard", "Ruined Greenhouse", "Bottom of the Well",
    "Echoing Tunnel", "Pump Room", "Underground Reservoir", "Tidal Shrine",
    "Clockwork Guardroom", "Bridge of Gears", "Crystal Mine", "Sealed Archive",
    "Star Observatory"
};
static const char *room_descriptions[ROOM_COUNT] = {
    "The House of Tides has stood still since its keeper vanished. An empty "
    "cabinet awaits three lost treasures: idol, chalice and crown. The house "
    "is east; its garden is north.",
    "Rain has stained the hall's patterned tiles. A hook hangs beside the "
    "front door west. The kitchen is north, the library east; stone steps "
    "descend to the cellar.",
    "Shelves crowd a table covered in the keeper's notes. A passage leads "
    "north to the archive. A margin reads: 'Silver opens the old way; "
    "brass guards the idol.' The hall lies west.",
    "Dust softens the stone floor. The crypt lies north, a mechanic's "
    "workshop west. Steps climb up to the hall. The sound of distant "
    "water comes from behind the walls.",
    "A stone slab seals the eastern crawlway. Its arch contains a narrow "
    "coin slot. An inscription promises passage to anyone paying the "
    "keeper's toll. The cellar is south.",
    "Faded portraits watch a brass-locked vault door to the east. Mechanical "
    "footsteps echo from a guardroom north. The crypt is west.",
    "A stone plinth stands under a dusty skylight. Its inscription reads "
    "'The first treasure remembers the earth.' The vault's only exit is west.",
    "An ivy-covered wall shelters the garden. A broken ladder leads up "
    "to the clock tower. The orchard is east and the house south. "
    "An iron anchor could hold a climbing rope.",
    "A clock with an empty gear socket overlooks the garden below. A lens "
    "mount points at shutters overhead. A plaque reads: 'Time, tide and "
    "light open the way to the stars.'",
    "Cold ovens line the kitchen. The keeper's maintenance list mentions "
    "clock oil, a jammed pump and a hungry bird. The pantry is east; "
    "the hall is south.",
    "A dry pantry holds the last supplies left by the keeper. A battery "
    "wrapper promises another 120 turns of lamp light. The kitchen is west.",
    "Tools hang above a metal case secured with hexagonal bolts. A label "
    "inside its glass lid reads CLOCK GEAR. A chalk sketch shows a pump "
    "valve connected to the observatory. The cellar is east.",
    "Roots break through the orchard path. A greenhouse stands north. "
    "Old stone steps spiral down a dry well. The garden lies west.",
    "A raven guards a nest high in a dead vine. Something glassy catches "
    "the daylight beneath its claws. The orchard is south.",
    "Stone steps climb to the orchard above. A rotted satchel rests in "
    "the silt. The keeper's initials are stitched into it. A low tunnel "
    "heads east toward the waterworks.",
    "Every footstep answers itself in the dark. A pipe runs north toward "
    "the pump room. The well lies west. A scratched arrow labels the "
    "pipe: 'Tide engine - service with wrench.'",
    "A pump shudders against a loose housing. A wheel valve is closed "
    "beside it. The tunnel is south; a walkway follows the pipe east "
    "to the reservoir.",
    "Black water covers the northern causeway to a shrine. A pipe leads "
    "west to the pump room. Iron stairs climb up to a bridge of gears. "
    "A gauge shows the water level is dangerously high.",
    "A narrow causeway leads south over the drained reservoir. Shells "
    "surround a stone altar. 'The second treasure remembers the sea,' "
    "reads a band of worn copper.",
    "A clockwork warden patrols the way east to a bridge. Its joints "
    "screech with every step. The gallery is south. The warden's chest "
    "plate bears the words SERVICE WITH OIL.",
    "Brass teeth turn far below the bridge. The guardroom is west, a "
    "crystal mine north. Stairs descend to the reservoir. A faded "
    "warning recommends music when passing the warden.",
    "Quartz gleams in the walls. A jewelled survey marker lies among "
    "the old workings. This forgotten reward is outside the keeper's "
    "three-treasure quest. The bridge is south.",
    "The keeper's ghost stands among sealed folios. It searches empty "
    "pockets as if something has been lost. A music stand faces the "
    "door south to the library.",
    "The dome is open. A crown waits inside a glass star case. A riddle "
    "asks: 'I end the night and begin the day. Name me.' The tower "
    "stair descends beneath the telescope."
};
/* North, east, south, west, up, down. Every ordinary link is reciprocal. */
static const signed char room_exits[ROOM_COUNT][DIRECTION_COUNT] = {
    { GARDEN, HALL, -1, -1, -1, -1 },
    { KITCHEN, LIBRARY, -1, START, -1, CELLAR },
    { ARCHIVE, -1, -1, HALL, -1, -1 },
    { CRYPT, -1, -1, WORKSHOP, HALL, -1 },
    { -1, GALLERY, CELLAR, -1, -1, -1 },
    { GUARDROOM, VAULT, -1, CRYPT, -1, -1 },
    { -1, -1, -1, GALLERY, -1, -1 },
    { -1, ORCHARD, START, -1, TOWER, -1 },
    { -1, -1, -1, -1, OBSERVATORY, GARDEN },
    { -1, PANTRY, HALL, -1, -1, -1 },
    { -1, -1, -1, KITCHEN, -1, -1 },
    { -1, CELLAR, -1, -1, -1, -1 },
    { GREENHOUSE, -1, -1, GARDEN, -1, WELL },
    { -1, -1, ORCHARD, -1, -1, -1 },
    { -1, TUNNEL, -1, -1, ORCHARD, -1 },
    { PUMP_ROOM, -1, -1, WELL, -1, -1 },
    { -1, RESERVOIR, TUNNEL, -1, -1, -1 },
    { SHRINE, -1, -1, PUMP_ROOM, BRIDGE, -1 },
    { -1, -1, RESERVOIR, -1, -1, -1 },
    { -1, BRIDGE, GALLERY, -1, -1, -1 },
    { MINE, -1, -1, GUARDROOM, -1, RESERVOIR },
    { -1, -1, BRIDGE, -1, -1, -1 },
    { -1, -1, LIBRARY, -1, -1, -1 },
    { -1, -1, -1, -1, -1, TOWER }
};
static const char *item_names[ITEM_COUNT] = {
    "lamp", "key", "coin", "idol", "rope", "bread", "oil", "wrench",
    "gear", "lens", "chalice", "crown", "gem", "journal", "flute", "battery"
};
static const char *item_descriptions[ITEM_COUNT] = {
    "An electric lamp. Its charge lasts 160 lit turns; one spare battery adds 120.",
    "A brass key, stamped with the same crest as the gallery's vault lock.",
    "A silver toll coin. The crypt's slot looks just wide enough to accept it.",
    "A jade idol with soil caught in its carving. Return it to the cabinet outside.",
    "A long climbing rope. Secure it to the garden's iron anchor with USE ROPE.",
    "A surprisingly intact loaf. A hungry bird might part with its prize for this.",
    "A refillable oil can. There is enough for the tower clock and the warden.",
    "A heavy wrench. Its jaws match the workshop case bolts and the pump housing.",
    "A brass clock gear. It fits the empty socket in the tower's mechanism.",
    "A telescope lens. The tower has an empty mount below the observatory shutters.",
    "A silver chalice, cool with sea spray. Return it to the cabinet outside.",
    "A star crown, the keeper's last treasure. Return it to the cabinet outside.",
    "A ruby survey marker. It is an optional treasure for the cabinet outside.",
    "The keeper's journal. READ JOURNAL reveals its story; the ghost may want it back.",
    "A little flute. Its lullaby can quiet the clockwork warden for a few turns.",
    "A single-use lamp battery. USE BATTERY works even when your lamp is empty."
};
static const char *direction_names[DIRECTION_COUNT] = {
    "north", "east", "south", "west", "up", "down"
};
static const char *direction_short[DIRECTION_COUNT] = { "n", "e", "s", "w", "u", "d" };

static const char *intro_paragraphs[4] = {
    "On a forgotten coast stands a house where the clocks have stopped. "
    "Its garden is wild, its halls are silent, and strange machinery "
    "sleeps beneath the stone.",
    "The keeper who tended this place has vanished. Only a restless ghost, "
    "a stubborn raven and a tireless brass guardian remain. Below them, "
    "buried passages lead toward black water and forgotten shrines.",
    "Recover the jade idol, silver chalice and star crown. Bring them home "
    "to the cabinet west of the house, and awaken the bond between earth, "
    "sea and stars.",
    "Explore with N, E, S, W, U and D. EXAMINE what you find and TALK to "
    "those you meet. JOURNAL follows your quest; HINT offers a nudge "
    "when the way is unclear."
};

static uint8_t console_buffer[CONSOLE_BUFFER_SIZE];
static unsigned console_buffer_used;
static char transcript[LOG_LINES][TEXT_WIDTH + 1];
static uint8_t transcript_lines;
static signed char item_locations[ITEM_COUNT];
static bool collected[ITEM_COUNT], visited[ROOM_COUNT];
static bool lamp_on, vault_open, slab_open, rope_tied, case_open;
static bool clock_oiled, gear_fitted, clock_running, pump_fixed, valve_open;
static bool raven_fed, journal_returned, warden_friendly, lens_fitted, crown_open;
static bool quit_requested, game_won;
static enum warden_mode warden_state;
static uint8_t warden_ticks;
static unsigned lamp_fuel, turns, score;
static enum room_id room;

static void console_flush(void)
{
    unsigned index;
    /* BIOS CONOUT bypasses BDOS look-ahead and Ctrl-S/Left flow control. */
    for (index = 0; index < console_buffer_used; ++index)
        bios(4, console_buffer[index]);
    console_buffer_used = 0;
}

static inline void console_put_byte(uint8_t byte)
{
    if (console_buffer_used == CONSOLE_BUFFER_SIZE)
        console_flush();
    console_buffer[console_buffer_used++] = byte;
}

static void write_text(const char *text)
{
    uint8_t byte;
    while (*text) {
        byte = (unsigned char)*text++;
        console_put_byte(byte);
    }
}

static void write_unsigned(unsigned value)
{
    char digits[5];
    uint8_t count = 0, byte;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        byte = (uint8_t)digits[--count];
        console_put_byte(byte);
    }
}

static void cursor_move(uint8_t row, uint8_t column)
{
    write_text("\033[");
    write_unsigned(row);
    console_put_byte(';');
    write_unsigned(column);
    console_put_byte('H');
}

static void set_color(uint8_t color)
{
    write_text("\033[");
    write_unsigned(color);
    console_put_byte('m');
}

static void reset_color(void) { write_text("\033[0m"); }

static void write_spaces(uint8_t count)
{
    while (count--) console_put_byte(' ');
}

static void clear_panel_row(uint8_t row)
{
    reset_color();
    cursor_move(row, TEXT_COLUMN);
    write_spaces(TEXT_WIDTH + 2);
}

/* Same alternating grey/white cabinet pattern as Breakout. */
static uint8_t checker_color(uint8_t row, uint8_t column)
{
    return (((row >> 1) + (column >> 1)) & 1) ? 100 : 107;
}

static void draw_cabinet(void)
{
    uint8_t row, column;
    /* Disable wrapping so painting the bottom-right border cannot scroll. */
    write_text("\033[0m\033[2J\033[H\033[?25l\033[?7l");
    for (column = 0; column < SCREEN_WIDTH; column += 2) {
        set_color(checker_color(0, column >> 1));
        cursor_move(2, column + 1);
        write_text("  ");
        set_color(checker_color(SCREEN_HEIGHT - 1, column >> 1));
        cursor_move(SCREEN_HEIGHT, column + 1);
        write_text("  ");
    }
    for (row = 3; row < SCREEN_HEIGHT; ++row) {
        set_color(checker_color(row - 2, 0));
        cursor_move(row, 1);
        write_text("  ");
        set_color(checker_color(row - 2, (SCREEN_WIDTH >> 1) - 1));
        cursor_move(row, SCREEN_WIDTH - 1);
        write_text("  ");
    }
    reset_color();
    cursor_move(1, 3);
    set_color(96);
    write_text("ZORK / CP-M");
    reset_color();
}

static void draw_frame(void)
{
    uint8_t row, column;
    draw_cabinet();
    set_color(90);
    for (row = 13; row <= 26; row += 13) {
        cursor_move(row, TEXT_COLUMN);
        for (column = 0; column < TEXT_WIDTH + 2; ++column)
            console_put_byte('-');
    }
    cursor_move(14, TEXT_COLUMN);
    set_color(96);
    write_text("ADVENTURE LOG");
    cursor_move(29, TEXT_COLUMN);
    set_color(37);
    write_text("N/E/S/W/U/D  X  TAKE  USE  GIVE  PUT  JOURNAL  HINT  HELP  Q");
    reset_color();
}

/* Wrap at spaces; every returned line fits within the cabinet's margins. */
static void wrap_line(const char **text, char *line)
{
    const char *start = *text;
    unsigned length = 0, last_space = 0;
    while (*start == ' ') ++start;
    while (start[length] && start[length] != '\n' && length < TEXT_WIDTH) {
        if (start[length] == ' ') last_space = length;
        ++length;
    }
    if (length == TEXT_WIDTH && start[length] && start[length] != ' ' && last_space)
        length = last_space;
    memcpy(line, start, length);
    line[length] = 0;
    start += length;
    while (*start == ' ') ++start;
    if (*start == '\n') ++start;
    *text = start;
}

static void write_centered(uint8_t row, const char *text)
{
    cursor_move(row, (uint8_t)((SCREEN_WIDTH - strlen(text)) / 2 + 1));
    write_text(text);
}

static uint8_t draw_intro_paragraph(uint8_t row, const char *text)
{
    char line[TEXT_WIDTH + 1];
    while (*text) {
        wrap_line(&text, line);
        cursor_move(row++, TEXT_COLUMN);
        write_text(line);
    }
    return row;
}

static void draw_intro(void)
{
    draw_cabinet();
    set_color(96);
    write_centered(4, "Z O R K");
    set_color(37);
    write_centered(5, "A Zork-inspired text adventure");
    set_color(93);
    write_centered(6, "T H E   H O U S E   O F   T I D E S");
    set_color(37);
    draw_intro_paragraph(8, intro_paragraphs[0]);
    draw_intro_paragraph(12, intro_paragraphs[1]);
    set_color(93);
    write_centered(16, "YOUR QUEST");
    set_color(37);
    draw_intro_paragraph(17, intro_paragraphs[2]);
    set_color(96);
    write_centered(21, "LISTEN. EXAMINE. EXPLORE.");
    set_color(37);
    draw_intro_paragraph(22, intro_paragraphs[3]);
    set_color(93);
    write_centered(27, "Press ENTER to begin   /   Q to return to CP/M");
    set_color(90);
    write_centered(29, "An original text adventure inspired by Zork.");
    reset_color();
    console_flush();
}

static void message(const char *text)
{
    do {
        if (transcript_lines == LOG_LINES) {
            memmove(transcript[0], transcript[1],
                    (LOG_LINES - 1) * (TEXT_WIDTH + 1));
            --transcript_lines;
        }
        wrap_line(&text, transcript[transcript_lines++]);
    } while (*text);
}


static bool carried(int item) { return item_locations[item] == CARRIED; }
static bool water_running(void) { return pump_fixed && valve_open; }
static bool dome_ready(void) { return clock_running && water_running() && lens_fitted; }
static bool is_treasure(int item)
{
    return item == IDOL || item == CHALICE || item == CROWN || item == GEM;
}
static unsigned treasures_home(void)
{
    return (unsigned)(item_locations[IDOL] == BANKED) +
           (unsigned)(item_locations[CHALICE] == BANKED) +
           (unsigned)(item_locations[CROWN] == BANKED);
}
static bool can_see(void)
{
    switch (room) {
    case CELLAR: case CRYPT: case WORKSHOP: case WELL:
    case TUNNEL: case PUMP_ROOM: case MINE:
        return lamp_on && (carried(LAMP) || item_locations[LAMP] == (int)room);
    default: return true;
    }
}

static bool passage_blocked(int direction)
{
    return (room == CRYPT && direction == EAST && !slab_open) ||
           (room == GALLERY && direction == EAST && !vault_open) ||
           (room == GARDEN && direction == UP && !rope_tied) ||
           (room == TOWER && direction == UP && !dome_ready()) ||
           (room == RESERVOIR && direction == NORTH && !water_running()) ||
           (room == GUARDROOM && direction == EAST && !warden_friendly &&
            warden_state != WARDEN_ASLEEP);
}

static const char *current_description(void)
{
    if (!can_see()) return "It is too dark to see. Light your lamp or retrace "
        "your steps. Your compass and the stairs still work in the dark.";
    if (room == START && game_won) return "Dawn shines on the awakened House "
        "of Tides. Its clock chimes above a flowing tide engine. The three "
        "treasures rest together in the cabinet, their long journey complete.";
    if (room == START && treasures_home()) return "The cabinet's carved "
        "crests glow around the treasures you have brought home. The house "
        "is east and the garden north. Its remaining treasures still wait.";
    if (room == CRYPT && slab_open) return "The stone slab has slid aside, "
        "revealing an eastern crawlway to the gallery. The cellar lies south. "
        "The toll coin has vanished into the ancient mechanism.";
    if (room == GALLERY && vault_open) return "The eastern vault door stands "
        "open. Portraits of the keeper's family line the way north to the "
        "guardroom. The crypt is west.";
    if (room == GARDEN && rope_tied) return "A rope is secured beside the broken "
        "ladder. You can climb up to the tower, walk east to the orchard, "
        "or return south to the house.";
    if (room == TOWER && dome_ready()) return "The clock ticks, water turns the "
        "tide engine, and your lens gathers the light. The shutters overhead "
        "are open: climb UP to the observatory, or DOWN to the garden.";
    if (room == TOWER && clock_running) return "The repaired clock ticks "
        "steadily. A lens mount points at the shutters above. Time alone "
        "cannot open them: they also need running water and a lens.";
    if (room == TOWER && gear_fitted && clock_oiled) return "The clock's gear "
        "is fitted and its bearings are oiled. It waits to be wound. A lens "
        "mount points at the shutters above; the garden is below.";
    if (room == TOWER && gear_fitted) return "The new gear fits the clock, "
        "but its dry bearings resist turning. An oil can would help. The "
        "dome is above the lens mount, the garden below.";
    if (room == TOWER && clock_oiled) return "The clock's bearings are oiled, "
        "but a socket still awaits its missing gear. A lens mount points "
        "at the shutters above. The garden is below.";
    if (room == WORKSHOP && case_open) return "The bolted case is open. A "
        "maintenance diagram links the tower clock, the pump valve and a "
        "telescope lens to the observatory. The cellar is east.";
    if (room == GREENHOUSE && raven_fed) return "The raven eats happily beside "
        "its nest. It has let go of the telescope lens. Broken panes admit "
        "shafts of sunlight. The orchard is south.";
    if (room == PUMP_ROOM && water_running()) return "The repaired pump hums "
        "and water surges through the open valve toward the tide engine. "
        "The reservoir is east, the tunnel south.";
    if (room == PUMP_ROOM && pump_fixed) return "The pump housing is secure. "
        "Its wheel valve remains closed, ready to be turned. The reservoir "
        "is east and the tunnel south.";
    if (room == RESERVOIR && water_running()) return "The water has fallen "
        "below the northern causeway. The shrine can now be reached. Iron "
        "stairs lead up to the bridge; the pump room is west.";
    if (room == ARCHIVE && journal_returned) return "The keeper's ghost reads "
        "the returned journal with a quiet smile. A little music stand is "
        "near the sealed folios. The library is south.";
    if (room == OBSERVATORY && crown_open) return "The star case stands open. "
        "Beyond the telescope, a pale dawn reaches across the sea. The "
        "clock tower lies down the winding stair.";
    return room_descriptions[room];
}

static void draw_game(void)
{
    uint8_t row, index;
    bool any = false;
    const char *text;
    char line[TEXT_WIDTH + 1];
    char objects[ITEM_COUNT * 12 + 12];
    write_text("\033[?25l");
    cursor_move(1, 21); reset_color(); write_spaces(59);
    cursor_move(1, 22); set_color(37);
    write_text("SCORE:"); write_unsigned(score);
    console_put_byte('/'); write_unsigned(MAX_SCORE);
    cursor_move(1, 38); write_text("TURNS:"); write_unsigned(turns);
    cursor_move(1, 56); set_color(lamp_on ? 93 : 90);
    write_text(lamp_on ? "LAMP:ON " : "LAMP:OFF "); write_unsigned(lamp_fuel);
    reset_color();
    for (row = 4; row <= 12; ++row) clear_panel_row(row);
    cursor_move(4, TEXT_COLUMN); set_color(96);
    write_text(can_see() ? room_names[room] : "Darkness");
    cursor_move(5, TEXT_COLUMN); set_color(90);
    write_text("THE HOUSE OF TIDES   Treasures home: ");
    write_unsigned(treasures_home()); write_text("/3"); reset_color();
    text = current_description();
    for (row = 6; *text && row <= 8; ++row) {
        wrap_line(&text, line);
        cursor_move(row, TEXT_COLUMN); write_text(line);
    }
    cursor_move(9, TEXT_COLUMN); set_color(93);
    if (room == GUARDROOM) {
        if (warden_friendly) write_text("The warden salutes its new mechanic.");
        else if (warden_state == WARDEN_ASLEEP) write_text("The warden sleeps. Its gears will soon wake.");
        else if (warden_state == WARDEN_ALERT) write_text("The warden watches you. Do not linger without a plan.");
        else write_text("The warden paces across the eastern doorway.");
    }
    if (room == ARCHIVE) write_text(journal_returned ?
        "The ghost nods gratefully." : "The ghost whispers: 'My journal... lost in the well...'");
    reset_color();
    strcpy(objects, "You see: ");
    if (can_see()) {
        for (index = 0; index < ITEM_COUNT; ++index) {
            if (item_locations[index] == (int)room) {
                if (any) strcat(objects, ", ");
                strcat(objects, item_names[index]); any = true;
            }
        }
    }
    if (!any) strcat(objects, can_see() ? "nothing loose" : "nothing in the dark");
    text = objects;
    for (row = 10; *text && row <= 11; ++row) {
        wrap_line(&text, line); cursor_move(row, TEXT_COLUMN);
        set_color(93); write_text(line);
    }
    cursor_move(12, TEXT_COLUMN); set_color(37); write_text("Exits: ");
    for (index = 0; index < DIRECTION_COUNT; ++index) {
        if (room_exits[room][index] >= 0) {
            write_text(direction_names[index]);
            if (passage_blocked(index)) write_text("(blocked)");
            write_text("  ");
        }
    }
    reset_color();
    for (index = 0; index < LOG_LINES; ++index) {
        clear_panel_row(LOG_ROW + index);
        if (index < transcript_lines) {
            cursor_move(LOG_ROW + index, TEXT_COLUMN); write_text(transcript[index]);
        }
    }
    clear_panel_row(INPUT_ROW); cursor_move(INPUT_ROW, TEXT_COLUMN);
    set_color(96); write_text("> "); reset_color();
    write_text("\033[?25h"); console_flush();
}

static void visit_room(void)
{
    if (can_see() && !visited[room]) { visited[room] = true; ++score; }
}

static void initialize_game(void)
{
    static const signed char initial_locations[ITEM_COUNT] = {
        HALL, LIBRARY, CELLAR, VAULT, GARDEN, PANTRY, KITCHEN, WORKSHOP,
        HIDDEN, HIDDEN, SHRINE, HIDDEN, MINE, WELL, HIDDEN, PANTRY
    };
    memcpy(item_locations, initial_locations, sizeof item_locations);
    memset(collected, 0, sizeof collected); memset(visited, 0, sizeof visited);
    lamp_on = vault_open = slab_open = rope_tied = case_open = false;
    clock_oiled = gear_fitted = clock_running = pump_fixed = valve_open = false;
    raven_fed = journal_returned = warden_friendly = lens_fitted = crown_open = false;
    quit_requested = game_won = false;
    warden_state = WARDEN_PATROL; warden_ticks = 0;
    lamp_fuel = LAMP_LIFE; turns = score = 0; transcript_lines = 0; room = START;
    visit_room();
    message("THE HOUSE OF TIDES");
    message("Recover the idol, chalice and crown. PUT each IN the cabinet here.");
    message("The keeper left clues. EXAMINE things, TALK to characters; try JOURNAL or HINT.");
}

/* Optional atmosphere: the shared random service never controls a solution. */
static void ambient_message(void)
{
    unsigned char request = 0, response[2];
    int previous, result;
    if (room != GALLERY) return;
    previous = svc_error_output(0);
    result = svc_call_service(SERVICE_GROUP_UTILITY, &request, 1, response, 2);
    svc_error_output(previous);
    if (result == 2 && ((unsigned)response[0] + ((unsigned)response[1] << 8)) % 19 == 0)
        message("A portrait's eyes seem to follow you.");
}

static bool move_player(int direction)
{
    int destination = room_exits[room][direction];
    if (destination < 0) { message("You cannot go that way."); return false; }
    if (passage_blocked(direction)) {
        if (room == CRYPT) message("The slab blocks the crawlway. Examine the coin slot.");
        else if (room == GALLERY) message("The vault is locked. A brass key might fit.");
        else if (room == GARDEN) message("The ladder is unsafe. Secure a rope to the anchor first.");
        else if (room == RESERVOIR) message("The causeway is submerged. Repair the pump and open its valve.");
        else if (room == GUARDROOM) message("The warden bars the bridge. Oil its joints or lull it with music.");
        else if (!clock_running) message("The dome needs time: repair, oil and WIND the tower clock.");
        else if (!water_running()) message("The dome needs tide: restore the pump and open its valve.");
        else message("The dome needs light: fit a lens to the tower's telescope mount.");
        return false;
    }
    room = (enum room_id)destination;
    message(can_see() ? room_names[room] : "You enter the darkness.");
    ambient_message();
    return true;
}

static int find_item(const char *noun)
{
    int index;
    for (index = 0; index < ITEM_COUNT; ++index)
        if (!strcmp(noun, item_names[index])) return index;
    if (!strcmp(noun, "light") || !strcmp(noun, "lantern")) return LAMP;
    if (!strcmp(noun, "spanner")) return WRENCH;
    if (!strcmp(noun, "diary")) return JOURNAL;
    if (!strcmp(noun, "ruby")) return GEM;
    return -1;
}
static int find_direction(const char *word)
{
    int index;
    for (index = 0; index < DIRECTION_COUNT; ++index)
        if (!strcmp(word, direction_names[index]) || !strcmp(word, direction_short[index])) return index;
    return -1;
}
static bool take_item(int item)
{
    if (item < 0) { message("Take what? Try TAKE LAMP."); return false; }
    if (carried(item)) { message("You already have it."); return false; }
    if (!can_see()) { message("It is too dark to find anything."); return false; }
    if (item_locations[item] != (int)room) {
        message("You see no such thing within reach here."); return false;
    }
    item_locations[item] = CARRIED;
    if (!collected[item]) { collected[item] = true; ++score; }
    message("Taken.");
    return true;
}
static bool drop_item(int item)
{
    if (item < 0 || !carried(item)) { message("You are not carrying that."); return false; }
    item_locations[item] = (signed char)room; message("Dropped."); return true;
}
static bool set_lamp(int mode)
{
    bool desired = mode < 0 ? !lamp_on : mode != 0;
    if (!carried(LAMP) && item_locations[LAMP] != (int)room) {
        message("Your lamp is not here."); return false;
    }
    if (desired && !lamp_fuel) {
        message("The lamp is empty. USE BATTERY, or return for the pantry's spare."); return false;
    }
    if (desired == lamp_on) {
        message(desired ? "The lamp is already lit." : "The lamp is already off."); return false;
    }
    lamp_on = desired;
    message(lamp_on ? "The lamp glows warmly." : "You switch off the lamp."); return true;
}

static bool target_matches(int item, const char *target)
{
    if (!*target) return true;
    switch (item) {
    case LAMP: case BATTERY: return !strcmp(target, "lamp");
    case KEY: return !strcmp(target, "door") || !strcmp(target, "vault") || !strcmp(target, "lock");
    case COIN: return !strcmp(target, "slot") || !strcmp(target, "slab");
    case ROPE: return !strcmp(target, "anchor") || !strcmp(target, "ladder");
    case BREAD: return !strcmp(target, "raven") || !strcmp(target, "bird");
    case OIL: return (room == TOWER && !strcmp(target, "clock")) ||
                   (room == GUARDROOM && !strcmp(target, "warden"));
    case WRENCH: return (room == WORKSHOP && (!strcmp(target, "case") || !strcmp(target, "bolts"))) ||
                      (room == PUMP_ROOM && !strcmp(target, "pump"));
    case GEAR: return !strcmp(target, "clock") || !strcmp(target, "socket");
    case LENS: return !strcmp(target, "mount") || !strcmp(target, "telescope");
    case JOURNAL: return !strcmp(target, "ghost") || !strcmp(target, "keeper");
    case FLUTE: return !strcmp(target, "warden");
    default: return false;
    }
}

static bool use_item(int item, const char *target)
{
    if (item < 0) { message("Use what? Try USE KEY ON DOOR."); return false; }
    if (!target_matches(item, target)) { message("That does not belong there."); return false; }
    if (item == LAMP) return set_lamp(-1);
    if (!carried(item)) { message("You are not carrying that."); return false; }
    if (item == BATTERY) {
        if (!carried(LAMP) && item_locations[LAMP] != (int)room) {
            message("Bring your lamp here first."); return false;
        }
        if (lamp_fuel == MAX_LAMP_FUEL) { message("The lamp is fully charged."); return false; }
        lamp_fuel += BATTERY_CHARGE;
        if (lamp_fuel > MAX_LAMP_FUEL) lamp_fuel = MAX_LAMP_FUEL;
        item_locations[BATTERY] = USED_UP;
        message("You fit the spare battery. The lamp gains 120 turns of charge."); return true;
    }
    if (!can_see()) { message("You need light to do that."); return false; }
    if (item == KEY && room == GALLERY) {
        if (vault_open) { message("The vault is already open."); return false; }
        vault_open = true; score += 10;
        message("The key turns. The vault door swings open."); return true;
    }
    if (item == COIN && room == CRYPT) {
        if (slab_open) { message("The crawlway is already open."); return false; }
        item_locations[COIN] = USED_UP; slab_open = true; score += 10;
        message("The coin drops into the slot. The slab opens an eastern crawlway."); return true;
    }
    if (item == ROPE && room == GARDEN) {
        item_locations[ROPE] = USED_UP; rope_tied = true; score += 5;
        message("You secure the rope. It is now safe to climb UP to the tower."); return true;
    }
    if (item == WRENCH && room == WORKSHOP) {
        if (case_open) { message("The gear case is already open."); return false; }
        case_open = true; item_locations[GEAR] = WORKSHOP; score += 5;
        message("The bolts loosen. You open the case and reveal a clock gear."); return true;
    }
    if (item == WRENCH && room == PUMP_ROOM) {
        if (pump_fixed) { message("The pump housing is already secure."); return false; }
        pump_fixed = true; score += 10;
        message("You tighten the pump housing. Now TURN VALVE to start the water."); return true;
    }
    if (item == GEAR && room == TOWER) {
        item_locations[GEAR] = USED_UP; gear_fitted = true; score += 10;
        message("The gear settles into the socket. The clock still needs oil and winding."); return true;
    }
    if (item == OIL && room == TOWER) {
        if (clock_oiled) { message("The clock is already oiled."); return false; }
        clock_oiled = true; score += 5;
        message("Oil frees the clock's bearings. A fitted gear and WIND CLOCK will set it going."); return true;
    }
    if (item == OIL && room == GUARDROOM) {
        if (warden_friendly) { message("The warden's joints are already running smoothly."); return false; }
        warden_friendly = true; warden_state = WARDEN_FRIENDLY; score += 15;
        message("You oil the warden. Its blade lowers; it salutes and welcomes its mechanic."); return true;
    }
    if (item == BREAD && room == GREENHOUSE) {
        item_locations[BREAD] = USED_UP; raven_fed = true;
        item_locations[LENS] = GREENHOUSE; score += 10;
        message("The raven trades its prize for your loaf. A glass lens drops into the moss."); return true;
    }
    if (item == LENS && room == TOWER) {
        item_locations[LENS] = USED_UP; lens_fitted = true; score += 10;
        message("The lens clicks into the telescope mount. Light reaches the dome mechanism."); return true;
    }
    if (item == JOURNAL && room == ARCHIVE) {
        item_locations[JOURNAL] = USED_UP; journal_returned = true;
        item_locations[FLUTE] = ARCHIVE; score += 20;
        message("The ghost reads its lost journal. 'Now I remember. I can finally leave.'");
        message("It gives you a flute: 'My warden still remembers this lullaby.'"); return true;
    }
    if (item == FLUTE && room == GUARDROOM) {
        if (warden_friendly || warden_state == WARDEN_ASLEEP) {
            message("The warden is already peaceful."); return false;
        }
        warden_state = WARDEN_ASLEEP; warden_ticks = 4;
        message("The lullaby stills the warden. You have a few turns to pass."); return true;
    }
    message("You cannot use that here."); return false;
}

static bool open_passage(const char *noun)
{
    if (room == CRYPT && (!*noun || !strcmp(noun, "slab") || !strcmp(noun, "slot") || !strcmp(noun, "crawlway"))) {
        if (slab_open) { message("The crawlway is already open."); return false; }
        return use_item(COIN, "slot");
    }
    if (room == GALLERY && (!*noun || !strcmp(noun, "door") || !strcmp(noun, "vault"))) return use_item(KEY, "door");
    if (room == WORKSHOP && (!*noun || !strcmp(noun, "case"))) return use_item(WRENCH, "case");
    if (room == OBSERVATORY && (!*noun || !strcmp(noun, "case"))) {
        message(crown_open ? "The star case is already open." : "The case listens for the answer to its riddle. SAY your answer."); return false;
    }
    message("There is nothing like that to open here."); return false;
}

static bool operate(const char *noun)
{
    if (!can_see()) { message("You need light to work the mechanism."); return false; }
    if (!strcmp(noun, "valve") && room == PUMP_ROOM) {
        if (!pump_fixed) { message("The loose pump would burst. Repair its housing first."); return false; }
        if (valve_open) { message("The valve is already open."); return false; }
        valve_open = true; score += 5;
        message("You turn the valve. The reservoir drains and the tide engine begins to turn."); return true;
    }
    if (!strcmp(noun, "clock") && room == TOWER) {
        if (!gear_fitted || !clock_oiled) {
            message("The clock needs its missing gear and oil before it can be wound."); return false;
        }
        if (clock_running) { message("The clock is already ticking."); return false; }
        clock_running = true; score += 5;
        message("You wind the clock. Its first chime in a hundred years rings across the house."); return true;
    }
    message("There is no such mechanism to operate here."); return false;
}

static bool deposit(int item)
{
    if (room != START) { message("The treasure cabinet is outside, west of the house."); return false; }
    if (item < 0 || !is_treasure(item)) { message("The cabinet holds the idol, chalice, crown and optional gem."); return false; }
    if (!carried(item)) { message("You are not carrying that treasure."); return false; }
    item_locations[item] = BANKED;
    score += item == CROWN ? 40 : item == GEM ? 15 : 25;
    if (item == IDOL) message("The jade idol settles into the cabinet. Its earth crest shines green.");
    else if (item == CHALICE) message("The silver chalice rests beside the sea crest. It glows like moonlit water.");
    else if (item == CROWN) message("The star crown fills the cabinet's last great hollow. Tiny stars begin to burn.");
    else message("You add the mine's ruby to the cabinet, an unexpected gift for the keeper.");
    return true;
}

static void inventory(void)
{
    char list[ITEM_COUNT * 12 + 24];
    bool any = false;
    int index;
    strcpy(list, "You are carrying: ");
    for (index = 0; index < ITEM_COUNT; ++index) if (carried(index)) {
        if (any) strcat(list, ", ");
        strcat(list, item_names[index]); any = true;
    }
    if (!any) strcat(list, "nothing");
    message(list);
}
static void quest_journal(void)
{
    message("YOUR QUEST: bank the idol, chalice and crown in the cabinet outside.");
    if (item_locations[IDOL] == BANKED) message("Idol: safely home in the cabinet.");
    else if (carried(IDOL)) message("Idol: in your pack, ready to return home.");
    else message(vault_open ? "Idol: the gallery vault is open." : "Idol: follow the cellar's silver toll to the brass-locked vault.");
    if (item_locations[CHALICE] == BANKED) message("Chalice: safely home in the cabinet.");
    else if (carried(CHALICE)) message("Chalice: in your pack, ready to return home.");
    else message(water_running() ? "Chalice: the reservoir causeway is clear; seek the northern shrine." : "Chalice: the waterworks need a repaired pump and an open valve.");
    message(clock_running ? "Tower: the keeper's clock is ticking." : "Tower: recover the workshop gear, oil the clock, then wind it.");
    message(lens_fitted ? "Light: the telescope lens is fitted." : "Light: a raven in the greenhouse guards something glassy.");
    if (item_locations[CROWN] == BANKED) message("Crown: safely home in the cabinet.");
    else if (carried(CROWN)) message("Crown: in your pack, ready to return home.");
    else message(crown_open ? "Crown: the star case is open." : "Crown: time, tide and light open the dome; a spoken answer opens the case.");
    message(journal_returned ? "Keeper: the ghost has found its memories." : "Optional: the archive's ghost seeks a journal lost in the well.");
    message("The mine's gem is optional. Bank it before completing the three-treasure quest.");
}
static void hint(void)
{
    if (!collected[LAMP]) message("A lamp hangs in the entrance hall. Take it before going below.");
    else if (room == ARCHIVE && !journal_returned) message("Recover the journal at the bottom of the orchard's well, then GIVE JOURNAL TO GHOST.");
    else if (room == GREENHOUSE && !raven_fed) message("A loaf from the pantry might persuade the raven. GIVE BREAD TO RAVEN.");
    else if (room == GUARDROOM && !warden_friendly) message("USE OIL ON WARDEN for lasting peace, or PLAY FLUTE for a short lullaby.");
    else if (room == WORKSHOP && !case_open) message("The wrench in this room fits the case bolts. OPEN CASE.");
    else if (room == CRYPT && !slab_open) message("Take the cellar's silver coin and USE COIN ON SLOT.");
    else if (room == GALLERY && !vault_open) message("The library's brass key fits the vault door.");
    else if (room == PUMP_ROOM && !pump_fixed) message("USE WRENCH ON PUMP before you TURN VALVE.");
    else if (room == TOWER && !clock_running) message("Fit the workshop gear, oil the clock with the kitchen's oil, then WIND CLOCK.");
    else if (room == OBSERVATORY && !crown_open) message("What ends the night and begins the day? SAY DAWN.");
    else if (!slab_open) message("The crypt's coin slot leads to the first treasure. Seek a silver coin below the hall.");
    else if (!vault_open) message("The idol lies behind the gallery's brass lock; the key is in the library.");
    else if (!water_running()) message("Descend the orchard's well. The tunnel reaches a pump: repair it and turn the valve.");
    else if (!dome_ready()) message("The tower's dome needs a ticking clock, running water and a fitted telescope lens.");
    else message("Carry the three main treasures home and PUT each IN CABINET. JOURNAL tracks the other tasks.");
}
static void help(void)
{
    message("Move: N E S W U D, GO NORTH, CLIMB UP. LOOK / L redraws the room.");
    message("Objects: TAKE KEY, DROP KEY, USE KEY ON DOOR, OPEN CASE.");
    message("Discover: EXAMINE / X, READ, SEARCH, TALK TO GHOST, HINT, JOURNAL.");
    message("Interact: GIVE BREAD TO RAVEN, WIND CLOCK, TURN VALVE, PLAY FLUTE.");
    message("Light: LIGHT LAMP, EXTINGUISH LAMP, USE BATTERY. I lists your items.");
    message("Bank treasures: PUT IDOL IN CABINET or DEPOSIT ALL, outside the house.");
    message("SAY an answer; WAIT lets time pass. SCORE, RESTART, QUIT / Q.");
    message("Only successful actions cost turns. Looking, talking and hints are free.");
}

static void read_journal(void)
{
    message("The keeper wrote: 'I linked the dome to time, tide and focused light.'");
    message("'The clock needs a gear and oil. The pump must be mended before its valve opens.'");
    message("'If the star case forgets me, I shall speak DAWN. Let the three treasures come home.'");
    message("The last page asks that this journal be returned to its owner in the archive.");
}
static void examine(const char *noun)
{
    int item = find_item(noun);
    if (!can_see() && item != LAMP && item != BATTERY) { message("It is too dark to examine that."); return; }
    if (item >= 0 && (carried(item) || item_locations[item] == (int)room)) {
        if (item == JOURNAL) read_journal();
        else message(item_descriptions[item]);
    } else if (room == LIBRARY && (!strcmp(noun, "note") || !strcmp(noun, "notes") || !strcmp(noun, "table") || !strcmp(noun, "shelves"))) {
        message("'Silver opens the crypt. Brass opens the vault. Time, tide and light open the dome.'");
        message("Another note says the keeper disappeared while inspecting the orchard's well.");
    } else if (room == CRYPT && (!strcmp(noun, "slot") || !strcmp(noun, "slab")))
        message(slab_open ? "The eastern crawlway is open." : "The slot accepts a silver coin. Try USE COIN ON SLOT.");
    else if (room == GALLERY && (!strcmp(noun, "door") || !strcmp(noun, "lock")))
        message(vault_open ? "The vault door is open." : "The brass lock matches the library's key.");
    else if (room == WORKSHOP && (!strcmp(noun, "case") || !strcmp(noun, "bolts")))
        message(case_open ? "The clock gear's case is open." : "Hexagonal bolts hold the case shut. A wrench would fit.");
    else if (room == GARDEN && (!strcmp(noun, "ladder") || !strcmp(noun, "anchor")))
        message(rope_tied ? "The rope now makes the tower climb safe." : "A rope tied to the iron anchor would make the ladder safe.");
    else if (room == TOWER && (!strcmp(noun, "clock") || !strcmp(noun, "plaque"))) {
        message(gear_fitted ? "The clock's gear is in place." : "The clock has an empty gear socket. Search the workshop's case.");
        message(clock_oiled ? "Its bearings have been oiled." : "The bearings are dry. There may be oil in the kitchen.");
        message(clock_running ? "The clock is ticking." : "Once the gear and oil are ready, WIND CLOCK.");
    } else if (room == TOWER && (!strcmp(noun, "mount") || !strcmp(noun, "shutters") || !strcmp(noun, "telescope")))
        message("A mount takes a glass lens. The dome interlock also needs the clock and the waterworks.");
    else if (room == GREENHOUSE && (!strcmp(noun, "raven") || !strcmp(noun, "bird") || !strcmp(noun, "nest")))
        message(raven_fed ? "The raven has traded its glass prize for bread." : "The raven's nest contains a lens. A hungry bird may accept bread.");
    else if (room == PUMP_ROOM && !strcmp(noun, "pump"))
        message(pump_fixed ? "The housing is secure. The wheel valve controls the flow." : "The housing is loose. Tighten it with a wrench before opening the valve.");
    else if (room == PUMP_ROOM && !strcmp(noun, "valve"))
        message(valve_open ? "The valve is open and the tide engine has water." : "The wheel valve is closed. TURN VALVE once the pump is safe.");
    else if (room == OBSERVATORY && (!strcmp(noun, "case") || !strcmp(noun, "riddle")))
        message(crown_open ? "The star case is open." : "'I end the night and begin the day. Name me.' SAY your answer.");
    else if (room == START && !strcmp(noun, "cabinet"))
        message("The cabinet accepts the idol, chalice, crown and optional mine gem. PUT a treasure IN CABINET.");
    else if (room == WELL && !strcmp(noun, "satchel"))
        message("The rotted satchel holds the keeper's journal, still dry inside its cover.");
    else if (room == GUARDROOM && !strcmp(noun, "warden"))
        message("Its joints need oil. A chest plate also describes a musical sleep override.");
    else message("You find no further clue in that here.");
}
static void talk(const char *noun)
{
    if (room == ARCHIVE && (!*noun || !strcmp(noun, "ghost") || !strcmp(noun, "keeper"))) {
        if (!journal_returned) {
            message("'I was checking the well when I lost everything. My journal knows what I no longer can.'");
            message("'Please bring it back. I once played a flute to calm my guardian.'");
        } else {
            message("'Thank you. The star case opens to DAWN. Bring our treasures home.'");
            message("'The oil can will make my warden gentle. The flute can put it briefly to sleep.'");
        }
    } else if (room == GUARDROOM && (!*noun || !strcmp(noun, "warden")))
        message(warden_friendly ? "'Mechanic recognised. Bridge access granted. Thank you for the oil.'" : "'Maintenance overdue. Oil required. Unauthorised visitors will be escorted out.'");
    else if (room == GREENHOUSE && (!*noun || !strcmp(noun, "raven") || !strcmp(noun, "bird")))
        message(raven_fed ? "The raven croaks a satisfied thank-you." : "The raven croaks: 'Bread! Bread!' and grips the glass prize.");
    else message("No one here answers you.");
}

/* A small deterministic NPC state machine: alert, temporary musical sleep,
 * and permanent friendship. Mistakes cause an escort, never random death. */
static void advance_warden(void)
{
    if (warden_friendly) { warden_state = WARDEN_FRIENDLY; return; }
    if (warden_state == WARDEN_ASLEEP) {
        if (warden_ticks) --warden_ticks;
        if (!warden_ticks) {
            warden_state = WARDEN_PATROL;
            if (room == GUARDROOM) message("The warden wakes and resumes its patrol.");
        }
    } else if (room == GUARDROOM) {
        if (warden_state != WARDEN_ALERT) {
            warden_state = WARDEN_ALERT; warden_ticks = 2;
            message("The warden raises its blade. Act soon, or retreat south.");
        } else if (warden_ticks && --warden_ticks == 0) {
            room = GALLERY; warden_state = WARDEN_PATROL;
            message("The warden gently escorts you back to the gallery. Your belongings are safe.");
        }
    } else { warden_state = WARDEN_PATROL; warden_ticks = 0; }
}
static void finish_turn(void)
{
    visit_room();
    if (turns < 65535u) ++turns;
    if (lamp_on && lamp_fuel) {
        --lamp_fuel;
        if (!lamp_fuel) { lamp_on = false; message("Your lamp goes out. Fit the pantry's battery if you still have it."); }
        else if (lamp_fuel == 20) message("Your lamp is growing dim: twenty lit turns remain.");
    }
    advance_warden();
    if (treasures_home() == 3 && !game_won) {
        game_won = true; score += 20;
        message("Earth, sea and stars shine together. The house's windows fill with dawn.");
        if (journal_returned) message("The keeper steps into the sunlight, smiles, and is finally at peace.");
        else message("A grateful whisper rises from the archive. One story still waits to be told.");
        message("YOU HAVE WON! Press ENTER to return to CP/M.");
    }
}

static char *next_word(char **input)
{
    char *word;
    while (**input == ' ' || **input == '\t') ++*input;
    word = *input;
    while (**input && **input != ' ' && **input != '\t') ++*input;
    if (**input) *(*input)++ = 0;
    return word;
}
static char *object_word(char **input)
{
    char *word = next_word(input);
    if (!strcmp(word, "the") || !strcmp(word, "a")) word = next_word(input);
    if (!strcmp(word, "brass") || !strcmp(word, "silver") || !strcmp(word, "jade") ||
        !strcmp(word, "glass") || !strcmp(word, "star") || !strcmp(word, "clockwork"))
        word = next_word(input);
    return word;
}
static void command(char *line)
{
    char *cursor = line, *verb, *noun, *prep, *target;
    int direction, item, index;
    bool spent_turn = false;
    if (quit_requested || game_won) return;
    verb = next_word(&cursor);
    if (!*verb) return;
    noun = object_word(&cursor);
    if ((!strcmp(verb, "talk") && !strcmp(noun, "to")) ||
        ((!strcmp(verb, "look") || !strcmp(verb, "l")) && !strcmp(noun, "at")))
        noun = object_word(&cursor);
    prep = next_word(&cursor); target = prep;
    if (*prep) {
        if (strcmp(prep, "on") && strcmp(prep, "in") && strcmp(prep, "into") &&
            strcmp(prep, "to") && strcmp(prep, "at") && strcmp(prep, "with")) {
            message("Try a command such as USE KEY ON DOOR or GIVE JOURNAL TO GHOST."); return;
        }
        target = object_word(&cursor);
        if (!*target || *next_word(&cursor)) {
            message("Please name one object and, if needed, one target."); return;
        }
        if (strcmp(verb, "use") && strcmp(verb, "give") && strcmp(verb, "put") &&
            strcmp(verb, "deposit") && strcmp(verb, "feed")) {
            message("That command does not take a second object."); return;
        }
    }
    direction = find_direction(verb);
    if (direction >= 0 && !*noun && !*prep) spent_turn = move_player(direction);
    else if (!strcmp(verb, "go")) {
        direction = find_direction(noun);
        if (direction < 0 || *prep) message("Go where? Use NORTH, EAST, SOUTH, WEST, UP or DOWN.");
        else spent_turn = move_player(direction);
    } else if (!strcmp(verb, "look") || !strcmp(verb, "l")) {
        if (*noun) examine(noun);
        else { visit_room(); message(can_see() ? room_names[room] : "It is too dark to see."); }
    } else if (!strcmp(verb, "help") || !strcmp(verb, "?")) help();
    else if (!strcmp(verb, "inventory") || !strcmp(verb, "i")) inventory();
    else if (!strcmp(verb, "journal") || !strcmp(verb, "quests")) quest_journal();
    else if (!strcmp(verb, "hint")) hint();
    else if (!strcmp(verb, "score")) message("The maximum is 300: exploration, first finds, solved puzzles and banked treasures.");
    else if (!strcmp(verb, "quit") || !strcmp(verb, "q")) {
        quit_requested = true; message("Goodbye, adventurer.");
    } else if (!strcmp(verb, "restart")) initialize_game();
    else if (!strcmp(verb, "wait") || !strcmp(verb, "z")) {
        message("Time passes. The house creaks."); spent_turn = true;
    } else if (!strcmp(verb, "take") || !strcmp(verb, "get")) {
        if (!strcmp(noun, "all")) {
            if (!can_see()) message("It is too dark to find anything.");
            else for (index = 0; index < ITEM_COUNT; ++index)
                if (item_locations[index] == (int)room && take_item(index)) spent_turn = true;
            if (!spent_turn && can_see()) message("Nothing loose is within reach.");
        } else spent_turn = take_item(find_item(noun));
    } else if (!strcmp(verb, "drop")) spent_turn = drop_item(find_item(noun));
    else if (!strcmp(verb, "use") || !strcmp(verb, "give"))
        spent_turn = use_item(find_item(noun), target);
    else if (!strcmp(verb, "feed") && (!strcmp(noun, "raven") || !strcmp(noun, "bird"))) {
        if (*target && strcmp(target, "bread")) message("The raven wants bread.");
        else spent_turn = use_item(BREAD, "raven");
    }
    else if (!strcmp(verb, "play") && !strcmp(noun, "flute"))
        spent_turn = use_item(FLUTE, "warden");
    else if (!strcmp(verb, "light") || !strcmp(verb, "extinguish")) {
        item = *noun ? find_item(noun) : LAMP;
        if (item != LAMP) message("That is not a lamp.");
        else spent_turn = set_lamp(!strcmp(verb, "light") ? 1 : 0);
    } else if (!strcmp(verb, "open") || !strcmp(verb, "unlock")) spent_turn = open_passage(noun);
    else if (!strcmp(verb, "turn") || !strcmp(verb, "wind")) spent_turn = operate(noun);
    else if (!strcmp(verb, "repair")) {
        if (!strcmp(noun, "pump")) spent_turn = use_item(WRENCH, "pump");
        else if (!strcmp(noun, "clock")) spent_turn = use_item(gear_fitted ? OIL : GEAR, "clock");
        else message("Examine the mechanism to learn what it needs.");
    } else if (!strcmp(verb, "put") || !strcmp(verb, "deposit")) {
        if (*target && strcmp(target, "cabinet")) message("Treasures belong in the cabinet outside.");
        else if (room != START) message("The treasure cabinet is outside, west of the house.");
        else if (!strcmp(noun, "all")) {
            for (index = 0; index < ITEM_COUNT; ++index)
                if (is_treasure(index) && carried(index) && deposit(index)) spent_turn = true;
            if (!spent_turn) message("You have no treasures to deposit here.");
        } else spent_turn = deposit(find_item(noun));
    } else if (!strcmp(verb, "say")) {
        if (room != OBSERVATORY) message("Your word echoes through the house.");
        else if (crown_open) message("The star case is already open.");
        else if (!strcmp(noun, "dawn") || !strcmp(noun, "daybreak") || !strcmp(noun, "sunrise")) {
            crown_open = true; item_locations[CROWN] = OBSERVATORY; score += 15;
            message("The word DAWN lights the stars on the case. Its glass lid opens."); spent_turn = true;
        } else message("The stars flicker, but the case stays shut. Read its riddle.");
    } else if (!strcmp(verb, "talk")) talk(noun);
    else if (!strcmp(verb, "examine") || !strcmp(verb, "x") || !strcmp(verb, "read") || !strcmp(verb, "search")) examine(noun);
    else if (!strcmp(verb, "climb")) {
        if (room == GARDEN && (!*noun || !strcmp(noun, "up") || !strcmp(noun, "rope"))) spent_turn = move_player(UP);
        else if ((room == TOWER || room == OBSERVATORY) && !strcmp(noun, "down")) spent_turn = move_player(DOWN);
        else if (room == TOWER && (!*noun || !strcmp(noun, "up"))) spent_turn = move_player(UP);
        else if (room == ORCHARD && !strcmp(noun, "down")) spent_turn = move_player(DOWN);
        else if (room == WELL && !strcmp(noun, "up")) spent_turn = move_player(UP);
        else message("Use UP or DOWN at a stair or a safe ladder.");
    } else message("I do not understand. Type HELP for commands.");
    if (spent_turn) finish_turn();
}


static int read_key(void)
{
    int key;
    console_flush();
    do { key = bdos(6, 0xff) & 0xff; } while (!key);
    return key;
}

static bool show_intro(void)
{
    int key;
    draw_intro();
    for (;;) {
        key = read_key();
        if (key == '\r' || key == '\n') return true;
        if (key == 'q' || key == 'Q' || key == 3) {
            quit_requested = true;
            clear_panel_row(27);
            write_centered(27, "Your journey can wait. Goodbye, adventurer.");
            console_flush();
            return false;
        }
        /* Ctrl-S/Left and Ctrl-D/Right cannot pause or dismiss the story. */
    }
}

/* Local line editor: BDOS 10 echoes/wraps outside a fixed command panel.
 * Ignore terminal escape sequences; Ctrl-C quits through normal cleanup.
 * Ctrl-S/Left (0x13) and Ctrl-D/Right (0x04) are ignored by the editor's
 * printable-byte filter. They must reach BDOS 6, never stdout flow control.
 */
static bool read_line(char *line)
{
    unsigned length = 0;
    uint8_t escape_state = 0;
    int key;
    line[0] = 0;
    for (;;) {
        key = read_key();
        if (key == 3) {
            quit_requested = true;
            message("Goodbye, adventurer.");
            return false;
        }
        if (escape_state == 2) {
            if (key >= 0x40 && key <= 0x7e) escape_state = 0;
            continue;
        }
        if (escape_state == 1) {
            escape_state = 0;
            if (key == '[' || key == 'O') { escape_state = 2; continue; }
        }
        if (key == 27) { escape_state = 1; continue; }
        if (key == '\r' || key == '\n') { line[length] = 0; return true; }
        if (key == 8 || key == 127) {
            if (length) {
                --length;
                cursor_move(INPUT_ROW, INPUT_COLUMN + length);
                console_put_byte(' ');
                cursor_move(INPUT_ROW, INPUT_COLUMN + length);
            }
        } else if (key == 21 || key == 24) {
            cursor_move(INPUT_ROW, INPUT_COLUMN);
            write_spaces((uint8_t)length);
            length = 0;
            cursor_move(INPUT_ROW, INPUT_COLUMN);
        } else if (key >= 32 && key <= 126 && length < LINE_SIZE) {
            console_put_byte((uint8_t)key);
            if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
            line[length++] = (char)key;
        }
    }
}

static void restore_console(void)
{
    reset_color();
    write_text("\033[?7h\033[?25h");
    cursor_move(SCREEN_HEIGHT, 1);
    write_text("\r\n");
    console_flush();
}

int main(void)
{
    char line[LINE_SIZE + 1];
    char echo[LINE_SIZE + 3];
    initialize_game();
    if (!show_intro()) {
        restore_console();
        return 0;
    }
    draw_frame();
    while (!quit_requested && !game_won) {
        draw_game();
        if (read_line(line) && *line) {
            strcpy(echo, "> ");
            strcat(echo, line);
            message(echo);
            command(line);
        }
    }
    draw_game();
    if (game_won) {
        int key;
        do { key = read_key(); } while (key != '\r' && key != '\n' && key != 3);
    }
    restore_console();
    return 0;
}
