#include "stdio.h"
#include "string.h"
#include "ISAMDB.H"
#include "SURGERY.H"
#define H_NOTES_WIDTH 48
#define H_NOTES_EDIT_WIDTH 72
#define SCREEN_ROWS 30
#define SCREEN_COLS 80
#define LIST_FIRST_ROW 6
#define PATIENT_PAGE_SIZE 20
#define HISTORY_PAGE_SIZE 3
#define MAX_DOCTOR_PATIENTS 64
#define MAX_PATIENT_RESULTS MAX_DOCTOR_PATIENTS

#define KEY_ESC 27
#define KEY_ENTER 13
#define KEY_BACKSPACE 8
#define KEY_DELETE 127
#define KEY_FORWARD_DELETE 7
#define KEY_RIGHT 4
#define KEY_LEFT 19
#define KEY_UP 5
#define KEY_DOWN 24
#define FORM_INVALID (-100)
#define FORM_CANCEL (-101)

extern int getch(void);
extern int kbhit(void);

static char doctor_record[DOCTOR_SIZE];
static char patient_record[PATIENT_SIZE];
static char history_record[HISTORY_SIZE];
static int roster_ids[MAX_DOCTOR_PATIENTS];
static char form_first[13];
static char form_last[17];
static char form_address[37];
static char form_age[4];
static char form_gender[2];
static char form_phone[13];
static char form_date[9];
static char form_type[13];
static char form_notes[H_NOTES_SIZE + 1];
static char wrapped_line[H_NOTES_EDIT_WIDTH + 1];
static char history_doctor_first[13];
static char history_doctor_last[17];

static int ui_patients(int doctor_id, int patient_count, char *search_query);

static void put_field(char *record, int offset, int size, char *value)
{
    int i;

    for (i = 0; i < size; i++)
    {
        if (value && value[i])
            record[offset + i] = value[i];
        else
            record[offset + i] = 0;
    }
}

static void get_field(char *dest, char *record, int offset, int size)
{
    memcpy(dest, &record[offset], size);
    dest[size] = 0;
}

static void format_number(char *dest, int value, int width)
{
    int i;

    if (value < 0)
        value = 0;
    for (i = width - 1; i >= 0; i--)
    {
        dest[i] = '0' + (value % 10);
        value = value / 10;
    }
    dest[width] = 0;
}

static int read_number(char *record, int offset, int width)
{
    int i;
    int value;

    value = 0;
    for (i = 0; i < width; i++)
    {
        if (record[offset + i] < '0' || record[offset + i] > '9')
            return -1;
        value = value * 10 + record[offset + i] - '0';
    }
    return value;
}

static void put_number(char *record, int offset, int width, int value)
{
    char number[10];

    format_number(number, value, width);
    put_field(record, offset, width, number);
}

static void put_name_key(char *record, int key_offset, int name_offset,
    int name_length)
{
    int i;
    int value;

    for (i = 0; i < name_length; i++)
    {
        value = record[name_offset + i];
        if (value >= 'a' && value <= 'z')
            value -= 'a' - 'A';
        record[key_offset + i] = value;
    }
    memcpy(&record[key_offset + name_length], &record[P_ID], PATIENT_KEY_LEN);
}

static void write_patient_keys(char *record)
{
    put_name_key(record, P_LAST_KEY, P_LAST, 10);
    put_name_key(record, P_FIRST_KEY, P_FIRST, 7);
    memcpy(&record[P_DOCTOR_KEY], &record[P_DOCTOR_ID], DOCTOR_KEY_LEN);
    memcpy(&record[P_DOCTOR_KEY + DOCTOR_KEY_LEN], &record[P_ID],
        PATIENT_KEY_LEN);
}

static void make_history_key(char *key, int patient_id, int sequence)
{
    format_number(key, patient_id, PATIENT_KEY_LEN);
    format_number(&key[PATIENT_KEY_LEN], sequence, 3);
    key[HISTORY_KEY_LEN] = 0;
}

static int find_doctor(int doctor_id)
{
    char key[DOCTOR_KEY_LEN + 1];

    format_number(key, doctor_id, DOCTOR_KEY_LEN);
    return key_find(DOCTOR_TABLE, key, doctor_record);
}

static int find_patient(int patient_id)
{
    char key[PATIENT_KEY_LEN + 1];

    format_number(key, patient_id, PATIENT_KEY_LEN);
    return key_find(PATIENT_TABLE, key, patient_record);
}

static int find_history(int patient_id, int sequence)
{
    char key[HISTORY_KEY_LEN + 1];

    make_history_key(key, patient_id, sequence);
    return key_find(HISTORY_TABLE, key, history_record);
}

static void ui_move(int row, int column)
{
    printf("\033[%d;%dH", row, column);
}

static void ui_clear(void)
{
    printf("\033[2J\033[0m\033[H");
}

static void ui_clear_line(int row)
{
    ui_move(row, 1);
    printf("\033[K");
}

static void ui_cursor(int visible)
{
    if (visible)
        printf("\033[?25h");
    else
        printf("\033[?25l");
}

static void ui_header(char *section, char *detail)
{
    ui_clear();
    printf("\033[44;97m %-78s \033[0m", "RIVERSIDE DOCTORS SURGERY");
    ui_move(2, 2);
    printf("%-20s %s", section, detail);
    ui_move(3, 1);
    printf("-------------------------------------------------------------------------------");
}

static void ui_footer(char *keys)
{
    ui_move(28, 1);
    printf("-------------------------------------------------------------------------------");
    ui_clear_line(29);
    ui_move(29, 2);
    printf("%s", keys);
    ui_clear_line(30);
    ui_move(30, 2);
    printf("Cursor Up/Down move   Enter select   Esc back");
    fflush(stdout);
}

