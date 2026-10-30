#include "stdio.h"
#include "string.h"
#include "isam.h"
#include "surgery.h"

static char doctor_record[DOCTOR_SIZE];
static char patient_record[PATIENT_SIZE];
static char history_record[HISTORY_SIZE];
static char load_batch[DB_BULK_BYTES];
static int batch_count;
static int history_total;
static struct db_schema schema;

static char *doctor_first[] = {
    "Amelia", "Benjamin", "Clara", "Daniel",
    "Elena", "Farid", "Grace", "Hugo"
};
static char *doctor_last[] = {
    "Hart", "Okafor", "Singh", "Morgan",
    "Rossi", "Khan", "Chen", "Davies"
};
static char *specialties[8] = {
    "General Practice", "Cardiology", "Paediatrics", "Dermatology",
    "Women's Health", "Respiratory", "Diabetes", "Mental Health"
};
static char *first_names[20] = {
    "Alex", "Blair", "Casey", "Drew", "Elliot",
    "Finley", "Gale", "Harper", "Indigo", "Jordan",
    "Kai", "Logan", "Morgan", "Nico", "Oakley",
    "Peyton", "Quinn", "Riley", "Sawyer", "Taylor"
};
static char *last_names[20] = {
    "Anderson", "Bennett", "Carter", "Dalton", "Ellis",
    "Fletcher", "Garcia", "Hayes", "Iverson", "Jackson",
    "Knight", "Lawson", "Maddox", "Nolan", "Owens",
    "Prescott", "Quincy", "Ramsey", "Sawyer", "Thatcher"
};
static char *streets[10] = {
    "Maple Ave", "Oak Street", "Pine Road", "Cedar Lane", "Elm Drive",
    "Birch Way", "Spruce Court", "Willow Blvd", "Cherry Path", "Ash Terrace"
};
static char *visit_types[3] = {
    "Registration", "Consultation", "Follow-up"
};
static char *visit_notes[8] = {
    "Baseline observations recorded; no immediate concerns.",
    "Routine review completed; continue current care plan.",
    "Symptoms improving; advice given and follow-up arranged.",
    "Medication reviewed; dosage and precautions discussed.",
    "Blood pressure and pulse checked; results satisfactory.",
    "Test results reviewed; patient informed of findings.",
    "Lifestyle guidance provided; progress will be monitored.",
    "Minor symptoms assessed; return if condition changes."
};

static void put_field(char *record, int offset, int size, char *value)
{
    int i;

    for (i = 0; i < size; i++)
        record[offset + i] = value && *value ? *value++ : 0;
}

static void format_number(char *dest, int value, int width)
{
    int i;

    for (i = width - 1; i >= 0; i--)
    {
        dest[i] = '0' + (value % 10);
        value /= 10;
    }
    dest[width] = 0;
}

static void put_number(char *record, int offset, int width, int value)
{
    char number[10];

    format_number(number, value, width);
    put_field(record, offset, width, number);
}

static void make_history_key(char *key, int patient_id, int sequence)
{
    format_number(key, patient_id, PATIENT_KEY_LEN);
    format_number(&key[PATIENT_KEY_LEN], sequence, 3);
    key[HISTORY_KEY_LEN] = 0;
}

static void configure_table(int index, char *name, int record_size,
    int key_size)
{
    int i;

    strncpy(schema.table[index].name, name, I_MXNM);
    schema.table[index].size = record_size;
    schema.table[index].keys = 1;
    for (i = 0; i < I_MXKEY; i++)
    {
        schema.table[index].offsets[i] = 0;
        schema.table[index].widths[i] = 0;
    }
    schema.table[index].widths[0] = key_size;
}

static void init_config(void)
{
    memset(&schema, 0, sizeof(schema));
    strncpy(schema.name, "SURGERY", I_MXNM);
    schema.tables = 3;
    configure_table(0, DOCTOR_TABLE, DOCTOR_SIZE, DOCTOR_KEY_LEN);
    configure_table(1, PATIENT_TABLE, PATIENT_SIZE, PATIENT_KEY_LEN);
    schema.table[1].keys = 4;
    schema.table[1].offsets[1] = P_LAST | I_KEY_UPPER;
    schema.table[1].widths[1] = P_LAST_KEY_SIZE;
    schema.table[1].offsets[2] = P_FIRST | I_KEY_UPPER;
    schema.table[1].widths[2] = P_FIRST_KEY_SIZE;
    schema.table[1].offsets[3] = P_DOCTOR_ID;
    schema.table[1].widths[3] = P_DOCTOR_KEY_SIZE;
    schema.table[1].extra[1] = PATIENT_KEY_LEN;
    schema.table[1].extra[2] = PATIENT_KEY_LEN;
    schema.table[1].extra[3] = PATIENT_KEY_LEN;
    configure_table(2, HISTORY_TABLE, HISTORY_SIZE, HISTORY_KEY_LEN);
}

