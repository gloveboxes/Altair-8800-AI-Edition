#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_LINE 80

extern int bdos(int function, int argument);

struct Value {
    int is_float;
    int int_value;
    float float_value;
};

struct Parser {
    char *text;
    int error;
    char error_message[40];
};

static void parse_expression(struct Parser *parser, struct Value *result);
static void parse_unary(struct Parser *parser, struct Value *result);

static int is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static int is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *skip_spaces(char *text)
{
    while (*text != '\0' && is_space(*text)) {
        ++text;
    }
    return text;
}

static int same_command(char *text, const char *command)
{
    text = skip_spaces(text);
    while (*text != '\0' && *command != '\0') {
        if (*text != *command) {
            return 0;
        }
        ++text;
        ++command;
    }
    if (*command != '\0') {
        return 0;
    }
    text = skip_spaces(text);
    return *text == '\0';
}

static int parse_int(const char *text)
{
    int sign;
    int value;

    sign = 1;
    value = 0;
    if (*text == '-') {
        sign = -1;
        ++text;
    } else if (*text == '+') {
        ++text;
    }

    while (*text >= '0' && *text <= '9') {
        value = (value * 10) + (*text - '0');
        ++text;
    }
    return value * sign;
}

static void set_int(struct Value *value, int int_value)
{
    value->is_float = 0;
    value->int_value = int_value;
    value->float_value = (float)int_value;
}

static void set_float(struct Value *value, float float_value)
{
    value->is_float = 1;
    value->int_value = (int)float_value;
    value->float_value = float_value;
}

static float value_as_float(const struct Value *value)
{
    if (value->is_float) {
        return value->float_value;
    }
    return (float)value->int_value;
}

static void parser_error(struct Parser *parser, const char *message)
{
    if (parser->error == 0) {
        strncpy(parser->error_message, message, sizeof(parser->error_message) - 1);
        parser->error_message[sizeof(parser->error_message) - 1] = '\0';
    }
    parser->error = 1;
}

static void token_add(struct Parser *parser, char *token, int *index, char ch)
{
    if (*index >= 31) {
        parser_error(parser, "Number too long");
        return;
    }
    token[*index] = ch;
    *index = *index + 1;
}

static void parse_number(struct Parser *parser, struct Value *result)
{
    char token[32];
    int index;
    int saw_digit;
    int is_float;

    parser->text = skip_spaces(parser->text);
    index = 0;
    saw_digit = 0;
    is_float = 0;

    while (is_digit(*parser->text) && parser->error == 0) {
        saw_digit = 1;
        token_add(parser, token, &index, *parser->text);
        ++parser->text;
    }

    if (*parser->text == '.' && parser->error == 0) {
        is_float = 1;
        token_add(parser, token, &index, *parser->text);
        ++parser->text;
        while (is_digit(*parser->text) && parser->error == 0) {
            saw_digit = 1;
            token_add(parser, token, &index, *parser->text);
            ++parser->text;
        }
    }

    if ((*parser->text == 'e' || *parser->text == 'E') && parser->error == 0) {
        is_float = 1;
        token_add(parser, token, &index, *parser->text);
        ++parser->text;
        if (*parser->text == '+' || *parser->text == '-') {
            token_add(parser, token, &index, *parser->text);
            ++parser->text;
        }
        while (is_digit(*parser->text) && parser->error == 0) {
            saw_digit = 1;
            token_add(parser, token, &index, *parser->text);
            ++parser->text;
        }
    }

    if (parser->error != 0) {
        set_int(result, 0);
        return;
    }
    if (saw_digit == 0) {
        parser_error(parser, "Expected number");
        set_int(result, 0);
        return;
    }

    token[index] = '\0';
    if (is_float) {
        set_float(result, atof(token));
    } else {
        set_int(result, parse_int(token));
    }
}

static void parse_primary(struct Parser *parser, struct Value *result)
{
    parser->text = skip_spaces(parser->text);
    if (*parser->text == '(') {
        ++parser->text;
        parse_expression(parser, result);
        parser->text = skip_spaces(parser->text);
        if (*parser->text != ')' && parser->error == 0) {
            parser_error(parser, "Expected )");
            set_int(result, 0);
            return;
        }
        if (*parser->text == ')') {
            ++parser->text;
        }
        return;
    }

    parse_number(parser, result);
}

