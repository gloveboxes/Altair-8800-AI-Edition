#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SHEETS.H"

/*
 * Formula engine and cell storage for SHEETS.
 *
 * dcc C11 implementation using the target's native signed 32-bit long type.
 * Parser state is private to this module; only the cell API is shared with the
 * UI. Formulas support +, -, *, /, parentheses, A1 references, SUM, AVG, MIN,
 * MAX, COUNT, and RAND.
 */

#define MAX_EVALUATION_DEPTH 25
#define MAX_FORMULA_LENGTH 83
#define NORMALIZED_FORMULA_SIZE 84

enum range_operation {
    RANGE_SUM,
    RANGE_AVERAGE,
    RANGE_MINIMUM,
    RANGE_MAXIMUM,
    RANGE_COUNT
};

static const char *parse_position;
static bool parse_valid;
static uint8_t evaluation_depth;
static uint8_t evaluation_rows[MAX_EVALUATION_DEPTH];
static uint8_t evaluation_columns[MAX_EVALUATION_DEPTH];
static bool circular_reference;

static bool parse_expression(long *result);
static bool evaluate_cell(int row, int column, long *result);

bool is_ascii_digit(int character)
{
    return character >= '0' && character <= '9';
}

bool is_ascii_letter(int character)
{
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
}

int uppercase_ascii(int character)
{
    if (character >= 'a' && character <= 'z')
        return character - ('a' - 'A');
    return character;
}

static void skip_spaces(void)
{
    while (*parse_position == ' ' || *parse_position == '\t')
        ++parse_position;
}

static int function_name_length_at(const char *text, const char *name)
{
    int length = 0;

    while (name[length] != '\0') {
        if (uppercase_ascii((unsigned char)text[length]) != name[length])
            return 0;
        ++length;
    }
    while (text[length] == ' ' || text[length] == '\t')
        ++length;
    return text[length] == '(' ? length + 1 : 0;
}

static bool match_function_name(const char *name)
{
    int length = function_name_length_at(parse_position, name);

    if (length == 0)
        return false;
    parse_position += length;
    return true;
}

static bool formula_uses_random(const char *text)
{
    while (*text != '\0') {
        if (function_name_length_at(text, "RAND") != 0)
            return true;
        ++text;
    }
    return false;
}

static size_t normalize_formula(char *destination, const char *source)
{
    size_t output_length = 0;

    while (*source != '\0') {
        if (*source == ' ' || *source == '\t') {
            ++source;
            continue;
        }
        if (is_ascii_letter((unsigned char)*source)) {
            char letter_run[16];
            uint8_t run_length = 0;
            uint8_t index;

            while (is_ascii_letter((unsigned char)*source)) {
                if (run_length < sizeof(letter_run) - 1)
                    letter_run[run_length++] = *source;
                ++source;
            }
            letter_run[run_length] = '\0';
            while (*source == ' ' || *source == '\t')
                ++source;

            for (index = 0; index < run_length; ++index) {
                int character = letter_run[index];
                if (*source == '(') {
                    if (character >= 'A' && character <= 'Z')
                        character += 'a' - 'A';
                } else {
                    character = uppercase_ascii(character);
                }
                destination[output_length++] = (char)character;
            }
        } else {
            destination[output_length++] = *source++;
        }
    }
    destination[output_length] = '\0';
    return output_length;
}

