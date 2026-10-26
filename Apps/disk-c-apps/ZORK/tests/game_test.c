#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

int bios(int function, int argument);
int bdos(int function, int argument);
#define main zork_main
#include "../zork.c"
#undef main

static unsigned char output[524288];
static unsigned output_used;
static const unsigned char *input;
static unsigned input_used;
static int diagnostics = 1, service_result = 2;
static unsigned random_value, service_calls;
static unsigned input_calls, arrival_offset, queued_count, queued_position;
static const unsigned char drawing_keys[] = {0x13, 0x04};

int bios(int function, int argument)
{
    assert(function == 4 && argument >= 0 && argument <= 255);
    assert(output_used < sizeof output);
    output[output_used++] = (unsigned char)argument;
    if (arrival_offset && output_used == arrival_offset) {
        queued_count = sizeof drawing_keys;
        arrival_offset = 0;
    }
    return 0;
}

int bdos(int function, int argument)
{
    assert(function == 6 && argument == 0xff);
    assert(console_buffer_used == 0);
    ++input_calls;
    if (queued_position < queued_count) return drawing_keys[queued_position++];
    assert(input && input[input_used]); /* Fail instead of hanging on lost input. */
    return input[input_used++];
}

int svc_error_output(int enabled)
{
    int previous = diagnostics;
    diagnostics = enabled != 0;
    return previous;
}

int svc_call_service(int group, const void *request, int length,
                     void *response, int capacity)
{
    unsigned char *bytes = response;
    assert(group == SERVICE_GROUP_UTILITY && length == 1 && capacity == 2);
    assert(*(const unsigned char *)request == 0 && !diagnostics);
    ++service_calls;
    if (service_result == 2) {
        bytes[0] = (unsigned char)random_value;
        bytes[1] = (unsigned char)(random_value >> 8);
    }
    return service_result;
}

static void reset_test(void)
{
    console_buffer_used = output_used = input_used = service_calls = 0;
    input_calls = arrival_offset = queued_count = queued_position = 0;
    input = NULL;
    diagnostics = 1;
    service_result = 2;
    random_value = 1;
    initialize_game();
}

static void issue(const char *text)
{
    char line[LINE_SIZE + 1];
    assert(strlen(text) <= LINE_SIZE);
    strcpy(line, text);
    command(line);
}

static bool log_contains(const char *text)
{
    unsigned index;
    for (index = 0; index < transcript_lines; ++index)
        if (strstr(transcript[index], text)) return true;
    return false;
}

static unsigned load_route(unsigned char *route, unsigned capacity)
{
    FILE *file = fopen("Apps/disk-c-apps/ZORK/tests/walkthrough.txt", "r");
    char line[LINE_SIZE + 2];
    unsigned used = 1, length;
    route[0] = '\r'; /* Enter past the world introduction. */
    assert(file); /* Run tests from the repository root. */
    while (fgets(line, sizeof line, file)) {
        length = (unsigned)strcspn(line, "\r\n");
        if (!length) continue;
        assert(length <= LINE_SIZE && used + length + 3 < capacity);
        memcpy(route + used, line, length); used += length;
        route[used++] = '\r';
    }
    assert(!ferror(file)); fclose(file);
    route[used++] = '\r'; route[used] = 0;
    return used;
}

static void test_complete_adventure(void)
{
    unsigned char route[4096];
    char line[LINE_SIZE + 1];
    unsigned position = 0, length, index, before;
    reset_test(); load_route(route, sizeof route);
    while (route[position]) {
        length = 0;
        while (route[position] && route[position] != '\r') line[length++] = (char)route[position++];
        if (route[position]) ++position;
        line[length] = 0;
        if (length) issue(line);
    }
    assert(game_won && room == START && score == MAX_SCORE);
    assert(treasures_home() == 3 && item_locations[GEM] == BANKED);
    assert(journal_returned && warden_friendly && dome_ready());
    for (index = 0; index < ROOM_COUNT; ++index) assert(visited[index]);
    for (index = 0; index < ITEM_COUNT; ++index) assert(collected[index]);
    assert(service_calls == 2 && diagnostics == 1);
    before = score; issue("wait"); issue("deposit all");
    assert(score == before && log_contains("YOU HAVE WON"));
}