static void parse_power(struct Parser *parser, struct Value *result)
{
    struct Value exponent;
    int count;
    int product;

    parse_primary(parser, result);
    parser->text = skip_spaces(parser->text);
    if (*parser->text != '^' || parser->error != 0) {
        return;
    }

    ++parser->text;
    parse_unary(parser, &exponent);
    if (parser->error != 0) {
        return;
    }
    if (!result->is_float && !exponent.is_float && exponent.int_value >= 0) {
        product = 1;
        for (count = 0; count < exponent.int_value; ++count) {
            product *= result->int_value;
        }
        set_int(result, product);
        return;
    }
    if (value_as_float(result) == 0.0f && value_as_float(&exponent) < 0.0f) {
        parser_error(parser, "Division by zero");
        set_int(result, 0);
        return;
    }
    set_float(result, powf(value_as_float(result), value_as_float(&exponent)));
}

static void parse_unary(struct Parser *parser, struct Value *result)
{
    parser->text = skip_spaces(parser->text);
    if (*parser->text == '+') {
        ++parser->text;
        parse_unary(parser, result);
        return;
    }
    if (*parser->text == '-') {
        ++parser->text;
        parse_unary(parser, result);
        if (result->is_float) {
            result->float_value = -result->float_value;
            result->int_value = (int)result->float_value;
        } else {
            result->int_value = -result->int_value;
            result->float_value = (float)result->int_value;
        }
        return;
    }
    if (*parser->text == '~') {
        ++parser->text;
        parse_unary(parser, result);
        if (result->is_float) {
            parser_error(parser, "~ is for integers only");
            set_int(result, 0);
        } else {
            set_int(result, ~result->int_value);
        }
        return;
    }

    parse_power(parser, result);
}

static void parse_multiply(struct Parser *parser, struct Value *result)
{
    struct Value right;
    char op;
    int integer_divide;
    float left_value;
    float right_value;

    parse_unary(parser, result);
    while (parser->error == 0) {
        parser->text = skip_spaces(parser->text);
        op = *parser->text;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++parser->text;
        integer_divide = 0;
        if (op == '/' && *parser->text == '/') {
            integer_divide = 1;
            ++parser->text;
        }
        parse_unary(parser, &right);

        if (integer_divide) {
            if (result->is_float || right.is_float) {
                parser_error(parser, "// is for integers only");
                set_int(result, 0);
                return;
            }
            if (right.int_value == 0) {
                parser_error(parser, "Division by zero");
                set_int(result, 0);
                return;
            }
            set_int(result, result->int_value / right.int_value);
        } else if (op == '%') {
            if (result->is_float || right.is_float) {
                parser_error(parser, "% is for integers only");
                set_int(result, 0);
                return;
            }
            if (right.int_value == 0) {
                parser_error(parser, "Division by zero");
                set_int(result, 0);
                return;
            }
            set_int(result, result->int_value % right.int_value);
        } else if (result->is_float || right.is_float) {
            left_value = value_as_float(result);
            right_value = value_as_float(&right);
            if (op == '*') {
                set_float(result, left_value * right_value);
            } else {
                if (right_value == 0.0f) {
                    parser_error(parser, "Division by zero");
                    set_int(result, 0);
                    return;
                }
                set_float(result, left_value / right_value);
            }
        } else if (op == '*') {
            set_int(result, result->int_value * right.int_value);
        } else {
            if (right.int_value == 0) {
                parser_error(parser, "Division by zero");
                set_int(result, 0);
                return;
            }
            set_int(result, result->int_value / right.int_value);
        }
    }
}