static int doctor_patient_count(int doctor_id)
{
    return PATIENT_MIN +
        (doctor_id - 1) * (PATIENT_MAX - PATIENT_MIN) / (DOCTOR_COUNT - 1);
}

static int first_doctor_patient(int doctor_id)
{
    int first, doctor;
    first = 1;
    for (doctor = 1; doctor < doctor_id; ++doctor)
        first += doctor_patient_count(doctor);
    return first;
}

static int patient_doctor(int patient_id)
{
    int doctor, count;
    for (doctor = 1; doctor < DOCTOR_COUNT; ++doctor)
    {
        count = doctor_patient_count(doctor);
        if (patient_id <= count)
            break;
        patient_id -= count;
    }
    return doctor;
}

static void build_doctor(int doctor_id)
{
    int first_patient;

    memset(doctor_record, 0, DOCTOR_SIZE);
    first_patient = first_doctor_patient(doctor_id);
    put_number(doctor_record, D_ID, DOCTOR_KEY_LEN, doctor_id);
    put_field(doctor_record, D_FIRST, 12, doctor_first[doctor_id - 1]);
    put_field(doctor_record, D_LAST, 16, doctor_last[doctor_id - 1]);
    put_field(doctor_record, D_SPECIALTY, 20, specialties[(doctor_id - 1) % 8]);
    put_number(doctor_record, D_ROOM, 3, 100 + doctor_id);
    put_number(doctor_record, D_FIRST_PATIENT, PATIENT_KEY_LEN, first_patient);
    put_number(doctor_record, D_PATIENT_COUNT, 3, doctor_patient_count(doctor_id));
}

static int patient_history_count(int patient_id)
{
#if HISTORY_MIN == HISTORY_MAX
    return HISTORY_MIN;
#else
    if (patient_id % 50 == 0)
        return HISTORY_MAX;
    if (patient_id % 25 == 0)
        return HISTORY_MIN + (HISTORY_MAX - HISTORY_MIN) * 2 / 5;
    return HISTORY_MIN + patient_id % 3;
#endif
}

static void build_patient(int patient_id)
{
    int doctor_id;
    char address[36];
    char house[6];
    char phone[13];

    memset(patient_record, 0, PATIENT_SIZE);
    memset(address, 0, 36);
    format_number(house, 10 + ((patient_id * 17) % 990), 3);
    strncpy(address, house, 35);
    strncat(address, " ", 35 - strlen(address));
    strncat(address, streets[(patient_id * 7) % 10], 35 - strlen(address));
    doctor_id = patient_doctor(patient_id);
    strcpy(phone, "555-");
    format_number(&phone[4], patient_id, PATIENT_KEY_LEN);
    put_number(patient_record, P_ID, PATIENT_KEY_LEN, patient_id);
    put_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN, doctor_id);
    put_field(patient_record, P_FIRST, 12, first_names[(patient_id - 1) % 20]);
    put_field(patient_record, P_LAST, 16,
        last_names[((patient_id - 1) * 3) % 20]);
    put_field(patient_record, P_ADDRESS, 36, address);
    put_number(patient_record, P_AGE, 3, 1 + ((patient_id * 11) % 99));
    patient_record[P_GENDER] = "FMO"[patient_id % 3];
    put_field(patient_record, P_PHONE, 12, phone);
    put_number(patient_record, P_HISTORY_COUNT, 3, patient_history_count(patient_id));
}