static void test_puzzle_gates_and_rewards(void)
{
    unsigned before;
    reset_test();
    issue("n"); issue("up"); assert(room == GARDEN && !rope_tied);
    issue("e"); issue("n"); issue("take lens");
    assert(item_locations[LENS] == HIDDEN);
    issue("s"); issue("d"); issue("take journal");
    assert(!can_see() && item_locations[JOURNAL] == WELL);
    issue("u"); issue("w"); issue("s"); issue("e");
    issue("take lamp"); issue("n"); issue("take oil"); issue("e");
    issue("take bread"); issue("take battery"); issue("w"); issue("s");
    issue("e"); issue("take key"); issue("w"); issue("d");
    before = turns; issue("take coin"); assert(turns == before);
    issue("light lamp"); issue("take coin"); issue("w"); issue("take wrench");
    issue("take gear"); assert(item_locations[GEAR] == HIDDEN);
    issue("open case"); assert(case_open && item_locations[GEAR] == WORKSHOP);
    before = score; issue("open case"); issue("use wrench on case"); assert(score == before);
    issue("take gear"); issue("e"); issue("n"); issue("e"); assert(room == CRYPT);
    issue("open slab"); assert(slab_open && item_locations[COIN] == USED_UP);
    before = score; issue("open slab"); issue("use coin"); assert(score == before);
    issue("e"); issue("e"); assert(room == GALLERY);
    issue("use key on raven"); assert(!vault_open);
    issue("open door"); assert(vault_open);
    before = score; issue("use key"); issue("open door"); issue("drop key"); issue("take key");
    assert(score == before);
    issue("n"); assert(warden_state == WARDEN_ALERT);
    issue("e"); assert(room == GUARDROOM);
    issue("wait"); issue("wait"); assert(room == GALLERY);
    issue("n"); issue("use oil on warden"); assert(warden_friendly);
    before = score; issue("use oil on warden"); assert(score == before);
    issue("e"); issue("d"); issue("n"); assert(room == RESERVOIR);
    issue("w"); issue("turn valve"); assert(!valve_open);
    issue("use wrench on pump"); issue("turn valve"); assert(water_running());
    before = score; issue("repair pump"); issue("turn valve"); assert(score == before);
    issue("e"); issue("n"); issue("take chalice"); assert(carried(CHALICE));
    issue("put chalice in cabinet"); assert(carried(CHALICE));
    issue("s"); issue("w"); issue("s"); issue("w"); issue("take journal");
    issue("u"); issue("n"); issue("give bread to raven"); issue("take lens");
    assert(raven_fed && carried(LENS) && item_locations[BREAD] == USED_UP);
    before = score; issue("feed raven"); assert(score == before);
    issue("s"); issue("w"); issue("take rope"); issue("use rope"); issue("u");
    issue("u"); assert(room == TOWER);
    issue("wind clock"); assert(!clock_running);
    issue("use gear on clock"); issue("wind clock"); assert(!clock_running);
    issue("use oil on clock"); issue("wind clock"); assert(clock_running);
    issue("u"); assert(room == TOWER);
    issue("use lens on mount"); issue("u"); assert(room == OBSERVATORY);
    issue("take crown"); assert(item_locations[CROWN] == HIDDEN);
    issue("say midnight"); assert(!crown_open);
    issue("say dawn"); assert(crown_open);
    before = score; issue("say dawn"); assert(score == before);
    issue("take crown"); assert(carried(CROWN));
}

