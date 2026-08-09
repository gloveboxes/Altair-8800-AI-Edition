#include "stdio.h"
#include "string.h"
#include "ISAMDB.H"
#include "SURGERY.H"

static char doctor_record[DOCTOR_SIZE];
static char patient_record[PATIENT_SIZE];
static char history_record[HISTORY_SIZE];

static char *doctor_first[DOCTOR_COUNT] = {
    "Amelia", "Benjamin", "Clara", "Daniel",
    "Elena", "Farid", "Grace", "Hugo"
};
static char *doctor_last[DOCTOR_COUNT] = {
    "Hart", "Okafor", "Singh", "Morgan",
    "Rossi", "Khan", "Chen", "Davies"
};
static char *specialties[DOCTOR_COUNT] = {
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
        record[offset + i] = value && value[i] ? value[i] : 0;
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

static void configure_table(int index, char *name, int record_size,
    int key_size)
{
    int i;

    strncpy(g_cfg.tbls[index].name, name, I_MXNM);
    g_cfg.tbls[index].disk = 'C';
    g_cfg.tbls[index].recsz = record_size;
    g_cfg.tbls[index].nkeys = 1;
    for (i = 0; i < I_MXKEY; i++)
    {
        g_cfg.tbls[index].keyoff[i] = 0;
        g_cfg.tbls[index].keysz[i] = 0;
    }
    g_cfg.tbls[index].keysz[0] = key_size;
}

static void init_config(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    strncpy(g_cfg.dbname, "SURGERY", I_MXNM);
    g_cfg.ntbls = 3;
    configure_table(0, DOCTOR_TABLE, DOCTOR_SIZE, DOCTOR_KEY_LEN);
    configure_table(1, PATIENT_TABLE, PATIENT_SIZE, PATIENT_KEY_LEN);
    g_cfg.tbls[1].nkeys = 4;
    g_cfg.tbls[1].keyoff[1] = P_LAST_KEY;
    g_cfg.tbls[1].keysz[1] = P_LAST_KEY_SIZE;
    g_cfg.tbls[1].keyoff[2] = P_FIRST_KEY;
    g_cfg.tbls[1].keysz[2] = P_FIRST_KEY_SIZE;
    g_cfg.tbls[1].keyoff[3] = P_DOCTOR_KEY;
    g_cfg.tbls[1].keysz[3] = P_DOCTOR_KEY_SIZE;
    configure_table(2, HISTORY_TABLE, HISTORY_SIZE, HISTORY_KEY_LEN);
}

static void build_doctor(int doctor_id)
{
    int first_patient;

    memset(doctor_record, 0, DOCTOR_SIZE);
    first_patient = (doctor_id - 1) * PATIENTS_PER_DOCTOR + 1;
    put_number(doctor_record, D_ID, DOCTOR_KEY_LEN, doctor_id);
    put_field(doctor_record, D_FIRST, 12, doctor_first[doctor_id - 1]);
    put_field(doctor_record, D_LAST, 16, doctor_last[doctor_id - 1]);
    put_field(doctor_record, D_SPECIALTY, 20, specialties[doctor_id - 1]);
    put_number(doctor_record, D_ROOM, 3, 100 + doctor_id);
    put_number(doctor_record, D_FIRST_PATIENT, PATIENT_KEY_LEN, first_patient);
    put_number(doctor_record, D_PATIENT_COUNT, 3, PATIENTS_PER_DOCTOR);
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
    doctor_id = ((patient_id - 1) / PATIENTS_PER_DOCTOR) + 1;
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
    put_number(patient_record, P_HISTORY_COUNT, 3, HISTORY_PER_PATIENT);
    write_patient_keys(patient_record);
}

static void build_history(int patient_id, int sequence)
{
    int doctor_id;
    char key[HISTORY_KEY_LEN + 1];
    char date[9];

    memset(history_record, 0, HISTORY_SIZE);
    doctor_id = ((patient_id - 1) / PATIENTS_PER_DOCTOR) + 1;
    make_history_key(key, patient_id, sequence);
    strcpy(date, "2023");
    format_number(&date[4], sequence, 2);
    format_number(&date[6], 1 + (patient_id % 28), 2);
    put_field(history_record, H_KEY, HISTORY_KEY_LEN, key);
    put_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN, doctor_id);
    put_field(history_record, H_DATE, 8, date);
    put_field(history_record, H_TYPE, 12, visit_types[sequence - 1]);
    put_field(history_record, H_NOTES, H_NOTES_SIZE,
        visit_notes[(patient_id + sequence * 3) % 8]);
}

static int create_database(void)
{
    int rc;

    init_config();
    rc = i_cfwr(CFG_FILE);
    if (rc == I_OK)
        rc = i_mktbl(DOCTOR_TABLE);
    if (rc == I_OK)
        rc = i_mktbl(PATIENT_TABLE);
    if (rc == I_OK)
        rc = i_mktbl(HISTORY_TABLE);
    return rc;
}

static int load_synthetic_data(void)
{
    int doctor_id;
    int patient_id;
    int sequence;
    int rc;

    printf("Loading %d doctors...\r\n", DOCTOR_COUNT);
    for (doctor_id = 1; doctor_id <= DOCTOR_COUNT; doctor_id++)
    {
        build_doctor(doctor_id);
        rc = i_insrt(DOCTOR_TABLE, doctor_record, DOCTOR_SIZE);
        if (rc != I_OK)
            return rc;
    }
    printf("Loading %d patients...\r\n", PATIENT_COUNT);
    for (patient_id = 1; patient_id <= PATIENT_COUNT; patient_id++)
    {
        build_patient(patient_id);
        rc = i_insrt(PATIENT_TABLE, patient_record, PATIENT_SIZE);
        if (rc != I_OK)
            return rc;
    }
    printf("Loading %d history entries...\r\n",
        PATIENT_COUNT * HISTORY_PER_PATIENT);
    for (patient_id = 1; patient_id <= PATIENT_COUNT; patient_id++)
    {
        for (sequence = 1; sequence <= HISTORY_PER_PATIENT; sequence++)
        {
            build_history(patient_id, sequence);
            rc = i_insrt(HISTORY_TABLE, history_record, HISTORY_SIZE);
            if (rc != I_OK)
                return rc;
        }
        if (patient_id % 40 == 0)
            printf("  History generated for %d patients\r\n", patient_id);
    }
    return i_cfwr(CFG_FILE);
}

int main(void)
{
    int rc;

    puts("\r\nDXISAM SURGERY DATABASE GENERATOR");
    puts("Creating DOCTORS, PATIENTS and HISTORY tables...");
    rc = create_database();
    if (rc == I_OK)
        rc = load_synthetic_data();
    if (rc != I_OK)
    {
        printf("Database generation failed rc=%d\r\n", rc);
        return 1;
    }
    printf("Generated %d doctors, %d patients and %d history records.\r\n",
        DOCTOR_COUNT, PATIENT_COUNT, PATIENT_COUNT * HISTORY_PER_PATIENT);
    return 0;
}