static void parse_add(struct Parser *parser, struct Value *result)
{
    struct Value right;
    char op;
    float left_value;
    float right_value;

    parse_multiply(parser, result);
    while (parser->error == 0) {
        parser->text = skip_spaces(parser->text);
        op = *parser->text;
        if (op != '+' && op != '-') {
            break;
        }
        ++parser->text;
        parse_multiply(parser, &right);

        if (result->is_float || right.is_float) {
            left_value = value_as_float(result);
            right_value = value_as_float(&right);
            if (op == '+') {
                set_float(result, left_value + right_value);
            } else {
                set_float(result, left_value - right_value);
            }
        } else if (op == '+') {
            set_int(result, result->int_value + right.int_value);
        } else {
            set_int(result, result->int_value - right.int_value);
        }
    }
}

static void parse_expression(struct Parser *parser, struct Value *result)
{
    struct Value right;
    float left_value;
    float right_value;

    parse_add(parser, result);
    while (parser->error == 0) {
        parser->text = skip_spaces(parser->text);
        if (*parser->text != '=') {
            break;
        }
        ++parser->text;
        parse_add(parser, &right);
        if (result->is_float || right.is_float) {
            left_value = value_as_float(result);
            right_value = value_as_float(&right);
            set_int(result, left_value == right_value);
        } else {
            set_int(result, result->int_value == right.int_value);
        }
    }
}

static void format_float(char *outbuf, float value)
{
    float magnitude;
    int precision;
    int length;
    char format[5];

    magnitude = value < 0.0f ? -value : value;
    if (magnitude >= 100000.0f) precision = 0;
    else if (magnitude >= 10000.0f) precision = 1;
    else if (magnitude >= 1000.0f) precision = 2;
    else if (magnitude >= 100.0f) precision = 3;
    else if (magnitude >= 10.0f) precision = 4;
    else if (magnitude >= 1.0f) precision = 5;
    else precision = 6;

    format[0] = '%';
    format[1] = '.';
    format[2] = (char)('0' + precision);
    format[3] = 'f';
    format[4] = '\0';
    sprintf(outbuf, format, value);

    length = strlen(outbuf);
    while (length > 0 && outbuf[length - 1] == '0') {
        outbuf[--length] = '\0';
    }
    if (length > 0 && outbuf[length - 1] == '.') {
        outbuf[--length] = '\0';
    }
    if (strcmp(outbuf, "-0") == 0) {
        outbuf[0] = '0';
        outbuf[1] = '\0';
    }
}

static int evaluate_expression(char *line, char *outbuf, int outsize)
{
    struct Parser parser;
    struct Value result;

    parser.text = line;
    parser.error = 0;
    parser.error_message[0] = '\0';
    parse_expression(&parser, &result);
    parser.text = skip_spaces(parser.text);
    if (parser.error == 0 && *parser.text != '\0') {
        parser_error(&parser, "Unexpected input");
    }
    if (parser.error != 0) {
        if (outbuf != NULL && outsize > 0) {
            strncpy(outbuf, parser.error_message, outsize - 1);
            outbuf[outsize - 1] = '\0';
        }
        return 1;
    }

    if (outbuf != NULL && outsize > 0) {
        if (result.is_float) {
            format_float(outbuf, result.float_value);
        } else {
            sprintf(outbuf, "%d", result.int_value);
        }
    }
    return 0;
}