static void test_warden_and_ghost(void)
{
    unsigned before;
    reset_test();
    room = ARCHIVE; item_locations[JOURNAL] = CARRIED;
    issue("talk to ghost"); assert(!journal_returned);
    issue("give journal to raven"); assert(!journal_returned);
    issue("give journal to ghost"); assert(journal_returned && item_locations[FLUTE] == ARCHIVE);
    before = score; issue("give journal to ghost"); assert(score == before);
    issue("take flute"); room = GUARDROOM;
    issue("wait"); assert(warden_state == WARDEN_ALERT);
    issue("play flute"); assert(warden_state == WARDEN_ASLEEP && warden_ticks == 3);
    issue("e"); assert(room == BRIDGE && warden_ticks == 2);
    issue("wait"); assert(warden_ticks == 1);
    issue("wait"); assert(warden_state == WARDEN_PATROL);
    issue("w"); issue("s"); assert(room == GALLERY); /* Retreat stays possible. */
}

static void test_treasure_cabinet(void)
{
    unsigned before;
    reset_test();
    item_locations[IDOL] = CARRIED;
    issue("put jade idol in the cabinet");
    assert(treasures_home() == 1 && item_locations[IDOL] == BANKED && !game_won);
    assert(strstr(current_description(), "glow"));
    before = score;
    issue("deposit idol"); issue("take idol");
    assert(score == before && item_locations[IDOL] == BANKED);
    item_locations[CHALICE] = item_locations[CROWN] = CARRIED;
    assert(!game_won); /* Possession alone does not complete the quest. */
    issue("deposit all");
    assert(game_won && treasures_home() == 3);
    assert(strstr(current_description(), "Dawn"));
}

static void test_parser_and_lamp(void)
{
    static const char *invalid[] = {
        "northbound", "eastward", "southpaw", "what", "lightning", "inside",
        "quitters", "scoreboard", "take monkey", "use skeletonkey", "nonsense",
        "take lamp on warden", "say dawn on case"
    };
    unsigned index, before, fuel;
    reset_test();
    for (index = 0; index < sizeof invalid / sizeof invalid[0]; ++index)
        issue(invalid[index]);
    assert(room == START && turns == 0 && score == 1 && !quit_requested);
    issue("e"); issue("get lamp"); issue("light");
    assert(lamp_on && lamp_fuel == LAMP_LIFE - 1);
    before = turns; fuel = lamp_fuel;
    issue("light lamp"); issue("look"); issue("i"); issue("help");
    assert(turns == before && lamp_fuel == fuel);
    issue("extinguish lamp");
    assert(!lamp_on && room == HALL && lamp_fuel == fuel);
    issue("light lamp"); issue("d"); issue("drop lamp");
    assert(can_see()); /* A lit lamp on the floor illuminates its own room. */
    issue("n");
    assert(!can_see()); /* A lamp in another room does not. */
    before = turns;
    issue("light lamp"); issue("use coin"); issue("take coin");
    assert(turns == before && !can_see());
    issue("s"); issue("take lamp"); issue("extinguish lamp");
    assert(!can_see());
    before = turns;
    issue("take coin");
    assert(turns == before && item_locations[COIN] == CELLAR);
    issue("light lamp");
    while (lamp_fuel) issue("wait");
    assert(!lamp_on && !can_see());
    issue("light lamp"); issue("use lamp");
    assert(!lamp_on && lamp_fuel == 0);
    item_locations[BATTERY] = CARRIED;
    issue("use battery");
    assert(lamp_fuel == BATTERY_CHARGE && !lamp_on && item_locations[BATTERY] == USED_UP);
    before = lamp_fuel; issue("use battery"); assert(lamp_fuel == before);
    issue("u");
    assert(room == HALL && can_see()); /* Exhaustion never traps the player. */
    issue("restart");
    assert(room == START && lamp_fuel == LAMP_LIFE && score == 1 && !lamp_on);
    for (index = 0; index < ITEM_COUNT; ++index) assert(!collected[index]);
}

static void test_optional_service(void)
{
    reset_test();
    ambient_message();
    assert(service_calls == 0);
    room = GALLERY;
    service_result = -2;
    ambient_message();
    assert(service_calls == 1 && diagnostics == 1 && !quit_requested);
    diagnostics = 0;
    service_result = 0; /* A short reply is not decoded. */
    ambient_message();
    assert(diagnostics == 0 && !log_contains("portrait's eyes"));
    service_result = 2;
    random_value = 256;
    ambient_message();
    assert(!log_contains("portrait's eyes"));
    random_value = 19;
    ambient_message();
    assert(log_contains("portrait's eyes") && diagnostics == 0);
}

