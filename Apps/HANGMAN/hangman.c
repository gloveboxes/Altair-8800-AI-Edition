#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* HANGMAN.C - C11 Hangman for CP/M 2.2 on the Z80, built with dcc. */

#define WORD_COUNT 100
#define MAX_WRONG 6
#define CTRL_C 3
#define ESCAPE_KEY 27
#define BORDER_LEFT 2
#define BORDER_RIGHT 78
#define BORDER_TOP 3
#define BORDER_BOTTOM 23
#define FIELD_WIDTH 68

extern int bdos(int function, int value);
extern int inp(unsigned port);
extern void outp(unsigned port, unsigned value);

static const char *const words[WORD_COUNT] = {
    "ALTAIR", "ASSEMBLER", "BASIC", "BINARY",
    "BOOTLOADER", "BYTE", "COMPILER", "COMPUTER",
    "CONSOLE", "DEBUGGER", "DIRECTORY", "DISKETTE",
    "EMULATOR", "FIRMWARE", "FLOPPY", "HARDWARE",
    "INTERRUPT", "KEYBOARD", "MEMORY", "MICROCHIP",
    "MODEM", "MONITOR", "OPERATING", "PROCESSOR",
    "PROGRAM", "REGISTER", "SECTOR", "SOFTWARE",
    "TERMINAL", "TRANSISTOR", "VARIABLE", "ZILOG",
    "ALGORITHM", "ARCHIVE", "BITMAP", "BUFFER",
    "CACHE", "CIRCUIT", "COMMAND", "CURSOR",
    "DATABASE", "DECODER", "DESKTOP", "DEVICE",
    "DIGITAL", "DISPLAY", "DRIVER", "EDITOR",
    "ETHERNET", "FILE", "GRAPHICS", "INPUT",
    "INTEGER", "INTERFACE", "JOYSTICK", "KERNEL",
    "LANGUAGE", "MACHINE", "NETWORK", "NIBBLE",
    "OPCODE", "OUTPUT", "PACKET", "PIXEL",
    "POINTER", "PRINTER", "PROTOCOL", "QUEUE",
    "ROUTINE", "SCANNER", "SCREEN", "SERIAL",
    "SERVER", "SOURCE", "STORAGE", "SYNTAX",
    "SYSTEM", "TIMER", "UTILITY", "VECTOR",
    "VIRTUAL", "VOLTAGE", "WEBSITE", "WIRELESS",
    "WORKSTATION", "ZEPHYR", "ADDRESS", "BOOLEAN",
    "CARTRIDGE", "CHECKSUM", "CONTROLLER", "ENCRYPTION",
    "FUNCTION", "INSTRUCTION", "MICROCODE", "MOTHERBOARD",
    "PERIPHERAL", "REPOSITORY", "SEMICONDUCTOR", "SUBROUTINE"
};

static const char *const gallows[MAX_WRONG + 1] = {
    "  +---+\n  |   |\n      |\n      |\n      |\n      |\n=========",
    "  +---+\n  |   |\n  O   |\n      |\n      |\n      |\n=========",
    "  +---+\n  |   |\n  O   |\n  |   |\n      |\n      |\n=========",
    "  +---+\n  |   |\n  O   |\n /|   |\n      |\n      |\n=========",
    "  +---+\n  |   |\n  O   |\n /|\\  |\n      |\n      |\n=========",
    "  +---+\n  |   |\n  O   |\n /|\\  |\n /    |\n      |\n=========",
    "  +---+\n  |   |\n  O   |\n /|\\  |\n / \\  |\n      |\n========="
};

static void write_text(const char *text)
{
    while (*text)
        putchar(*text++);
}

static void write_unsigned(uint8_t value)
{
    char digits[3];
    uint8_t count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0)
        putchar(digits[--count]);
}

static void move_cursor(uint8_t row, uint8_t column)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    write_unsigned(row);
    putchar(';');
    write_unsigned(column);
    putchar('H');
}

static void set_color(uint8_t color)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    write_unsigned(color);
    putchar('m');
}

static void reset_color(void)
{
    write_text("\033[0m");
}

static void clear_screen(void)
{
    write_text("\033[0m\033[2J\033[H");
}

static void hide_cursor(void)
{
    write_text("\033[?25l");
}

static void show_cursor(void)
{
    write_text("\033[?25h");
}

static void clear_field(uint8_t row, uint8_t column, uint8_t width)
{
    move_cursor(row, column);
    while (width-- != 0)
        putchar(' ');
    move_cursor(row, column);
}

static uint8_t cabinet_background(uint8_t row, uint8_t column)
{
    return (((row / 2) + (column / 2)) & 1) != 0 ? 100 : 107;
}

static void draw_frame(void)
{
    clear_screen();
    hide_cursor();

    move_cursor(1, 2);
    set_color(36);
    write_text("HANGMAN");
    reset_color();
    write_text(" for ");
    set_color(33);
    write_text("Altair 8800");
    reset_color();
    write_text(" C11");

    move_cursor(2, 2);
    write_text("A-Z Guess a letter    ESC, Q or Ctrl-C Quit");

    for (uint8_t row = BORDER_TOP; row <= BORDER_BOTTOM; ++row) {
        if (row < BORDER_TOP + 2 || row > BORDER_BOTTOM - 2) {
            move_cursor(row, BORDER_LEFT);
            for (uint8_t column = BORDER_LEFT; column <= BORDER_RIGHT;
                 ++column) {
                set_color(cabinet_background(
                    (uint8_t)(row - BORDER_TOP),
                    (uint8_t)(column - BORDER_LEFT)));
                putchar(' ');
            }
            continue;
        }
        move_cursor(row, BORDER_LEFT);
        for (uint8_t column = BORDER_LEFT; column < BORDER_LEFT + 2; ++column) {
            set_color(cabinet_background(
                (uint8_t)(row - BORDER_TOP),
                (uint8_t)(column - BORDER_LEFT)));
            putchar(' ');
        }
        move_cursor(row, BORDER_RIGHT - 1);
        for (uint8_t column = BORDER_RIGHT - 1; column <= BORDER_RIGHT;
             ++column) {
            set_color(cabinet_background(
                (uint8_t)(row - BORDER_TOP),
                (uint8_t)(column - BORDER_LEFT)));
            putchar(' ');
        }
    }
    reset_color();
}