static long parse_text_long(const char *text)
{
    bool negative = false;
    long value = 0;

    if (*text == '-') {
        negative = true;
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    while (is_ascii_digit((unsigned char)*text)) {
        value = value * 10 + (*text - '0');
        ++text;
    }
    return negative ? -value : value;
}

static void format_long(char *buffer, long value)
{
    char digits[11];
    uint8_t count = 0;
    uint8_t output = 0;
    unsigned long magnitude;

    if (value < 0) {
        buffer[output++] = '-';
        magnitude = (unsigned long)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned long)value;
    }
    if (magnitude == 0)
        digits[count++] = '0';
    while (magnitude != 0) {
        digits[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    while (count != 0)
        buffer[output++] = digits[--count];
    buffer[output] = '\0';
}

static bool cell_on_evaluation_stack(int row, int column)
{
    uint8_t index;

    for (index = 0; index < evaluation_depth; ++index) {
        if (evaluation_rows[index] == row &&
            evaluation_columns[index] == column)
            return true;
    }
    return false;
}

static bool parse_cell_reference(int *row, int *column)
{
    int parsed_row = 0;
    int parsed_column;

    skip_spaces();
    if (*parse_position == '$')
        ++parse_position;
    if (!is_ascii_letter((unsigned char)*parse_position))
        return false;
    parsed_column = uppercase_ascii((unsigned char)*parse_position++) - 'A';
    if (*parse_position == '$')
        ++parse_position;
    if (!is_ascii_digit((unsigned char)*parse_position))
        return false;
    while (is_ascii_digit((unsigned char)*parse_position)) {
        parsed_row = parsed_row * 10 + (*parse_position - '0');
        ++parse_position;
    }
    --parsed_row;
    if (parsed_row < 0 || parsed_row >= MAX_ROWS ||
        parsed_column < 0 || parsed_column >= MAX_COLUMNS)
        return false;
    *row = parsed_row;
    *column = parsed_column;
    return true;
}

static uint16_t random_number(void)
{
    uint16_t value;

    outp(45, 1);
    value = (uint8_t)inp(200);
    value |= (uint16_t)((uint8_t)inp(200)) << 8;
    return value & 0x7fff;
}

static bool evaluate_cell(int row, int column, long *result)
{
    const char *cell_text;
    const char *saved_position;
    bool result_valid;

    *result = 0;
    if (row < 0 || row >= MAX_ROWS || column < 0 || column >= MAX_COLUMNS)
        return false;
    cell_text = CELL_AT(row, column);
    if (cell_text == NULL || *cell_text == '\0')
        return true;
    if (*cell_text != '=') {
        if (*cell_text == '-' || is_ascii_digit((unsigned char)*cell_text))
            *result = parse_text_long(cell_text);
        return true;
    }
    if (cell_on_evaluation_stack(row, column)) {
        circular_reference = true;
        parse_valid = false;
        return false;
    }
    if (evaluation_depth >= MAX_EVALUATION_DEPTH)
        return false;

    evaluation_rows[evaluation_depth] = (uint8_t)row;
    evaluation_columns[evaluation_depth] = (uint8_t)column;
    ++evaluation_depth;
    saved_position = parse_position;
    parse_position = cell_text + 1;
    parse_valid = true;
    result_valid = parse_expression(result);
    parse_position = saved_position;
    --evaluation_depth;
    return result_valid && parse_valid;
}

static bool parse_random_function(long *result)
{
    long limit;

    skip_spaces();
    if (*parse_position == ')') {
        ++parse_position;
        *result = random_number();
        return true;
    }
    if (!parse_expression(&limit))
        return false;
    skip_spaces();
    if (*parse_position != ')') {
        parse_valid = false;
        return false;
    }
    ++parse_position;
    *result = limit > 0 ? random_number() % limit : random_number();
    return true;
}

static bool fold_range(long *result, enum range_operation operation)
{
    int first_row;
    int first_column;
    int last_row;
    int last_column;
    int row;
    int column;
    unsigned count = 0;
    bool have_number = false;

    if (!parse_cell_reference(&first_row, &first_column)) {
        parse_valid = false;
        return false;
    }
    skip_spaces();
    if (*parse_position != ':') {
        parse_valid = false;
        return false;
    }
    ++parse_position;
    if (!parse_cell_reference(&last_row, &last_column)) {
        parse_valid = false;
        return false;
    }
    skip_spaces();
    if (*parse_position != ')') {
        parse_valid = false;
        return false;
    }
    ++parse_position;
    *result = 0;

    for (row = first_row; row <= last_row; ++row) {
        for (column = first_column; column <= last_column; ++column) {
            const char *cell_text = CELL_AT(row, column);
            long value;

            if (cell_on_evaluation_stack(row, column)) {
                circular_reference = true;
                parse_valid = false;
                return false;
            }
            if (operation == RANGE_COUNT) {
                if (cell_text != NULL && *cell_text != '\0')
                    ++count;
                continue;
            }
            if (cell_text == NULL || (*cell_text != '=' && *cell_text != '-' &&
                                      !is_ascii_digit((unsigned char)*cell_text)))
                continue;
            if (!evaluate_cell(row, column, &value)) {
                parse_valid = false;
                return false;
            }
            ++count;
            if (operation == RANGE_SUM || operation == RANGE_AVERAGE) {
                *result += value;
            } else if (operation == RANGE_MINIMUM) {
                if (!have_number || value < *result)
                    *result = value;
                have_number = true;
            } else if (operation == RANGE_MAXIMUM) {
                if (!have_number || value > *result)
                    *result = value;
                have_number = true;
            }
        }
    }

    if (operation == RANGE_AVERAGE) {
        if (count == 0) {
            parse_valid = false;
            return false;
        }
        *result /= count;
    } else if (operation == RANGE_COUNT) {
        *result = count;
    }
    return true;
}

static int parse_function(long *result)
{
    if (match_function_name("RAND"))
        return parse_random_function(result);
    if (match_function_name("SUM"))
        return fold_range(result, RANGE_SUM);
    if (match_function_name("AVG"))
        return fold_range(result, RANGE_AVERAGE);
    if (match_function_name("MIN"))
        return fold_range(result, RANGE_MINIMUM);
    if (match_function_name("MAX"))
        return fold_range(result, RANGE_MAXIMUM);
    if (match_function_name("COUNT"))
        return fold_range(result, RANGE_COUNT);
    return -1;
}

static bool parse_factor(long *result)
{
    bool negative = false;
    int row;
    int column;
    int function_result;

    skip_spaces();
    while (*parse_position == '-' || *parse_position == '+') {
        if (*parse_position == '-')
            negative = !negative;
        ++parse_position;
        skip_spaces();
    }

    if (*parse_position == '(') {
        ++parse_position;
        if (!parse_expression(result))
            return false;
        skip_spaces();
        if (*parse_position != ')') {
            parse_valid = false;
            return false;
        }
        ++parse_position;
    } else if (is_ascii_digit((unsigned char)*parse_position)) {
        *result = 0;
        while (is_ascii_digit((unsigned char)*parse_position)) {
            *result = *result * 10 + (*parse_position - '0');
            ++parse_position;
        }
    } else if ((function_result = parse_function(result)) >= 0) {
        if (function_result == 0)
            return false;
    } else if (is_ascii_letter((unsigned char)*parse_position) ||
               *parse_position == '$') {
        if (!parse_cell_reference(&row, &column) ||
            !evaluate_cell(row, column, result)) {
            parse_valid = false;
            return false;
        }
    } else {
        parse_valid = false;
        return false;
    }

    if (negative)
        *result = -*result;
    return true;
}

static bool parse_term(long *result)
{
    long right;

    if (!parse_factor(result))
        return false;
    skip_spaces();
    while (*parse_position == '*' || *parse_position == '/') {
        char operation = *parse_position++;
        if (!parse_factor(&right))
            return false;
        if (operation == '*') {
            *result *= right;
        } else {
            if (right == 0) {
                parse_valid = false;
                return false;
            }
            *result /= right;
        }
        skip_spaces();
    }
    return true;
}

static bool parse_expression(long *result)
{
    long right;

    if (!parse_term(result))
        return false;
    skip_spaces();
    while (*parse_position == '+' || *parse_position == '-') {
        char operation = *parse_position++;
        if (!parse_term(&right))
            return false;
        if (operation == '+')
            *result += right;
        else
            *result -= right;
        skip_spaces();
    }
    return true;
}

int set_cell(int row, int column, const char *text)
{
    char normalized[NORMALIZED_FORMULA_SIZE];
    char frozen_value[12];
    char *stored_text;
    size_t length;

    if (row < 0 || row >= MAX_ROWS || column < 0 || column >= MAX_COLUMNS)
        return -1;
    free(CELL_AT(row, column));
    CELL_AT(row, column) = NULL;
    if (text == NULL)
        return 0;

    while (*text == ' ' || *text == '\t')
        ++text;
    length = strlen(text);
    while (length != 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'))
        --length;
    if (length == 0)
        return 0;

    if (*text == '=' && length <= MAX_FORMULA_LENGTH) {
        char source[NORMALIZED_FORMULA_SIZE];
        memcpy(source, text, length);
        source[length] = '\0';
        length = normalize_formula(normalized, source);
        text = normalized;
    }

    if (*text == '=' && formula_uses_random(text)) {
        long result;
        const char *saved_position = parse_position;
        evaluation_depth = 0;
        parse_position = text + 1;
        parse_valid = true;
        if (parse_expression(&result) && parse_valid) {
            format_long(frozen_value, result);
            text = frozen_value;
            length = strlen(text);
        }
        parse_position = saved_position;
    }

    stored_text = malloc(length + 1);
    if (stored_text == NULL)
        return -1;
    memcpy(stored_text, text, length);
    stored_text[length] = '\0';
    CELL_AT(row, column) = stored_text;
    return 0;
}

void clear_cell(int row, int column)
{
    if (row < 0 || row >= MAX_ROWS || column < 0 || column >= MAX_COLUMNS)
        return;
    if (CELL_AT(row, column) != NULL) {
        free(CELL_AT(row, column));
        CELL_AT(row, column) = NULL;
        dirty = 1;
    }
}

static bool contains_reference_error(const char *text)
{
    while (*text != '\0') {
        if (text[0] == '#' && text[1] == 'R' && text[2] == 'E' &&
            text[3] == 'F' && text[4] == '!')
            return true;
        ++text;
    }
    return false;
}

void render_cell(int row, int column, char *buffer)
{
    const char *cell_text;
    char value_text[12];
    size_t length;
    size_t index;
    size_t padding;

    for (index = 0; index < CELL_WIDTH; ++index)
        buffer[index] = ' ';
    buffer[CELL_WIDTH] = '\0';

    cell_text = CELL_AT(row, column);
    if (cell_text == NULL || *cell_text == '\0')
        return;

    if (*cell_text == '=') {
        long value;
        bool valid;

        if (contains_reference_error(cell_text)) {
            memcpy(buffer, "  #REF!   ", CELL_WIDTH);
            return;
        }
        evaluation_depth = 0;
        circular_reference = false;
        valid = evaluate_cell(row, column, &value);
        if (!valid) {
            memcpy(buffer, circular_reference ? "  #CIRC   " : "  #ERR    ",
                   CELL_WIDTH);
            return;
        }
        format_long(value_text, value);
        cell_text = value_text;
    }

    length = strlen(cell_text);
    if (length > CELL_WIDTH) {
        if (*CELL_AT(row, column) == '=' || *cell_text == '-' ||
            is_ascii_digit((unsigned char)*cell_text)) {
            for (index = 0; index < CELL_WIDTH; ++index)
                buffer[index] = '#';
        } else {
            memcpy(buffer, cell_text, CELL_WIDTH);
        }
        return;
    }

    if (*CELL_AT(row, column) == '=' || *cell_text == '-' ||
        is_ascii_digit((unsigned char)*cell_text)) {
        padding = CELL_WIDTH - length;
        memcpy(buffer + padding, cell_text, length);
    } else {
        memcpy(buffer, cell_text, length);
    }
}