static void test_console_and_editor(void)
{
    char line[LINE_SIZE + 1];
    unsigned index;
    static unsigned char long_input[LINE_SIZE + 25];
    reset_test();
    for (index = 0; index < CONSOLE_BUFFER_SIZE + 256; ++index)
        console_put_byte((uint8_t)index);
    console_flush();
    assert(output_used == CONSOLE_BUFFER_SIZE + 256);
    for (index = 0; index < output_used; ++index)
        assert(output[index] == (unsigned char)index);
    output_used = 0;
    cursor_move(INPUT_ROW, INPUT_COLUMN);
    input = (const unsigned char *)"TAKE X\b\177LAMP\033[D\r";
    assert(read_line(line) && !strcmp(line, "takelamp"));
    input_used = 0;
    input = (const unsigned char *)"discard\025USE LAMP\030LOOK\r";
    assert(read_line(line) && !strcmp(line, "look"));
    input_used = 0;
    input = (const unsigned char *)"\023LO\004OK\r";
    assert(read_line(line) && !strcmp(line, "look"));
    input_used = 0;
    memset(long_input, 'A', sizeof long_input);
    long_input[sizeof long_input - 2] = '\r';
    long_input[sizeof long_input - 1] = 0;
    input = long_input;
    assert(read_line(line) && strlen(line) == LINE_SIZE);
    for (index = 0; index < LINE_SIZE; ++index) assert(line[index] == 'a');
    input_used = 0;
    input = (const unsigned char *)"\003";
    assert(!read_line(line) && quit_requested && console_buffer_used == 0);
}

/* Minimal VT100 screen model. Validate border cells and cursor limits instead
 * of snapshotting implementation-specific escape sequences or spacing.
 */
static char screen[SCREEN_HEIGHT][SCREEN_WIDTH];
static unsigned char backgrounds[SCREEN_HEIGHT][SCREEN_WIDTH];
static unsigned terminal_row, terminal_column, background;
static bool wrapping, cursor_visible;
static unsigned scrolls;

static void model_screen(void)
{
    unsigned index = 0;
    terminal_row = terminal_column = background = scrolls = 0;
    wrapping = cursor_visible = true;
    memset(screen, ' ', sizeof screen);
    memset(backgrounds, 0, sizeof backgrounds);
    while (index < output_used) {
        unsigned char byte = output[index++];
        if (byte == 27) {
            unsigned values[4] = {0}, count = 0;
            bool private_mode = false;
            unsigned char final;
            assert(index < output_used && output[index++] == '[');
            if (output[index] == '?') { private_mode = true; ++index; }
            while (index < output_used) {
                byte = output[index];
                if (byte >= '0' && byte <= '9') values[count] = values[count] * 10 + byte - '0';
                else if (byte == ';') { ++count; assert(count < 4); }
                else break;
                ++index;
            }
            assert(index < output_used);
            final = output[index++];
            if (final == 'H') {
                if (!values[0]) values[0] = 1;
                if (!values[1]) values[1] = 1;
                assert(values[0] >= 1 && values[0] <= SCREEN_HEIGHT);
                assert(values[1] >= 1 && values[1] <= SCREEN_WIDTH);
                terminal_row = values[0] - 1;
                terminal_column = values[1] - 1;
            } else if (final == 'J') {
                assert(values[0] == 2);
                memset(screen, ' ', sizeof screen);
                memset(backgrounds, 0, sizeof backgrounds);
            } else if (final == 'm') {
                if (values[0] == 0) background = 0;
                else if (values[0] >= 100) background = values[0];
            } else if ((final == 'h' || final == 'l') && private_mode) {
                if (values[0] == 7) wrapping = final == 'h';
                else { assert(values[0] == 25); cursor_visible = final == 'h'; }
            } else assert(!"Unexpected terminal sequence");
        } else if (byte == '\r') terminal_column = 0;
        else if (byte == '\n') {
            if (terminal_row == SCREEN_HEIGHT - 1) ++scrolls;
            else ++terminal_row;
        } else {
            assert(byte >= 32 && byte <= 126);
            assert(terminal_row < SCREEN_HEIGHT && terminal_column < SCREEN_WIDTH);
            screen[terminal_row][terminal_column] = (char)byte;
            backgrounds[terminal_row][terminal_column] = (unsigned char)background;
            if (terminal_column < SCREEN_WIDTH - 1) ++terminal_column;
            else assert(!wrapping); /* Painting the corner must not wrap/scroll. */
        }
    }
}

