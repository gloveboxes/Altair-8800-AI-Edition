#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SHEETS.H"
#include "../COMMON/DXSVC.H"

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
#define FORMULA_CACHE_SIZE 64

char *cells[MAX_ROWS][MAX_COLUMNS];

enum range_operation {
    RANGE_SUM,
    RANGE_AVERAGE,
    RANGE_MINIMUM,
    RANGE_MAXIMUM,
    RANGE_COUNT
};

struct dependency_bounds {
    uint8_t first_row;
    uint8_t last_row;
    uint8_t first_column;
    uint8_t last_column;
};

#define CACHE_DISPLAY_READY 128
#define CACHE_HEIGHT_MASK 127

static const char *parse_position;
static bool parse_valid;
static uint8_t evaluation_depth;
static uint8_t evaluation_rows[MAX_EVALUATION_DEPTH];
static uint8_t evaluation_columns[MAX_EVALUATION_DEPTH];
static bool circular_reference;
static struct dependency_bounds evaluation_bounds[MAX_EVALUATION_DEPTH];
static uint8_t evaluation_heights[MAX_EVALUATION_DEPTH];
static bool evaluation_cacheable[MAX_EVALUATION_DEPTH];
/* Key zero means empty. Dependency rectangles conservatively include all
 * transitive reads, so edits can retain entries outside the changed area.
 * Failures are context-dependent and are deliberately not cached. */
static uint16_t formula_cache_keys[FORMULA_CACHE_SIZE];
static long formula_cache_values[FORMULA_CACHE_SIZE];
static struct dependency_bounds formula_cache_bounds[FORMULA_CACHE_SIZE];
static uint8_t formula_cache_heights[FORMULA_CACHE_SIZE];
static char formula_cache_display[FORMULA_CACHE_SIZE][CELL_WIDTH];
static uint8_t cached_formula_count;
static uint8_t formula_cache_victims[FORMULA_CACHE_SIZE / 2];

void invalidate_formula_cache(void)
{
    memset(formula_cache_keys, 0, sizeof(formula_cache_keys));
    cached_formula_count = 0;
}

static inline uint16_t cell_key(int row, int column)
{
    return (uint16_t)(row * MAX_COLUMNS + column + 1);
}

static inline uint8_t cache_index_for(uint16_t key)
{
    return (uint8_t)((key & (FORMULA_CACHE_SIZE / 2 - 1)) * 2);
}

static inline uint8_t find_cached_formula(uint16_t key)
{
    uint8_t index = cache_index_for(key);
    if (formula_cache_keys[index] == key)
        return index;
    ++index;
    if (formula_cache_keys[index] == key)
        return index;
    return FORMULA_CACHE_SIZE;
}

static uint8_t reserve_cache_entry(uint16_t key)
{
    uint8_t index = cache_index_for(key);
    uint8_t bucket = index / 2;

    if (formula_cache_keys[index] != 0) {
        if (formula_cache_keys[index + 1] == 0)
            ++index;
        else
            index += formula_cache_victims[bucket];
    }
    formula_cache_victims[bucket] = (index & 1) ^ 1;
    if (formula_cache_keys[index] == 0)
        ++cached_formula_count;
    return index;
}

static void invalidate_changed_cell(int row, int column)
{
    uint16_t key = cell_key(row, column);
    uint8_t index;

    if (cached_formula_count == 0)
        return;
    for (index = 0; index < FORMULA_CACHE_SIZE; ++index) {
        const struct dependency_bounds *bounds = &formula_cache_bounds[index];
        if (formula_cache_keys[index] != 0 &&
            (formula_cache_keys[index] == key ||
             (row >= bounds->first_row && row <= bounds->last_row &&
              column >= bounds->first_column && column <= bounds->last_column))) {
            formula_cache_keys[index] = 0;
            if (--cached_formula_count == 0)
                return;
        }
    }
}

static void include_bounds(struct dependency_bounds *destination,
                           const struct dependency_bounds *source)
{
    if (source->first_row < destination->first_row)
        destination->first_row = source->first_row;
    if (source->last_row > destination->last_row)
        destination->last_row = source->last_row;
    if (source->first_column < destination->first_column)
        destination->first_column = source->first_column;
    if (source->last_column > destination->last_column)
        destination->last_column = source->last_column;
}