static int ui_next_escape_byte(void)
{
    int attempts;

    for (attempts = 0; attempts < 12000; attempts++)
    {
        if (kbhit())
            return getch() & 255;
    }
    return -1;
}

static int ui_read_key(void)
{
    int key;
    int next;

    key = getch() & 255;
    if (key != KEY_ESC)
        return key;
    next = ui_next_escape_byte();
    if (next != '[' && next != 'O')
        return KEY_ESC;
    next = ui_next_escape_byte();
    if (next == 'A')
        return KEY_UP;
    if (next == 'B')
        return KEY_DOWN;
    if (next == 'C')
        return KEY_RIGHT;
    if (next == 'D')
        return KEY_LEFT;
    return KEY_ESC;
}

static void ui_wrapped_text(char *text, int row, int column, int width);

static int ui_prompt(char *label, char *text, int max_length)
{
    int key;
    int length;

    length = 0;
    text[0] = 0;
    ui_clear_line(29);
    ui_move(29, 2);
    printf("%s", label);
    ui_cursor(1);
    fflush(stdout);
    for (;;)
    {
        key = ui_read_key();
        if (key == KEY_ESC)
        {
            ui_cursor(0);
            return 0;
        }
        if (key == KEY_ENTER || key == 10)
        {
            ui_cursor(0);
            return length > 0;
        }
        if (key == KEY_BACKSPACE || key == KEY_DELETE)
        {
            if (length > 0)
            {
                length--;
                text[length] = 0;
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (key >= 32 && key <= 126 && length < max_length)
        {
            text[length] = key;
            length++;
            text[length] = 0;
            putchar(key);
            fflush(stdout);
        }
    }
}

static int ui_form_field(int row, char *label, char *value, int max_length)
{
    int key;
    int length;
    int replace;

    length = strlen(value);
    replace = length > 0;
    ui_move(row, 4);
    printf("%-15s", label);
    ui_move(row, 21);
    printf("\033[7m%-40s\033[0m", value);
    ui_move(row, 21 + length);
    ui_cursor(1);
    fflush(stdout);
    for (;;)
    {
        key = ui_read_key();
        if (key == KEY_ESC)
        {
            ui_cursor(0);
            return 0;
        }
        if (key == KEY_ENTER || key == 10)
        {
            ui_cursor(0);
            return 1;
        }
        if (key == KEY_BACKSPACE || key == KEY_DELETE)
        {
            replace = 0;
            if (length > 0)
            {
                length--;
                value[length] = 0;
            }
        }
        else if (key >= 32 && key <= 126)
        {
            if (replace)
            {
                length = 0;
                value[0] = 0;
                replace = 0;
            }
            if (length < max_length)
            {
                value[length++] = key;
                value[length] = 0;
            }
        }
        ui_move(row, 21);
        printf("\033[7m%-40s\033[0m", value);
        ui_move(row, 21 + length);
        fflush(stdout);
    }
}

static int parse_decimal(char *value)
{
    int result;
    int i;

    if (!value[0])
        return -1;
    result = 0;
    for (i = 0; value[i]; i++)
    {
        if (value[i] < '0' || value[i] > '9')
            return -1;
        result = result * 10 + value[i] - '0';
    }
    return result;
}

static void ui_form_message(char *message)
{
    ui_clear_line(26);
    ui_move(26, 4);
    printf("\033[31m%s\033[0m", message);
    fflush(stdout);
}

static int ui_patient_form(int patient_id, int doctor_id, int adding)
{
    char detail[32];
    int age;

    if (adding)
    {
        form_first[0] = 0;
        form_last[0] = 0;
        form_address[0] = 0;
        form_age[0] = 0;
        strcpy(form_gender, "O");
        form_phone[0] = 0;
        strcpy(detail, "New patient");
    }
    else
    {
        get_field(form_first, patient_record, P_FIRST, 12);
        get_field(form_last, patient_record, P_LAST, 16);
        get_field(form_address, patient_record, P_ADDRESS, 36);
        format_number(form_age, read_number(patient_record, P_AGE, 3), 3);
        form_gender[0] = patient_record[P_GENDER];
        form_gender[1] = 0;
        get_field(form_phone, patient_record, P_PHONE, 12);
        strcpy(detail, "Patient ");
        format_number(&detail[8], patient_id, PATIENT_KEY_LEN);
    }

    ui_header(adding ? "ADD PATIENT" : "EDIT PATIENT", detail);
    ui_move(5, 4);
    printf("Doctor ID: %03d", doctor_id);
    ui_move(7, 4);
    printf("Enter patient details. Typing replaces the current field.");
    ui_footer("Enter next/save   Esc cancel");
    if (!ui_form_field(10, "First name", form_first, 12) ||
        !ui_form_field(12, "Last name", form_last, 16) ||
        !ui_form_field(14, "Address", form_address, 36) ||
        !ui_form_field(16, "Age", form_age, 3) ||
        !ui_form_field(18, "Gender M/F/O", form_gender, 1) ||
        !ui_form_field(20, "Phone", form_phone, 12))
        return 0;

    age = parse_decimal(form_age);
    if (!form_first[0] || !form_last[0])
    {
        ui_form_message("First and last name are required. Press any key.");
        ui_read_key();
        return FORM_INVALID;
    }
    if (age < 0 || age > 120)
    {
        ui_form_message("Age must be from 0 to 120. Press any key.");
        ui_read_key();
        return FORM_INVALID;
    }
    if (form_gender[0] >= 'a' && form_gender[0] <= 'z')
        form_gender[0] = form_gender[0] - ('a' - 'A');
    if (form_gender[0] != 'M' && form_gender[0] != 'F' &&
        form_gender[0] != 'O')
    {
        ui_form_message("Gender must be M, F or O. Press any key.");
        ui_read_key();
        return FORM_INVALID;
    }
    return age + 1;
}

static void write_patient_details(char *record, int age)
{
    put_field(record, P_FIRST, 12, form_first);
    put_field(record, P_LAST, 16, form_last);
    put_field(record, P_ADDRESS, 36, form_address);
    put_number(record, P_AGE, 3, age);
    record[P_GENDER] = form_gender[0];
    put_field(record, P_PHONE, 12, form_phone);
    write_patient_keys(record);
}

static int next_patient_id(void)
{
    struct i_cursor cursor;
    int patient_id;
    int rc;

    patient_id = 0;
    rc = cur_first(PATIENT_TABLE, &cursor, patient_record);
    while (rc >= 0)
    {
        patient_id = read_number(patient_record, P_ID, PATIENT_KEY_LEN);
        rc = cur_next(PATIENT_TABLE, &cursor, patient_record);
    }
    if (rc != I_ENREC || patient_id >= 99999)
        return I_ENREC;
    return patient_id + 1;
}

static int edit_patient(int patient_id)
{
    int age_result;
    int phys;

    phys = find_patient(patient_id);
    if (phys < 0)
        return phys;
    age_result = ui_patient_form(patient_id,
        read_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN), 0);
    if (age_result == 0)
        return FORM_CANCEL;
    if (age_result < 0)
        return age_result;
    write_patient_details(patient_record, age_result - 1);
    return i_wrphys(PATIENT_TABLE, patient_record, PATIENT_SIZE, phys);
}

static int add_patient(int doctor_id)
{
    int age_result;
    int patient_count;
    int patient_id;
    int patient_phys;
    int doctor_phys;
    int rc;

    doctor_phys = find_doctor(doctor_id);
    if (doctor_phys < 0)
        return doctor_phys;
    patient_count = read_number(doctor_record, D_PATIENT_COUNT, 3);
    if (patient_count >= MAX_DOCTOR_PATIENTS)
        return I_EUPDT;
    patient_id = next_patient_id();
    if (patient_id < 0)
        return patient_id;
    age_result = ui_patient_form(patient_id, doctor_id, 1);
    if (age_result == 0)
        return FORM_CANCEL;
    if (age_result < 0)
        return age_result;

    memset(patient_record, 0, PATIENT_SIZE);
    put_number(patient_record, P_ID, PATIENT_KEY_LEN, patient_id);
    put_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN, doctor_id);
    put_number(patient_record, P_HISTORY_COUNT, 3, 0);
    write_patient_details(patient_record, age_result - 1);
    rc = i_insrt(PATIENT_TABLE, patient_record, PATIENT_SIZE);
    if (rc != I_OK)
        return rc;

    doctor_phys = find_doctor(doctor_id);
    if (doctor_phys < 0)
        return doctor_phys;
    put_number(doctor_record, D_PATIENT_COUNT, 3, patient_count + 1);
    rc = i_wrphys(DOCTOR_TABLE, doctor_record, DOCTOR_SIZE, doctor_phys);
    if (rc != I_OK)
    {
        patient_phys = find_patient(patient_id);
        if (patient_phys >= 0)
            i_delphys(PATIENT_TABLE, patient_phys);
    }
    return rc;
}