static void assert_cabinet_border(void)
{
    unsigned row, column;
    for (row = 1; row < SCREEN_HEIGHT; ++row) {
        for (column = 0; column < SCREEN_WIDTH; ++column) {
            if (row == 1 || row == SCREEN_HEIGHT - 1 || column < 2 || column >= 78) {
                assert(screen[row][column] == ' ');
                assert(backgrounds[row][column] == 100 || backgrounds[row][column] == 107);
            }
        }
    }
    assert(!scrolls && !wrapping);
}

static void assert_border(void)
{
    assert_cabinet_border();
    assert(cursor_visible);
    assert(terminal_row == INPUT_ROW - 1 && terminal_column == INPUT_COLUMN - 1);
}

static void test_intro(void)
{
    unsigned paragraph, lines;
    const char *text;
    char line[TEXT_WIDTH + 1];
    reset_test();
    for (paragraph = 0; paragraph < 4; ++paragraph) {
        text = intro_paragraphs[paragraph]; lines = 0;
        while (*text) { wrap_line(&text, line); ++lines; }
        assert(lines > 0 && lines <= 3);
    }
    draw_intro();
    assert(input_calls == 0 && console_buffer_used == 0);
    model_screen(); assert_cabinet_border();
    assert(!cursor_visible);
    assert(!memcmp(screen[7] + TEXT_COLUMN - 1, "On a forgotten coast", 20));
    assert(!memcmp(screen[16] + TEXT_COLUMN - 1, "Recover the jade idol", 21));

    reset_test();
    arrival_offset = 100; /* Left/Right arrive while the story is drawing. */
    input = (const unsigned char *)"\r";
    assert(show_intro());
    assert(queued_position == 2 && input_used == 1 && input_calls == 3);
    assert(turns == 0 && score == 1 && lamp_fuel == LAMP_LIFE);
    assert(!quit_requested && room == START);
    model_screen(); assert_cabinet_border();

    reset_test();
    input = (const unsigned char *)"q";
    assert(zork_main() == 0 && quit_requested && turns == 0);
    assert(input_used == 1 && !console_buffer_used);
    model_screen();
    assert(wrapping && cursor_visible && scrolls == 1);
}

static void test_keys_during_drawing(void)
{
    static const unsigned offsets[] = {
        1, CONSOLE_BUFFER_SIZE, CONSOLE_BUFFER_SIZE + 1
    };
    unsigned index;
    char line[LINE_SIZE + 1];
    for (index = 0; index < sizeof offsets / sizeof offsets[0]; ++index) {
        reset_test();
        arrival_offset = offsets[index];
        draw_frame();
        draw_game();
        assert(output_used > CONSOLE_BUFFER_SIZE + 1);
        assert(!arrival_offset && queued_count == 2 && queued_position == 0);
        assert(input_calls == 0); /* Drawing cannot consume queued keys. */
        model_screen();
        assert_border();
        assert(read_key() == 0x13); /* Left/Ctrl-S reaches direct input. */
        assert(read_key() == 0x04); /* Right/Ctrl-D stays in order. */
        assert(queued_position == 2 && input_used == 0);

        /* Also inject while redrawing after startup and let the real editor
         * consume the controls before the following command. */
        queued_count = queued_position = 0;
        arrival_offset = output_used + 100;
        draw_game();
        assert(!arrival_offset && queued_count == 2 && queued_position == 0);
        input = (const unsigned char *)"LOOK\r";
        assert(read_line(line) && !strcmp(line, "look"));
        assert(queued_position == 2 && input_used == 5 && !quit_requested);
        issue(line);
        assert(room == START && turns == 0);
    }
}