static void build_history(int patient_id, int sequence)
{
    int doctor_id;
    char key[HISTORY_KEY_LEN + 1];
    char date[9];

    memset(history_record, 0, HISTORY_SIZE);
    doctor_id = patient_doctor(patient_id);
    make_history_key(key, patient_id, sequence);
    format_number(date, 2023 + (sequence - 1) / 12, 4);
    format_number(&date[4], 1 + (sequence - 1) % 12, 2);
    format_number(&date[6], 1 + (patient_id % 28), 2);
    put_field(history_record, H_KEY, HISTORY_KEY_LEN, key);
    put_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN, doctor_id);
    put_field(history_record, H_DATE, 8, date);
    put_field(history_record, H_TYPE, 12,
        visit_types[sequence == 1 ? 0 : 1 + sequence % 2]);
    put_field(history_record, H_NOTES, H_NOTES_SIZE,
        visit_notes[(patient_id + sequence * 3) % 8]);
}

static int create_database(void)
{
    init_config();
    return db_new(CFG_FILE, &schema);
}

static int flush_records(int table, int size)
{
    int rc, completed;
    if (!batch_count)
        return I_OK;
    rc = db_append(table, load_batch, batch_count * size, &completed);
    if (rc != I_OK)
    {
        printf("Batch table %d stopped after %d of %d records rc=%d\r\n",
            table, completed, batch_count, rc);
        return rc;
    }
    batch_count = 0;
    return I_OK;
}

static int queue_record(int table, const char *record, int size)
{
    int rc;
    if (batch_count >= sizeof(load_batch) / size)
    {
        rc = flush_records(table, size);
        if (rc != I_OK)
            return rc;
    }
    memcpy(load_batch + batch_count * size, record, size);
    batch_count++;
    return I_OK;
}

static int load_synthetic_data(void)
{
    int doctor_id;
    int patient_id;
    int sequence;
    int rc;

    printf("Loading %d doctors...\r\n", DOCTOR_COUNT);
    rc = db_build(0);
    if (rc != I_OK) return rc;
    for (doctor_id = 1; doctor_id <= DOCTOR_COUNT; doctor_id++)
    {
        build_doctor(doctor_id);
        rc = queue_record(0, doctor_record, DOCTOR_SIZE);
        if (rc != I_OK)
            return rc;
    }
    rc = flush_records(0, DOCTOR_SIZE);
    if (rc == I_OK) rc = db_finish(0);
    if (rc != I_OK)
        return rc;
    printf("Loading %d patients...\r\n", PATIENT_COUNT);
    rc = db_build(1);
    if (rc != I_OK) return rc;
    for (patient_id = 1; patient_id <= PATIENT_COUNT; patient_id++)
    {
        build_patient(patient_id);
        rc = queue_record(1, patient_record, PATIENT_SIZE);
        if (rc != I_OK)
            return rc;
    }
    rc = flush_records(1, PATIENT_SIZE);
    if (rc == I_OK) rc = db_finish(1);
    if (rc != I_OK)
        return rc;
    printf("Loading %d history entries...\r\n", history_total);
    rc = db_build(2);
    if (rc != I_OK) return rc;
    for (patient_id = 1; patient_id <= PATIENT_COUNT; patient_id++)
    {
        for (sequence = 1; sequence <= patient_history_count(patient_id); sequence++)
        {
            build_history(patient_id, sequence);
            rc = queue_record(2, history_record, HISTORY_SIZE);
            if (rc != I_OK)
                return rc;
        }
        if (patient_id % 40 == 0)
            printf("  History generated for %d patients\r\n", patient_id);
    }
    rc = flush_records(2, HISTORY_SIZE);
    return rc == I_OK ? db_finish(2) : rc;
}

int main(void)
{
    int rc, patient_id;

    puts("\r\nSURGERY DATABASE GENERATOR - Banked ISAM Library");
    history_total = 0;
    for (patient_id = 1; patient_id <= PATIENT_COUNT; ++patient_id)
        history_total += patient_history_count(patient_id);
    if (history_total > HISTORY_LIMIT)
    {
        puts("History record limit exceeded; existing database was not changed.");
        return 1;
    }
    puts("Ensure sufficient disk space before generation; existing data is replaced.");
    puts("Creating DOCTORS, PATIENTS and HISTORY tables...");
    rc = create_database();
    if (rc == I_OK)
        rc = load_synthetic_data();
    if (rc != I_OK)
    {
        printf("Database generation failed rc=%d\r\n", rc);
        if (rc == I_EWRIT)
            puts("Write failed; check free disk space before running DOCGEN again.");
        db_close();
        return 1;
    }
    printf("Generated %d doctors, %d patients and %d history records.\r\n",
        DOCTOR_COUNT, PATIENT_COUNT, history_total);
    return db_close() == I_OK ? 0 : 1;
}