static int ui_note_text_field(char *value)
{
    int cursor;
    int i;
    int key;
    int length;
    int row;

    length = strlen(value);
    cursor = length;
    for (;;)
    {
        for (row = 16; row <= 19; row++)
            ui_clear_line(row);
        ui_move(16, 4);
        printf("Clinical notes:");
        for (i = 0; i < length; i++)
        {
            if (i % H_NOTES_EDIT_WIDTH == 0)
                ui_move(17 + (i / H_NOTES_EDIT_WIDTH), 4);
            putchar(value[i]);
        }
        ui_move(20, 4);
        printf("%d/%d characters   Cursor Left/Right   Backspace/Delete edit",
            length, H_NOTES_SIZE);
        ui_move(17 + (cursor / H_NOTES_EDIT_WIDTH),
            4 + (cursor % H_NOTES_EDIT_WIDTH));
        ui_cursor(1);
        fflush(stdout);
        key = ui_read_key();
        if (key == KEY_ESC)
        {
            ui_cursor(0);
            return 0;
        }
        if (key == KEY_ENTER || key == 10)
        {
            ui_cursor(0);
            return 1;
        }
        if (key == KEY_LEFT)
        {
            if (cursor > 0)
                cursor--;
        }
        else if (key == KEY_RIGHT)
        {
            if (cursor < length)
                cursor++;
        }
        else if (key == KEY_BACKSPACE || key == KEY_DELETE)
        {
            if (cursor > 0)
            {
                memmove(&value[cursor - 1], &value[cursor],
                    length - cursor + 1);
                cursor--;
                length--;
            }
        }
        else if (key == KEY_FORWARD_DELETE)
        {
            if (cursor < length)
            {
                memmove(&value[cursor], &value[cursor + 1],
                    length - cursor);
                length--;
            }
        }
        else if (key >= 32 && key <= 126)
        {
            if (length < H_NOTES_SIZE)
            {
                memmove(&value[cursor + 1], &value[cursor],
                    length - cursor + 1);
                value[cursor++] = key;
                length++;
            }
        }
    }
}

static int valid_date(char *date)
{
    int day;
    int days;
    int i;
    int month;
    int year;

    for (i = 0; i < 8; i++)
        if (date[i] < '0' || date[i] > '9')
            return 0;
    if (date[8] != 0)
        return 0;
    year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
        (date[2] - '0') * 10 + date[3] - '0';
    month = (date[4] - '0') * 10 + date[5] - '0';
    day = (date[6] - '0') * 10 + date[7] - '0';
    if (year < 1900 || year > 2099 || month < 1 || month > 12)
        return 0;
    days = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11)
        days = 30;
    else if (month == 2)
        days = 28 + (year % 4 == 0 &&
            (year % 100 != 0 || year % 400 == 0));
    return day >= 1 && day <= days;
}