static void record_reference(int first_row, int first_column,
                             int last_row, int last_column)
{
    struct dependency_bounds bounds;

    if (evaluation_depth == 0)
        return;
    bounds.first_row = (uint8_t)first_row;
    bounds.last_row = (uint8_t)last_row;
    bounds.first_column = (uint8_t)first_column;
    bounds.last_column = (uint8_t)last_column;
    include_bounds(&evaluation_bounds[evaluation_depth - 1], &bounds);
}

static inline void record_cell_reference(int row, int column)
{
    if (evaluation_depth != 0) {
        struct dependency_bounds *bounds = &evaluation_bounds[evaluation_depth - 1];
        if (row < bounds->first_row)
            bounds->first_row = (uint8_t)row;
        if (row > bounds->last_row)
            bounds->last_row = (uint8_t)row;
        if (column < bounds->first_column)
            bounds->first_column = (uint8_t)column;
        if (column > bounds->last_column)
            bounds->last_column = (uint8_t)column;
    }
}

static void record_dependency(const struct dependency_bounds *bounds,
                              uint8_t height)
{
    if (evaluation_depth != 0) {
        uint8_t parent = evaluation_depth - 1;
        include_bounds(&evaluation_bounds[parent], bounds);
        if (height + 1 > evaluation_heights[parent])
            evaluation_heights[parent] = height + 1;
    }
}

static bool parse_expression(long *result);
static bool evaluate_cell(int row, int column, long *result);

static inline void skip_spaces(void)
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
        if ((*text == 'r' || *text == 'R') &&
            function_name_length_at(text, "RAND") != 0)
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
        if (is_letter((unsigned char)*source)) {
            char letter_run[16];
            uint8_t run_length = 0;
            uint8_t index;

            while (is_letter((unsigned char)*source)) {
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
    unsigned long value = 0;

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
    return (long)(negative ? 0UL - value : value);
}

/* Numeric cells retain their original text, followed by a pre-parsed native
 * long after the NUL. Grid moves and free() still operate on the text pointer. */
static inline void read_number(const char *text, long *result)
{
    memcpy(result, text + strlen(text) + 1, sizeof(long));
}

static void format_long(char *buffer, long value)
{
    static const unsigned long powers[] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL
    };
    uint8_t index;
    uint8_t output = 0;
    bool started = false;
    unsigned long magnitude;

    if (value < 0) {
        buffer[output++] = '-';
        magnitude = (unsigned long)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned long)value;
    }
    /* Decimal subtraction avoids the Z80 runtime's two 32-bit divisions per
     * digit. At most nine subtractions are needed for each decimal place. */
    for (index = 0; index < sizeof(powers) / sizeof(powers[0]); ++index) {
        unsigned long place = powers[index];
        char digit = '0';
        while (magnitude >= place) {
            magnitude -= place;
            ++digit;
        }
        if (digit != '0' || started) {
            buffer[output++] = digit;
            started = true;
        }
    }
    buffer[output++] = (char)('0' + magnitude);
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

    if (*parse_position == '$')
        ++parse_position;
    if (!is_letter((unsigned char)*parse_position))
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
    return svc_random_u16() & 0x7fff;
}