static int read_key(void)
{
    int key;

    fflush(stdout);
    do {
        key = bdos(6, 0xff) & 0xff;
    } while (key == 0);
    return key;
}

static int upper_letter(int key)
{
    if (key >= 'a' && key <= 'z')
        key -= 'a' - 'A';
    return key;
}

static uint16_t random_word(void)
{
    uint16_t value;

    outp(45, 1);
    value = (uint16_t)inp(200);
    value |= (uint16_t)(inp(200) << 8);
    return value;
}

static bool word_complete(const char *word, const bool guessed[])
{
    while (*word) {
        if (!guessed[*word - 'A'])
            return false;
        ++word;
    }
    return true;
}

static void show_word(const char *word, const bool guessed[], bool reveal)
{
    while (*word) {
        if (reveal || guessed[*word - 'A'])
            putchar(*word);
        else
            putchar('_');
        putchar(' ');
        ++word;
    }
}

static void show_guesses(const bool guessed[])
{
    for (int letter = 0; letter < 26; ++letter) {
        if (guessed[letter]) {
            putchar('A' + letter);
            putchar(' ');
        }
    }
}

static void draw_gallows(int wrong)
{
    const char *line = gallows[wrong];

    for (uint8_t row = 0; row < 7; ++row) {
        clear_field((uint8_t)(6 + row), 8, 12);
        while (*line != '\0' && *line != '\n')
            putchar(*line++);
        if (*line == '\n')
            ++line;
    }
}

static void draw_game(const char *word, const bool guessed[], int wrong,
                      const char *message)
{
    draw_gallows(wrong);

    clear_field(6, 28, 45);
    set_color(33);
    write_text("WORD");
    reset_color();
    clear_field(8, 28, 45);
    show_word(word, guessed, false);

    clear_field(11, 28, 45);
    write_text("Wrong guesses remaining: ");
    write_unsigned((uint8_t)(MAX_WRONG - wrong));

    clear_field(13, 28, 45);
    write_text("Guessed: ");
    show_guesses(guessed);

    clear_field(17, 6, FIELD_WIDTH);
    set_color(35);
    write_text(message);
    reset_color();
    clear_field(20, 6, FIELD_WIDTH);
    write_text("> ");
    fflush(stdout);
}

static void draw_result(const char *word, const bool guessed[], int wrong,
                        bool won)
{
    draw_gallows(wrong);
    clear_field(8, 28, 45);
    show_word(word, guessed, true);
    clear_field(17, 6, FIELD_WIDTH);
    set_color(won ? 32 : 31);
    if (won) {
        write_text("YOU WON with ");
        write_unsigned((uint8_t)wrong);
        write_text(wrong == 1 ? " wrong guess!" : " wrong guesses!");
    } else {
        write_text("YOU LOST. THE WORD WAS: ");
        write_text(word);
    }
    reset_color();
}

static bool play_game(void)
{
    bool guessed[26];

    memset(guessed, 0, sizeof(guessed));
    const char *word = words[random_word() % WORD_COUNT];
    int wrong = 0;
    const char *message = "Guess a letter, or press ESC to quit.";

    for (;;) {
        draw_game(word, guessed, wrong, message);

        int key = upper_letter(read_key());
        if (key == ESCAPE_KEY || key == CTRL_C || key == 'Q')
            return false;
        if (key < 'A' || key > 'Z') {
            message = "Please enter A-Z, or press ESC to quit.";
            continue;
        }

        putchar(key);
        int index = key - 'A';
        if (guessed[index]) {
            message = "You already guessed that letter.";
            continue;
        }

        guessed[index] = true;
        if (strchr(word, key) != NULL)
            message = "Good guess!";
        else {
            ++wrong;
            message = "That letter is not in the word.";
        }

        if (word_complete(word, guessed)) {
            draw_result(word, guessed, wrong, true);
            return true;
        }
        if (wrong == MAX_WRONG) {
            draw_result(word, guessed, wrong, false);
            return true;
        }
    }
}

int main(void)
{
    draw_frame();

    for (;;) {
        if (!play_game())
            break;
        clear_field(20, 6, FIELD_WIDTH);
        write_text("Play again (Y/N, or ESC)? ");
        int key;
        do {
            key = upper_letter(read_key());
        } while (key != 'Y' && key != 'N' && key != ESCAPE_KEY &&
                 key != CTRL_C);
        if (key != ESCAPE_KEY && key != CTRL_C)
            putchar(key);
        if (key != 'Y')
            break;
    }

    clear_field(20, 6, FIELD_WIDTH);
    set_color(33);
    write_text("Thanks for playing Hangman!");
    reset_color();
    move_cursor(24, 1);
    show_cursor();
    write_text("\r\n");
    fflush(stdout);
    return 0;
}