static int ui_date_field(int row, char *date)
{
    char digits[9];
    int key;
    int length;
    int replace;

    length = valid_date(date) ? 8 : 0;
    if (length)
    {
        memcpy(digits, &date[6], 2);
        memcpy(&digits[2], &date[4], 2);
        memcpy(&digits[4], date, 4);
    }
    digits[length] = 0;
    replace = length > 0;
    for (;;)
    {
        ui_move(row, 4);
        printf("%-15s", "Date DD/MM/YYYY");
        ui_move(row, 21);
        printf("\033[7m%c%c/%c%c/%c%c%c%c\033[0m",
            length > 0 ? digits[0] : '_', length > 1 ? digits[1] : '_',
            length > 2 ? digits[2] : '_', length > 3 ? digits[3] : '_',
            length > 4 ? digits[4] : '_', length > 5 ? digits[5] : '_',
            length > 6 ? digits[6] : '_', length > 7 ? digits[7] : '_');
        ui_move(row, 21 + length + (length > 1) + (length > 3));
        ui_cursor(1);
        fflush(stdout);
        key = ui_read_key();
        if (key == KEY_ESC)
        {
            ui_cursor(0);
            return 0;
        }
        if (key == KEY_ENTER || key == 10)
        {
            if (length == 8)
            {
                memcpy(date, &digits[4], 4);
                memcpy(&date[4], &digits[2], 2);
                memcpy(&date[6], digits, 2);
                date[8] = 0;
            }
            else
                date[0] = 0;
            if (valid_date(date))
            {
                ui_cursor(0);
                ui_clear_line(26);
                return 1;
            }
            ui_form_message("That is not a valid calendar date.");
            replace = 1;
        }
        if (key == KEY_BACKSPACE || key == KEY_DELETE)
        {
            replace = 0;
            if (length > 0)
                digits[--length] = 0;
            ui_clear_line(26);
        }
        else if (key >= '0' && key <= '9')
        {
            if (replace)
            {
                length = 0;
                replace = 0;
            }
            if (length < 8)
            {
                digits[length++] = key;
                digits[length] = 0;
            }
            ui_clear_line(26);
        }
    }
}

static int ui_note_form(int patient_id, int sequence, int adding)
{
    char detail[32];

    if (adding)
    {
        form_date[0] = 0;
        strcpy(form_type, "Consultation");
        form_notes[0] = 0;
        strcpy(detail, "New history note");
    }
    else
    {
        get_field(form_date, history_record, H_DATE, 8);
        get_field(form_type, history_record, H_TYPE, 12);
        get_field(form_notes, history_record, H_NOTES, H_NOTES_SIZE);
        strcpy(detail, "History note ");
        format_number(&detail[13], sequence, 3);
    }
    ui_header(adding ? "ADD NOTE" : "EDIT NOTE", detail);
    ui_move(5, 4);
    printf("Patient ID: %05d", patient_id);
    ui_move(7, 4);
    printf("Typing replaces the current field.");
    ui_footer("Enter next/save   Esc cancel");
    if (!ui_date_field(10, form_date) ||
        !ui_form_field(12, "Visit type", form_type, 12) ||
        !ui_note_text_field(form_notes))
        return FORM_CANCEL;
    if (!valid_date(form_date))
    {
        ui_form_message("Enter a valid date from 1900 to 2099. Press any key.");
        ui_read_key();
        return FORM_INVALID;
    }
    if (!form_type[0] || !form_notes[0])
    {
        ui_form_message("Visit type and clinical notes are required. Press any key.");
        ui_read_key();
        return FORM_INVALID;
    }
    return I_OK;
}

static void write_note_details(char *record)
{
    put_field(record, H_DATE, 8, form_date);
    put_field(record, H_TYPE, 12, form_type);
    put_field(record, H_NOTES, H_NOTES_SIZE, form_notes);
}

static int edit_note(int patient_id, int sequence)
{
    int phys;
    int rc;

    phys = find_history(patient_id, sequence);
    if (phys < 0)
        return phys;
    rc = ui_note_form(patient_id, sequence, 0);
    if (rc != I_OK)
        return rc;
    write_note_details(history_record);
    return i_wrphys(HISTORY_TABLE, history_record, HISTORY_SIZE, phys);
}

static int add_note(int patient_id)
{
    char key[HISTORY_KEY_LEN + 1];
    int doctor_id;
    int history_count;
    int history_phys;
    int patient_phys;
    int rc;

    patient_phys = find_patient(patient_id);
    if (patient_phys < 0)
        return patient_phys;
    doctor_id = read_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN);
    history_count = read_number(patient_record, P_HISTORY_COUNT, 3);
    if (history_count >= 999)
        return I_EUPDT;
    rc = ui_note_form(patient_id, history_count + 1, 1);
    if (rc != I_OK)
        return rc;
    memset(history_record, 0, HISTORY_SIZE);
    make_history_key(key, patient_id, history_count + 1);
    put_field(history_record, H_KEY, HISTORY_KEY_LEN, key);
    put_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN, doctor_id);
    write_note_details(history_record);
    rc = i_insrt(HISTORY_TABLE, history_record, HISTORY_SIZE);
    if (rc != I_OK)
        return rc;
    put_number(patient_record, P_HISTORY_COUNT, 3, history_count + 1);
    rc = i_wrphys(PATIENT_TABLE, patient_record, PATIENT_SIZE, patient_phys);
    if (rc != I_OK)
    {
        history_phys = find_history(patient_id, history_count + 1);
        if (history_phys >= 0)
            i_delphys(HISTORY_TABLE, history_phys);
    }
    return rc;
}