static bool evaluate_cell(int row, int column, long *result)
{
    const char *cell_text;
    const char *saved_position;
    bool result_valid;
    uint16_t cache_key;
    uint8_t cache_index;
    uint8_t depth;
    uint8_t height;

    *result = 0;
    if (row < 0 || row >= MAX_ROWS || column < 0 || column >= MAX_COLUMNS)
        return false;
    cell_text = CELL_AT(row, column);
    if (cell_text == NULL || *cell_text == '\0')
        return true;
    if (*cell_text != '=') {
        if (*cell_text == '-' || is_ascii_digit((unsigned char)*cell_text))
            read_number(cell_text, result);
        return true;
    }
    if (evaluation_depth >= MAX_EVALUATION_DEPTH)
        return false;

    cache_key = cell_key(row, column);
    cache_index = find_cached_formula(cache_key);
    if (cache_index != FORMULA_CACHE_SIZE) {
        height = formula_cache_heights[cache_index] & CACHE_HEIGHT_MASK;
        if (evaluation_depth + height > MAX_EVALUATION_DEPTH)
            return false;
        *result = formula_cache_values[cache_index];
        record_dependency(&formula_cache_bounds[cache_index], height);
        return true;
    }
    /* A successful cached dependency cannot introduce a cycle. COUNT ranges
     * containing formulas have caller-dependent cycle rules and stay uncached. */
    if (cell_on_evaluation_stack(row, column)) {
        circular_reference = true;
        parse_valid = false;
        return false;
    }

    depth = evaluation_depth;
    evaluation_rows[depth] = (uint8_t)row;
    evaluation_columns[depth] = (uint8_t)column;
    evaluation_bounds[depth].first_row = MAX_ROWS;
    evaluation_bounds[depth].last_row = 0;
    evaluation_bounds[depth].first_column = MAX_COLUMNS;
    evaluation_bounds[depth].last_column = 0;
    evaluation_heights[depth] = 1;
    evaluation_cacheable[depth] = true;
    ++evaluation_depth;
    saved_position = parse_position;
    parse_position = cell_text + 1;
    parse_valid = true;
    result_valid = parse_expression(result);
    parse_position = saved_position;
    --evaluation_depth;
    if (result_valid && parse_valid) {
        if (evaluation_cacheable[depth]) {
            cache_index = reserve_cache_entry(cache_key);
            formula_cache_values[cache_index] = *result;
            formula_cache_bounds[cache_index] = evaluation_bounds[depth];
            formula_cache_heights[cache_index] = evaluation_heights[depth];
            formula_cache_keys[cache_index] = cache_key;
        } else if (evaluation_depth != 0) {
            evaluation_cacheable[evaluation_depth - 1] = false;
        }
        record_dependency(&evaluation_bounds[depth], evaluation_heights[depth]);
        return true;
    }
    return false;
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
    uint8_t stack_index;
    unsigned count = 0;
    bool have_number = false;

    skip_spaces();
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
    skip_spaces();
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

    if (first_row <= last_row && first_column <= last_column) {
        record_reference(first_row, first_column, last_row, last_column);
        /* A range intersects the active formula stack or it doesn't; this
         * check need not be repeated for every numeric or empty cell. */
        for (stack_index = 0; stack_index < evaluation_depth; ++stack_index) {
            if (evaluation_rows[stack_index] >= first_row &&
                evaluation_rows[stack_index] <= last_row &&
                evaluation_columns[stack_index] >= first_column &&
                evaluation_columns[stack_index] <= last_column) {
                circular_reference = true;
                parse_valid = false;
                return false;
            }
        }
    }

    for (row = first_row; row <= last_row; ++row) {
        char **slot = &CELL_AT(row, first_column);
        for (column = first_column; column <= last_column; ++column) {
            const char *cell_text = *slot++;
            long value;

            if (operation == RANGE_COUNT) {
                if (cell_text != NULL && *cell_text != '\0') {
                    ++count;
                    if (*cell_text == '=' && evaluation_depth != 0)
                        evaluation_cacheable[evaluation_depth - 1] = false;
                }
                continue;
            }
            if (cell_text == NULL || (*cell_text != '=' && *cell_text != '-' &&
                                      !is_ascii_digit((unsigned char)*cell_text)))
                continue;
            if (*cell_text == '=') {
                if (!evaluate_cell(row, column, &value)) {
                    parse_valid = false;
                    return false;
                }
            } else {
                read_number(cell_text, &value);
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
    switch (uppercase_ascii((unsigned char)*parse_position)) {
    case 'R':
        if (match_function_name("RAND"))
            return parse_random_function(result);
        break;
    case 'S':
        if (match_function_name("SUM"))
            return fold_range(result, RANGE_SUM);
        break;
    case 'A':
        if (match_function_name("AVG"))
            return fold_range(result, RANGE_AVERAGE);
        break;
    case 'M':
        if (match_function_name("MIN"))
            return fold_range(result, RANGE_MINIMUM);
        if (match_function_name("MAX"))
            return fold_range(result, RANGE_MAXIMUM);
        break;
    case 'C':
        if (match_function_name("COUNT"))
            return fold_range(result, RANGE_COUNT);
        break;
    }
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
    } else {
        function_result = -1;
        /* A1 and $A$1 are references, without probing any function names. */
        if (is_letter((unsigned char)*parse_position) &&
            is_letter((unsigned char)parse_position[1]))
            function_result = parse_function(result);
        if (function_result >= 0) {
            if (function_result == 0)
                return false;
        } else if (parse_cell_reference(&row, &column)) {
            record_cell_reference(row, column);
            if (!evaluate_cell(row, column, result)) {
                parse_valid = false;
                return false;
            }
        } else {
            parse_valid = false;
            return false;
        }
    }

    if (negative)
        *result = -*result;
    skip_spaces();
    return true;
}

static bool parse_term(long *result)
{
    long right;

    if (!parse_factor(result))
        return false;
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
    }
    return true;
}

static bool parse_expression(long *result)
{
    long right;

    if (!parse_term(result))
        return false;
    while (*parse_position == '+' || *parse_position == '-') {
        char operation = *parse_position++;
        if (!parse_term(&right))
            return false;
        if (operation == '+')
            *result += right;
        else
            *result -= right;
    }
    return true;
}

int set_cell(int row, int column, const char *text)
{
    char normalized[NORMALIZED_FORMULA_SIZE];
    char frozen_value[12];
    char *stored_text;
    size_t length;
    bool numeric;
    bool uses_random = false;

    if (row < 0 || row >= MAX_ROWS || column < 0 || column >= MAX_COLUMNS)
        return -1;
    if (text == NULL)
        text = "";

    while (*text == ' ' || *text == '\t')
        ++text;
    length = strlen(text);
    while (length != 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'))
        --length;
    if (*text == '=' && length <= MAX_FORMULA_LENGTH) {
        char source[NORMALIZED_FORMULA_SIZE];
        memcpy(source, text, length);
        source[length] = '\0';
        length = normalize_formula(normalized, source);
        text = normalized;
    }

    uses_random = *text == '=' && formula_uses_random(text);
    if (!uses_random && CELL_AT(row, column) != NULL &&
        strlen(CELL_AT(row, column)) == length &&
        memcmp(CELL_AT(row, column), text, length) == 0)
        return 0;
    if (CELL_AT(row, column) != NULL || length != 0)
        invalidate_changed_cell(row, column);
    free(CELL_AT(row, column));
    CELL_AT(row, column) = NULL;
    if (length == 0)
        return 0;

    if (uses_random) {
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

    numeric = *text == '-' || is_ascii_digit((unsigned char)*text);
    stored_text = malloc(length + 1 + (numeric ? sizeof(long) : 0));
    if (stored_text == NULL)
        return -1;
    memcpy(stored_text, text, length);
    stored_text[length] = '\0';
    if (numeric) {
        long value = parse_text_long(stored_text);
        memcpy(stored_text + length + 1, &value, sizeof(long));
    }
    CELL_AT(row, column) = stored_text;
    /* Freezing RAND can evaluate formulas while this cell is temporarily
     * empty. Discard any results that depended on that temporary state. */
    if (uses_random)
        invalidate_changed_cell(row, column);
    return 0;
}

void clear_cell(int row, int column)
{
    if (row < 0 || row >= MAX_ROWS || column < 0 || column >= MAX_COLUMNS)
        return;
    if (CELL_AT(row, column) != NULL) {
        free(CELL_AT(row, column));
        CELL_AT(row, column) = NULL;
        invalidate_changed_cell(row, column);
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
    size_t padding;
    uint8_t cache_index = 0;
    bool formula;

    buffer[CELL_WIDTH] = '\0';

    cell_text = CELL_AT(row, column);
    if (cell_text == NULL || *cell_text == '\0') {
        memset(buffer, ' ', CELL_WIDTH);
        return;
    }

    formula = *cell_text == '=';
    if (formula) {
        long value;
        bool valid;
        uint16_t key = cell_key(row, column);

        cache_index = find_cached_formula(key);
        if (cache_index != FORMULA_CACHE_SIZE &&
            (formula_cache_heights[cache_index] & CACHE_DISPLAY_READY)) {
            memcpy(buffer, formula_cache_display[cache_index], CELL_WIDTH);
            return;
        }

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
        cache_index = find_cached_formula(key);
    }

    length = strlen(cell_text);
    if (length > CELL_WIDTH) {
        if (*CELL_AT(row, column) == '=' || *cell_text == '-' ||
            is_ascii_digit((unsigned char)*cell_text)) {
            memset(buffer, '#', CELL_WIDTH);
        } else {
            memcpy(buffer, cell_text, CELL_WIDTH);
        }
    } else if (formula || *cell_text == '-' ||
               is_ascii_digit((unsigned char)*cell_text)) {
        padding = CELL_WIDTH - length;
        memset(buffer, ' ', padding);
        memcpy(buffer + padding, cell_text, length);
    } else {
        memcpy(buffer, cell_text, length);
        memset(buffer + length, ' ', CELL_WIDTH - length);
    }
    if (formula && cache_index != FORMULA_CACHE_SIZE) {
        memcpy(formula_cache_display[cache_index], buffer, CELL_WIDTH);
        formula_cache_heights[cache_index] |= CACHE_DISPLAY_READY;
    }
}