static void test_layout_and_main(void)
{
    unsigned index, mode, lines;
    int direction, destination;
    static const int opposite[DIRECTION_COUNT] = { SOUTH, WEST, NORTH, EAST, DOWN, UP };
    const char *description;
    char wrapped[TEXT_WIDTH + 1];
    unsigned char route[4096];
    unsigned route_length = load_route(route, sizeof route);
    reset_test();
    draw_frame(); draw_game();
    model_screen(); assert_border();
    for (index = 0; index < 30; ++index)
        message("A deliberately long adventure message checks that prose wraps inside the panel without repainting the checker border.");
    for (index = 0; index < transcript_lines; ++index)
        assert(strlen(transcript[index]) <= TEXT_WIDTH);
    help(); draw_game();
    model_screen(); assert_border();
    issue("e"); issue("take lamp"); issue("d"); draw_game();
    model_screen(); assert_border();
    assert(!memcmp(screen[3] + 3, "Darkness", 8));
    /* The larger world must remain navigable and all descriptions must fit
     * in three lines, both before and after their mechanisms are restored. */
    lamp_on = true; item_locations[LAMP] = CARRIED;
    for (mode = 0; mode < 2; ++mode) {
        if (mode) {
            slab_open = vault_open = rope_tied = case_open = clock_running = true;
            raven_fed = pump_fixed = valve_open = journal_returned = true;
            lens_fitted = crown_open = true;
        }
        for (index = 0; index < ROOM_COUNT; ++index) {
            room = (enum room_id)index;
            description = current_description(); lines = 0;
            while (*description) { wrap_line(&description, wrapped); ++lines; }
            assert(lines <= 3);
            for (direction = 0; direction < DIRECTION_COUNT; ++direction) {
                destination = room_exits[index][direction];
                if (destination >= 0) {
                    assert(destination < ROOM_COUNT);
                    assert(room_exits[destination][opposite[direction]] == (int)index);
                }
            }
            draw_game();
        }
    }
    /* All sixteen dropped objects must fit the two-line object panel. */
    room = HALL;
    for (index = 0; index < ITEM_COUNT; ++index) item_locations[index] = HALL;
    draw_game();
    model_screen(); assert_border();

    reset_test();
    input = (const unsigned char *)"\rQUIT\r";
    assert(zork_main() == 0 && quit_requested);
    model_screen();
    assert(wrapping && cursor_visible && scrolls == 1 && !console_buffer_used);
    reset_test();
    input = (const unsigned char *)"\003";
    assert(zork_main() == 0 && quit_requested);
    model_screen();
    assert(wrapping && cursor_visible && scrolls == 1 && !console_buffer_used);
    reset_test();
    input = (const unsigned char *)"\r\003";
    assert(zork_main() == 0 && quit_requested);
    model_screen();
    assert(wrapping && cursor_visible && scrolls == 1 && !console_buffer_used);
    reset_test();
    service_result = -2; /* A full game also works without host services. */
    input = route;
    assert(zork_main() == 0 && game_won && score == MAX_SCORE);
    assert(input_used == route_length && service_calls == 2);
    model_screen();
    assert(wrapping && cursor_visible && scrolls == 1 && !console_buffer_used);
}

int main(void)
{
    test_complete_adventure();
    test_puzzle_gates_and_rewards();
    test_warden_and_ghost();
    test_treasure_cabinet();
    test_parser_and_lamp();
    test_optional_service();
    test_console_and_editor();
    test_intro();
    test_keys_during_drawing();
    test_layout_and_main();
    puts("ZORK 24-room quest, puzzle gates, NPC states, parser, lamp, Ctrl-S and terminal layout PASS");
    return 0;
}