static int lower_char(int value)
{
    if (value >= 'A' && value <= 'Z')
        return value + ('a' - 'A');
    return value;
}

static int contains_text(char *value, char *query)
{
    int i;
    int j;

    if (!query[0])
        return 1;
    for (i = 0; value[i]; i++)
    {
        j = 0;
        while (query[j] && value[i + j] &&
               lower_char(value[i + j]) == lower_char(query[j]))
            j++;
        if (!query[j])
            return 1;
    }
    return 0;
}

static int search_doctor(char *query)
{
    struct i_cursor cursor;
    char first[13];
    char last[17];
    int rc;

    rc = cur_first(DOCTOR_TABLE, &cursor, doctor_record);
    while (rc >= 0)
    {
        get_field(first, doctor_record, D_FIRST, 12);
        get_field(last, doctor_record, D_LAST, 16);
        if (contains_text(first, query) || contains_text(last, query))
            return read_number(doctor_record, D_ID, DOCTOR_KEY_LEN);
        rc = cur_next(DOCTOR_TABLE, &cursor, doctor_record);
    }
    return I_ENREC;
}

static int add_patient_matches(int key_index, char *prefix, int length,
    int match_count)
{
    struct i_cursor cursor;
    int patient_id;
    int i;
    int rc;

    rc = idx_open(PATIENT_TABLE, key_index, prefix, length, &cursor,
        patient_record);
    while (rc >= 0)
    {
        patient_id = read_number(patient_record, P_ID, PATIENT_KEY_LEN);
        for (i = 0; i < match_count; i++)
            if (roster_ids[i] == patient_id)
                break;
        if (i == match_count && match_count < MAX_PATIENT_RESULTS)
            roster_ids[match_count++] = patient_id;
        rc = cur_next(PATIENT_TABLE, &cursor, patient_record);
    }
    if (rc != I_ENREC)
        return rc;
    return match_count;
}

static int search_patient(char *query)
{
    char prefix[11];
    int i;
    int length;
    int rc;

    length = strlen(query);
    if (length > 10)
        length = 10;
    for (i = 0; i < length; i++)
    {
        prefix[i] = query[i];
        if (prefix[i] >= 'a' && prefix[i] <= 'z')
            prefix[i] -= 'a' - 'A';
    }
    rc = add_patient_matches(1, prefix, length, 0);
    if (rc < 0)
        return rc;
    if (length > 7)
        length = 7;
    return add_patient_matches(2, prefix, length, rc);
}

static void ui_wrapped_text(char *text, int row, int column, int width)
{
    int line_length;
    int word_length;
    int word_start;
    int position;
    int i;

    if (width < 1)
        return;
    if (width > H_NOTES_EDIT_WIDTH)
        width = H_NOTES_EDIT_WIDTH;
    line_length = 0;
    position = 0;
    while (text[position])
    {
        while (text[position] == ' ')
            position++;
        if (!text[position])
            break;
        word_start = position;
        while (text[position] && text[position] != ' ')
            position++;
        word_length = position - word_start;
        if (line_length > 0 && line_length + word_length + 1 > width)
        {
            wrapped_line[line_length] = 0;
            ui_move(row, column);
            printf("%s", wrapped_line);
            row++;
            line_length = 0;
        }
        if (line_length > 0)
            wrapped_line[line_length++] = ' ';
        for (i = 0; i < word_length && line_length < width; i++)
            wrapped_line[line_length++] = text[word_start + i];
    }
    if (line_length > 0)
    {
        wrapped_line[line_length] = 0;
        ui_move(row, column);
        printf("%s", wrapped_line);
    }
}

static int ui_history_entry(int patient_id, int item_index, int top,
    int selected)
{
    char date[9];
    char type[13];
    char notes[H_NOTES_SIZE + 1];
    int row;
    int rc;

    rc = find_history(patient_id, item_index + 1);
    if (rc < 0)
        return rc;
    get_field(date, history_record, H_DATE, 8);
    get_field(type, history_record, H_TYPE, 12);
    get_field(notes, history_record, H_NOTES, H_NOTES_SIZE);
    row = 12 + ((item_index - top) * 5);
    ui_clear_line(row);
    ui_clear_line(row + 1);
    ui_clear_line(row + 2);
    ui_clear_line(row + 3);
    ui_clear_line(row + 4);
    ui_move(row, 3);
    if (item_index == selected)
        printf("\033[7m");
    printf("%s   %-12s", date, type);
    if (item_index == selected)
        printf("\033[0m");
    ui_wrapped_text(notes, row, 29, H_NOTES_WIDTH);
    return I_OK;
}

static int ui_history_page(int patient_id, int history_count, int top,
    int selected)
{
    int index;
    int row;
    int rc;

    for (row = 12; row <= 26; row++)
        ui_clear_line(row);
    for (index = 0; index < HISTORY_PAGE_SIZE && top + index < history_count;
         index++)
    {
        rc = ui_history_entry(patient_id, top + index, top, selected);
        if (rc != I_OK)
            return rc;
    }
    fflush(stdout);
    return I_OK;
}