#if 0
static void copy_text(char *dst, int dst_size, const char *src)
{
    int i;

    if (dst_size <= 0) {
        return;
    }
    for (i = 0; i < dst_size - 1 && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void append_text(char *dst, int dst_size, const char *src)
{
    int len;

    if (dst_size <= 0) {
        return;
    }
    len = 0;
    while (dst[len] != '\0' && len < dst_size - 1) {
        ++len;
    }
    while (src[0] != '\0' && len < dst_size - 1) {
        dst[len] = *src;
        ++len;
        ++src;
    }
    dst[len] = '\0';
}

static int outch(int c)
{
    return putchar(c);
}

static int pstr(const char *s)
{
    while (*s != '\0') {
        outch(*s);
        ++s;
    }
    return 0;
}

static int pnum(int n)
{
    char b[10];
    int i;

    if (n == 0) {
        outch('0');
        return 0;
    }

    i = 0;
    while (n > 0 && i < 10) {
        b[i] = (char)((n % 10) + '0');
        ++i;
        n = n / 10;
    }

    while (i > 0) {
        --i;
        outch(b[i]);
    }
    return 0;
}

static int cur(int r, int c)
{
    outch(27);
    pstr("[");
    pnum(r);
    pstr(";");
    pnum(c);
    pstr("H");
    return 0;
}

static int cls(void)
{
    outch(27);
    pstr("[2J");
    outch(27);
    pstr("[H");
    return 0;
}

static int rst(void)
{
    outch(27);
    pstr("[0m");
    return 0;
}

static int setfg(int c)
{
    outch(27);
    pstr("[");
    pnum(c);
    pstr("m");
    return 0;
}

static int setbg(int c)
{
    outch(27);
    pstr("[");
    pnum(c);
    pstr("m");
    return 0;
}

static void draw_line(int row, int col, const char *text, int color)
{
    int i;
    int len;
    char trunc[82];

    len = strlen(text);
    if (len > SCREEN_WIDTH - col + 1) {
        len = SCREEN_WIDTH - col + 1;
    }
    if (len < 0) {
        len = 0;
    }
    memcpy(trunc, text, len);
    trunc[len] = '\0';

    cur(row, col);
    setfg(color);
    for (i = col; i <= SCREEN_WIDTH + 1; ++i) {
        outch(' ');
    }
    cur(row, col);
    pstr(trunc);
    rst();
}

static void draw_box(int top, int left, int height, int width)
{
    int r;
    int c;

    cur(top, left);
    outch('+');
    for (c = 1; c < width; ++c) {
        outch('-');
    }
    outch('+');
    for (r = 1; r < height; ++r) {
        cur(top + r, left);
        outch('|');
        cur(top + r, left + width);
        outch('|');
    }
    cur(top + height, left);
    outch('+');
    for (c = 1; c < width; ++c) {
        outch('-');
    }
    outch('+');
}

static void draw_screen(char *expr, char *result, char *status, char history[][MAX_LINE], int history_count)
{
    int i;
    char linebuf[82];

    cls();
    draw_box(1, 1, 28, 78);
    setfg(36);
    cur(2, 3);
    pstr("ALT AIR CALCULATOR");
    rst();
    setfg(33);
    cur(2, 55);
    pstr("ESC/Quit");
    rst();
    draw_line(3, 3, "Type an expression, /bye exits, /clear clears history", 37);
    draw_line(5, 3, "Expression:", 36);
    draw_line(6, 3, expr, 37);
    draw_line(8, 3, "Result:", 36);
    draw_line(9, 3, result, 33);
    draw_line(11, 3, "History:", 36);
    for (i = 0; i < MAX_HISTORY; ++i) {
        if (i < history_count) {
            sprintf(linebuf, "%d. %s", i + 1, history[i]);
            draw_line(12 + i, 3, linebuf, 37);
        } else {
            draw_line(12 + i, 3, "", 37);
        }
    }
    draw_line(24, 3, "Status:", 36);
    draw_line(25, 3, status, 35);
    draw_line(27, 3, "Calc>", 36);
    draw_line(28, 3, "", 37);
    cur(28, 8);
    fflush(stdout);
}

static int read_key(void)
{
    int ch;

    while ((bdos(11) & 0xFF) == 0) {
    }
    ch = bdos(6, 0xFF) & 0xFF;
    return ch;
}

static int read_line_ui(char *line, int line_size)
{
    int pos;
    int ch;
    char temp[82];

    pos = 0;
    line[0] = '\0';
    while (1) {
        ch = read_key();
        if (ch == 13 || ch == 10) {
            break;
        }
        if (ch == 3 || ch == 27) {
            line[0] = '\0';
            return 0;
        }
        if (ch == 8 || ch == 127) {
            if (pos > 0) {
                --pos;
            }
        } else if (ch >= 32 && pos < line_size - 1) {
            line[pos] = (char)ch;
            ++pos;
        }
        line[pos] = '\0';
        temp[0] = '\0';
        append_text(temp, sizeof(temp), "Calc> ");
        append_text(temp, sizeof(temp), line);
        draw_line(28, 3, temp, 37);
        cur(28, 8 + pos);
        fflush(stdout);
    }
    return 1;
}

static void add_history(char history[][MAX_LINE], int *history_count, char *expr, char *result)
{
    int i;
    char linebuf[82];

    if (*history_count < MAX_HISTORY) {
        ++(*history_count);
    }
    for (i = *history_count - 1; i > 0; --i) {
        copy_text(history[i], MAX_LINE, history[i - 1]);
    }
    linebuf[0] = '\0';
    append_text(linebuf, sizeof(linebuf), expr);
    append_text(linebuf, sizeof(linebuf), " => ");
    append_text(linebuf, sizeof(linebuf), result);
    copy_text(history[0], MAX_LINE, linebuf);
}

static void clear_history(char history[][MAX_LINE], int *history_count)
{
    int i;

    for (i = 0; i < MAX_HISTORY; ++i) {
        history[i][0] = '\0';
    }
    *history_count = 0;
}
#endif

static void append_argument(char *line, int line_size, const char *text)
{
    int length;

    length = 0;
    while (line[length] != '\0') {
        ++length;
    }
    while (*text != '\0' && length < line_size - 1) {
        line[length] = *text;
        ++length;
        ++text;
    }
    line[length] = '\0';
}

static int read_line(char *line, int line_size)
{
    int character;
    int length;

    length = 0;
    line[0] = '\0';
    while (1) {
        do {
            character = bdos(6, 0xff) & 0xff;
        } while (character == 0);

        if (character == '\r' || character == '\n') {
            putchar('\r');
            putchar('\n');
            return 1;
        }
        if (character == 3) {
            putchar('\r');
            putchar('\n');
            return 0;
        }
        if (character == '\b' || character == 127) {
            if (length > 0) {
                --length;
                line[length] = '\0';
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (character >= 32 && character < 127 && length < line_size - 1) {
            line[length] = (char)character;
            ++length;
            line[length] = '\0';
            putchar(character);
        }
        fflush(stdout);
    }
}

static void print_help(void)
{
    printf("Available commands:\n");
    printf("  /help   Show this help text\n");
    printf("  /bye    Exit the calculator\n");
    printf("\n");
    printf("Operators:\n");
    printf("  +   add\n");
    printf("  -   subtract or unary negation\n");
    printf("  +   unary plus\n");
    printf("  *   multiply\n");
    printf("  /   divide\n");
    printf("  //  integer division (integers only)\n");
    printf("  %%   modulus (integers only)\n");
    printf("  ^   power (exponent)\n");
    printf("  ~   unary bitwise not (integers only)\n");
    printf("  =   equality comparison\n");
    printf("  parentheses: ( and )\n");
    printf("\n");
    printf("Examples:\n");
    printf("  2 + 2          -> 4\n");
    printf("  10.0 / 4       -> 2.5\n");
    printf("  10 // 4        -> 2\n");
    printf("  (3 + 5) * 2    -> 16\n");
    printf("  7 %% 3          -> 1\n");
    printf("  2 ^ 3          -> 8\n");
    printf("  ~5             -> -6\n");
    printf("  -5 + 3         -> -2\n");
}

int main(int argc, char *argv[])
{
    char line[MAX_LINE];
    char result[MAX_LINE];
    int index;

    if (argc > 1) {
        line[0] = '\0';
        for (index = 1; index < argc; ++index) {
            if (index > 1) {
                append_argument(line, sizeof(line), " ");
            }
            append_argument(line, sizeof(line), argv[index]);
        }
        if (same_command(line, "/help")) {
            print_help();
            return 0;
        }
        if (evaluate_expression(line, result, sizeof(result)) != 0) {
            printf("Error: %s\n", result);
            return 1;
        }
        printf("Result: %s\n", result);
        return 0;
    }

    printf("Type /help for help\n");
    while (1) {
        printf("Calc> ");
        fflush(stdout);
        if (!read_line(line, sizeof(line))) {
            return 0;
        }
        if (same_command(line, "/bye")) {
            return 0;
        }
        if (same_command(line, "/help")) {
            print_help();
            continue;
        }
        if (*skip_spaces(line) != '\0') {
            if (evaluate_expression(line, result, sizeof(result)) != 0) {
                printf("Error: %s\n", result);
            } else {
                printf("Result: %s\n", result);
            }
        }
    }
}