static int ui_history(int patient_id)
{
    int doctor_id;
    int history_count;
    int selected;
    int old_selected;
    int top;
    int old_top;
    int rc;
    int key;
    int redraw;

    selected = 0;
    top = 0;
    redraw = 1;
    for (;;)
    {
        if (redraw)
        {
            rc = find_patient(patient_id);
            if (rc < 0)
                return rc;
            get_field(form_first, patient_record, P_FIRST, 12);
            get_field(form_last, patient_record, P_LAST, 16);
            get_field(form_address, patient_record, P_ADDRESS, 36);
            get_field(form_phone, patient_record, P_PHONE, 12);
            doctor_id = read_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN);
            history_count = read_number(patient_record, P_HISTORY_COUNT, 3);
            rc = find_doctor(doctor_id);
            if (rc < 0)
                return rc;
            get_field(history_doctor_first, doctor_record, D_FIRST, 12);
            get_field(history_doctor_last, doctor_record, D_LAST, 16);
            if (history_count == 0)
                selected = 0;
            else if (selected >= history_count)
                selected = history_count - 1;
            if (selected < top)
                top = selected;
            if (selected >= top + HISTORY_PAGE_SIZE)
                top = selected - HISTORY_PAGE_SIZE + 1;
            ui_header("PATIENT HISTORY", "Clinical record");
            ui_move(5, 3);
            printf("Patient %05d   \033[1m%s %s\033[0m",
                patient_id, form_first, form_last);
            ui_move(6, 3);
            printf("Age %-3d   Gender %c   Phone %s",
                read_number(patient_record, P_AGE, 3),
                patient_record[P_GENDER], form_phone);
            ui_move(7, 3);
            printf("Address: %s", form_address);
            ui_move(8, 3);
            printf("Registered doctor: Dr %s %s",
                history_doctor_first, history_doctor_last);
            ui_move(10, 3);
            printf("DATE       TYPE           CLINICAL NOTES");
            ui_move(11, 3);
            printf("--------------------------------------------------------------------------");
            rc = ui_history_page(patient_id, history_count, top, selected);
            if (rc != I_OK)
                return rc;
            ui_footer("A Add note   E Edit selected note");
            redraw = 0;
        }
        key = ui_read_key();
        if (key == KEY_ESC || key == 'q' || key == 'Q')
            return I_OK;
        old_selected = selected;
        old_top = top;
        if (key == KEY_UP && selected > 0)
            selected--;
        else if (key == KEY_DOWN && selected < history_count - 1)
            selected++;
        else if (key == 'e' || key == 'E')
        {
            if (history_count > 0)
            {
                rc = edit_note(patient_id, selected + 1);
                if (rc != I_OK && rc != FORM_INVALID && rc != FORM_CANCEL)
                    return rc;
                redraw = 1;
            }
        }
        else if (key == 'a' || key == 'A')
        {
            rc = add_note(patient_id);
            if (rc != I_OK && rc != FORM_INVALID && rc != FORM_CANCEL)
                return rc;
            if (rc == I_OK)
                selected = history_count;
            redraw = 1;
        }
        if (selected < top)
            top = selected;
        if (selected >= top + HISTORY_PAGE_SIZE)
            top = selected - HISTORY_PAGE_SIZE + 1;
        if (!redraw && selected != old_selected)
        {
            if (top != old_top)
                rc = ui_history_page(patient_id, history_count, top, selected);
            else
            {
                rc = ui_history_entry(patient_id, old_selected, top, selected);
                if (rc == I_OK)
                    rc = ui_history_entry(patient_id, selected, top, selected);
                fflush(stdout);
            }
            if (rc != I_OK)
                return rc;
        }
    }
}

static int ui_patient_search(void)
{
    char query[25];
    int match_count;

    if (!ui_prompt("Search patient name: ", query, 24))
        return I_OK;
    match_count = search_patient(query);
    if (match_count < 0 && match_count != I_ENREC)
        return match_count;
    if (match_count <= 0)
    {
        ui_clear_line(29);
        ui_move(29, 2);
        printf("No patient matching '%s'. Press any key.", query);
        fflush(stdout);
        ui_read_key();
        return I_OK;
    }
    return ui_patients(0, match_count, query);
}

static int load_roster(int doctor_id)
{
    struct i_cursor cursor;
    char prefix[DOCTOR_KEY_LEN + 1];
    int patient_count;
    int roster_count;
    int rc;

    rc = find_doctor(doctor_id);
    if (rc < 0)
        return rc;
    patient_count = read_number(doctor_record, D_PATIENT_COUNT, 3);
    if (patient_count < 0 || patient_count > MAX_DOCTOR_PATIENTS)
        return I_EVERIFY;
    format_number(prefix, doctor_id, DOCTOR_KEY_LEN);
    roster_count = 0;
    rc = idx_open(PATIENT_TABLE, 3, prefix, DOCTOR_KEY_LEN, &cursor,
        patient_record);
    while (rc >= 0 && roster_count < MAX_DOCTOR_PATIENTS)
    {
        roster_ids[roster_count++] = read_number(patient_record, P_ID,
            PATIENT_KEY_LEN);
        rc = cur_next(PATIENT_TABLE, &cursor, patient_record);
    }
    if (rc != I_ENREC || roster_count != patient_count)
        return I_EVERIFY;
    return roster_count;
}

static int ui_patient_row(int item_index, int top, int selected)
{
    char patient_first_name[13];
    char patient_last_name[17];
    int patient_id;
    int rc;

    patient_id = roster_ids[item_index];
    rc = find_patient(patient_id);
    if (rc < 0)
        return rc;
    get_field(patient_first_name, patient_record, P_FIRST, 12);
    get_field(patient_last_name, patient_record, P_LAST, 16);
    ui_clear_line(LIST_FIRST_ROW + item_index - top);
    ui_move(LIST_FIRST_ROW + item_index - top, 3);
    if (item_index == selected)
        printf("\033[7m");
    printf("%05d   %-11s %-15s   %3d   %c       %d visits",
        patient_id, patient_first_name, patient_last_name,
        read_number(patient_record, P_AGE, 3), patient_record[P_GENDER],
        read_number(patient_record, P_HISTORY_COUNT, 3));
    if (item_index == selected)
        printf("\033[0m");
    return I_OK;
}

static int ui_patient_page(int patient_count, int top, int selected)
{
    int index;
    int rc;

    for (index = 0; index < PATIENT_PAGE_SIZE; index++)
    {
        ui_clear_line(LIST_FIRST_ROW + index);
        if (top + index < patient_count)
        {
            rc = ui_patient_row(top + index, top, selected);
            if (rc != I_OK)
                return rc;
        }
    }
    fflush(stdout);
    return I_OK;
}

static int ui_patients(int doctor_id, int patient_count, char *search_query)
{
    char doctor_first_name[13];
    char doctor_last_name[17];
    char specialty[21];
    char detail[50];
    int selected;
    int old_selected;
    int top;
    int old_top;
    int key;
    int rc;
    int redraw;

    selected = 0;
    top = 0;
    redraw = 1;
    for (;;)
    {
        if (redraw)
        {
            if (doctor_id)
            {
                rc = find_doctor(doctor_id);
                if (rc < 0)
                    return rc;
                get_field(doctor_first_name, doctor_record, D_FIRST, 12);
                get_field(doctor_last_name, doctor_record, D_LAST, 16);
                get_field(specialty, doctor_record, D_SPECIALTY, 20);
                patient_count = load_roster(doctor_id);
                if (patient_count < 0)
                    return patient_count;
                strcpy(detail, "Dr ");
                strcat(detail, doctor_first_name);
                strcat(detail, " ");
                strcat(detail, doctor_last_name);
                ui_header("PATIENTS", detail);
            }
            else
                ui_header("PATIENT SEARCH", search_query);
            ui_move(4, 3);
            printf("ID      NAME                         AGE  SEX   HISTORY");
            rc = ui_patient_page(patient_count, top, selected);
            if (rc != I_OK)
                return rc;
            if (doctor_id)
                ui_footer("A Add patient   E Edit patient   / Search all patients");
            else
                ui_footer("Up/Down select   Enter open patient   Esc/Q return");
            redraw = 0;
        }
        key = ui_read_key();
        if (key == KEY_ESC || key == 'q' || key == 'Q')
            return I_OK;
        old_selected = selected;
        old_top = top;
        if (key == KEY_UP)
        {
            if (selected > 0)
                selected--;
        }
        else if (key == KEY_DOWN)
        {
            if (selected < patient_count - 1)
                selected++;
        }
        else if (key == KEY_ENTER || key == 10)
        {
            rc = ui_history(roster_ids[selected]);
            if (rc != I_OK)
                return rc;
            redraw = 1;
        }
        else if (doctor_id && (key == '/' || key == 'p' || key == 'P'))
        {
            rc = ui_patient_search();
            if (rc != I_OK)
                return rc;
            redraw = 1;
        }
        else if (doctor_id && (key == 'e' || key == 'E'))
        {
            rc = edit_patient(roster_ids[selected]);
            if (rc != I_OK && rc != FORM_INVALID && rc != FORM_CANCEL)
                return rc;
            redraw = 1;
        }
        else if (doctor_id && (key == 'a' || key == 'A'))
        {
            rc = add_patient(doctor_id);
            if (rc != I_OK && rc != FORM_INVALID && rc != FORM_CANCEL)
                return rc;
            if (rc == I_OK)
            {
                selected = patient_count;
                top = selected - PATIENT_PAGE_SIZE + 1;
                if (top < 0)
                    top = 0;
            }
            redraw = 1;
        }
        if (selected < top)
            top = selected;
        if (selected >= top + PATIENT_PAGE_SIZE)
            top = selected - PATIENT_PAGE_SIZE + 1;
        if (!redraw && selected != old_selected)
        {
            if (top != old_top)
            {
                rc = ui_patient_page(patient_count, top, selected);
                if (rc != I_OK)
                    return rc;
            }
            else
            {
                rc = ui_patient_row(old_selected, top, selected);
                if (rc == I_OK)
                    rc = ui_patient_row(selected, top, selected);
                if (rc != I_OK)
                    return rc;
                fflush(stdout);
            }
        }
    }
}

static int ui_doctor_row(int doctor_id, int selected)
{
    char first[13];
    char last[17];
    char specialty[21];
    int rc;

    rc = find_doctor(doctor_id);
    if (rc < 0)
        return rc;
    get_field(first, doctor_record, D_FIRST, 12);
    get_field(last, doctor_record, D_LAST, 16);
    get_field(specialty, doctor_record, D_SPECIALTY, 20);
    ui_clear_line(LIST_FIRST_ROW + doctor_id - 1);
    ui_move(LIST_FIRST_ROW + doctor_id - 1, 3);
    if (doctor_id == selected)
        printf("\033[7m");
    printf("%03d   Dr %-11s %-15s %-20s  %3d     %3d",
        doctor_id, first, last, specialty,
        read_number(doctor_record, D_ROOM, 3),
        read_number(doctor_record, D_PATIENT_COUNT, 3));
    if (doctor_id == selected)
        printf("\033[0m");
    return I_OK;
}

static void ui_isam_stats(void)
{
    struct i_stats stats;
    int key;

    for (;;)
    {
        iget_stats(&stats);
        ui_header("ISAM ACTIVITY", "Interactive session counters");
        ui_move(5, 3);
        printf("INDEX ACCESS\r\n\r\n"
            "    Exact key lookups       %5d\r\n"
            "    Index comparisons       %5d\r\n"
            "    Index cursor starts     %5d\r\n"
            "    Index cursor rows       %5d\r\n\r\n"
            "TABLE DATA\r\n\r\n"
            "    Physical records read   %5d\r\n"
            "    Table scan calls        %5d\r\n"
            "    Table slots scanned     %5d",
            stats.exact_lookups, stats.index_comparisons,
            stats.cursor_starts, stats.cursor_rows, stats.physical_reads,
            stats.table_scans, stats.scan_slots);
        ui_move(21, 3);
        if (stats.table_scans == 0)
            printf("STATUS: No table scans; access used indexes.");
        else
            printf("STATUS: Table scans occurred.");
        ui_footer("R Reset counters   Esc/Q return");
        key = ui_read_key();
        if (key == KEY_ESC || key == 'q' || key == 'Q')
            return;
        if (key == 'r' || key == 'R')
            istat_reset();
    }
}

static int ui_doctors(void)
{
    char query[25];
    int selected;
    int old_selected;
    int doctor_id;
    int key;
    int rc;
    int redraw;

    selected = 1;
    redraw = 1;
    for (;;)
    {
        if (redraw)
        {
            ui_header("DOCTORS", "Select a doctor to view registered patients");
            ui_move(4, 3);
            printf("ID    DOCTOR                         SPECIALTY              ROOM  PATIENTS");
            for (doctor_id = 1; doctor_id <= DOCTOR_COUNT; doctor_id++)
            {
                rc = ui_doctor_row(doctor_id, selected);
                if (rc != I_OK)
                    return rc;
            }
            ui_footer("/ Search doctors   P Search patients   D ISAM activity   Q Quit");
            redraw = 0;
        }
        key = ui_read_key();
        if (key == KEY_ESC || key == 'q' || key == 'Q')
            return I_OK;
        old_selected = selected;
        if (key == KEY_UP)
        {
            if (selected > 1)
                selected--;
        }
        else if (key == KEY_DOWN)
        {
            if (selected < DOCTOR_COUNT)
                selected++;
        }
        else if (key == KEY_ENTER || key == 10)
        {
            rc = ui_patients(selected, 0, 0);
            if (rc != I_OK)
                return rc;
            redraw = 1;
        }
        else if (key == '/')
        {
            if (ui_prompt("Search doctor name: ", query, 24))
            {
                doctor_id = search_doctor(query);
                if (doctor_id >= 0)
                    selected = doctor_id;
                else
                {
                    ui_clear_line(29);
                    ui_move(29, 2);
                    printf("No doctor matching '%s'. Press any key.", query);
                    fflush(stdout);
                    ui_read_key();
                }
            }
            redraw = 1;
        }
        else if (key == 'p' || key == 'P')
        {
            rc = ui_patient_search();
            if (rc != I_OK)
                return rc;
            redraw = 1;
        }
        else if (key == 'd' || key == 'D')
        {
            ui_isam_stats();
            redraw = 1;
        }
        if (!redraw && selected != old_selected)
        {
            rc = ui_doctor_row(old_selected, selected);
            if (rc == I_OK)
                rc = ui_doctor_row(selected, selected);
            if (rc != I_OK)
                return rc;
            fflush(stdout);
        }
    }
}

static int verify_database(void)
{
    struct i_cursor cursor;
    struct i_verify report;
    char *tables[3];
    int table_index;
    int patient_id;
    int doctor_id;
    int history_count;
    int sequence;
    int rc;

    tables[0] = DOCTOR_TABLE;
    tables[1] = PATIENT_TABLE;
    tables[2] = HISTORY_TABLE;
    puts("\r\nVERIFYING TABLES AND RELATIONSHIPS");
    for (table_index = 0; table_index < 3; table_index++)
    {
        rc = table_verify(tables[table_index], &report);
        printf("  %-8s records=%d index=%d deleted=%d errors=%d\r\n",
            tables[table_index], report.active_records, report.index_entries,
            report.deleted_records, report.errors);
        if (rc != I_OK)
            return rc;
    }
    rc = cur_first(PATIENT_TABLE, &cursor, patient_record);
    while (rc >= 0)
    {
        patient_id = read_number(patient_record, P_ID, PATIENT_KEY_LEN);
        doctor_id = read_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN);
        history_count = read_number(patient_record, P_HISTORY_COUNT, 3);
        rc = find_doctor(doctor_id);
        if (rc < 0)
            return rc;
        for (sequence = 1; sequence <= history_count; sequence++)
        {
            rc = find_history(patient_id, sequence);
            if (rc < 0)
                return rc;
            if (read_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN) != doctor_id)
                return I_EVERIFY;
        }
        rc = cur_next(PATIENT_TABLE, &cursor, patient_record);
    }
    if (rc != I_ENREC)
        return rc;
    for (doctor_id = 1; doctor_id <= DOCTOR_COUNT; doctor_id++)
    {
        rc = load_roster(doctor_id);
        if (rc < 0)
            return rc;
    }
    puts("  All relationships and roster links resolved through indexes");
    return I_OK;
}

int main(void)
{
    int rc;

    puts("\r\nDXISAM DOCTORS SURGERY");
    rc = i_cfrd(CFG_FILE);
    if (rc != I_OK)
    {
        printf("Cannot load %s rc=%d\r\n", CFG_FILE, rc);
        puts("Run DOCGEN once to generate the surgery database.");
        return 1;
    }
    if (g_cfg.tbls[1].recsz != PATIENT_SIZE ||
        g_cfg.tbls[1].nkeys != 4)
    {
        puts("Database schema is obsolete; run DOCGEN to rebuild it.");
        return 1;
    }
    rc = verify_database();
    if (rc == I_OK)
    {
        istat_reset();
        ui_cursor(0);
        rc = ui_doctors();
        ui_cursor(1);
        ui_clear();
    }
    if (rc != I_OK)
    {
        printf("Surgery demonstration failed rc=%d\r\n", rc);
        return 1;
    }
    printf("Riverside Doctors Surgery closed.\r\n");
    return 0;
}