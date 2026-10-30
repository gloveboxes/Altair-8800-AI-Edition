#include "FIXTURE.H"
#define main doctor_main
#include "../doctor.c"
#undef main

#define CHECK(test, code) if (!(test)) return code

static int scalar_relationships(void)
{
    struct db_scan scan;
    char *patient;
    int rc, status, row, patient_id, doctor_id, count, sequence;
    rc = db_scan(patient_table, 0, 0, 0, &scan, scan_rows, sizeof(scan_rows), DB_RECORDS);
    for (;;)
    {
        if (rc != I_OK && rc != I_ENREC)
            return rc;
        status = rc;
        for (row = 0; row < scan.count; ++row)
        {
            patient = scan_rows + row * PATIENT_SIZE;
            patient_id = read_number(patient, P_ID, PATIENT_KEY_LEN);
            doctor_id = read_number(patient, P_DOCTOR_ID, DOCTOR_KEY_LEN);
            count = read_number(patient, P_HISTORY_COUNT, 3);
            rc = find_doctor(doctor_id);
            if (rc < 0)
                return rc;
            for (sequence = 1; sequence <= count; ++sequence)
            {
                rc = find_history(patient_id, sequence);
                if (rc < 0)
                    return rc;
                if (read_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN) != doctor_id)
                    return I_EVERIFY;
            }
        }
        if (status == I_ENREC)
            return I_OK;
        rc = db_more(&scan, scan_rows, sizeof(scan_rows));
    }
}

static int page_checks(void)
{
    struct i_stats stats;
    int i, count, rc, slot;
    list_rows = malloc(UI_WORK_BYTES + 1);
    CHECK(list_rows, 1);
    CHECK(db_open(CFG_FILE) == I_OK, 2);
    doctor_table = db_table(DOCTOR_TABLE);
    patient_table = db_table(PATIENT_TABLE);
    history_table = db_table(HISTORY_TABLE);
    CHECK(doctor_table >= 0 && patient_table >= 0 && history_table >= 0, 3);
    doctor_scan.cursor.entry_count = -1;
    db_reset();
    CHECK(load_doctor_page(0) == I_OK && doctor_count == DOCTOR_COUNT, 4);
    db_stats(&stats);
    CHECK(stats.physical_reads == DOCTOR_PAGE_SIZE &&
        stats.cursor_rows == DOCTOR_PAGE_SIZE && !stats.table_scans, 4);
    CHECK(
        load_roster(1) == 30, 4);
    CHECK(load_patient_page(30, 0) == I_OK, 5);
    for (i = 0; i < PATIENT_PAGE_SIZE; ++i)
    {
        CHECK(find_patient(roster_ids[i]) >= 0, 5);
        CHECK(!memcmp(list_rows + i * sizeof(struct patient_row), patient_record, 5) &&
            !memcmp(list_rows + i * sizeof(struct patient_row) + 5, patient_record + P_FIRST, 28) &&
            !memcmp(list_rows + i * sizeof(struct patient_row) + 33, patient_record + P_AGE, 4) &&
            !memcmp(list_rows + i * sizeof(struct patient_row) + 37, patient_record + P_HISTORY_COUNT, 3), 5);
    }
    db_reset();
    outp(5, 40);
    for (i = 0; i < PATIENT_PAGE_SIZE; ++i)
        CHECK(find_patient(roster_ids[i]) >= 0, 6);
    outp(5, 168);
    db_stats(&stats);
    CHECK(stats.exact_lookups == 20 && stats.physical_reads == 20, 7);
    CHECK(load_patient_page(30, 0) == I_OK, 7);
    db_reset();
    outp(5, 42);
    rc = db_fetch(patient_table, roster_slots, PATIENT_PAGE_SIZE, 0, list_rows,
        PATIENT_PAGE_SIZE * PATIENT_SIZE);
    outp(5, 170);
    CHECK(rc == I_OK, 7);
    db_reset();
    outp(5, 41);
    rc = load_patient_page(30, 0);
    outp(5, 169);
    CHECK(rc == I_OK, 8);
    db_stats(&stats);
    CHECK(!stats.exact_lookups && stats.physical_reads == 20, 9);
    db_reset();
    for (i = 0; i < PATIENT_PAGE_SIZE; ++i)
        CHECK(ui_patient_row(i, 0, 0) == I_OK, 10);
    CHECK(ui_patient_row(0, 0, 1) == I_OK && ui_patient_row(1, 0, 1) == I_OK, 11);
    db_stats(&stats);
    CHECK(!stats.physical_reads && !stats.exact_lookups && !stats.cursor_rows, 12);
    CHECK(load_patient_page(30, 10) == I_OK &&
        read_number(list_rows, 0, 5) == 11 &&
        read_number(list_rows + 19 * sizeof(struct patient_row), 0, 5) == 30, 13);
    CHECK(load_patient_page(30, 20) == I_OK &&
        read_number(list_rows, 0, 5) == 21, 14);

    count = search_patient("A");
    CHECK(count > 0 && load_patient_page(count, 0) == I_OK, 15);
    for (i = 0; i < count && i < PATIENT_PAGE_SIZE; ++i)
        CHECK(read_number(list_rows + i * sizeof(struct patient_row), 0, 5) == roster_ids[i], 16);
    CHECK(load_history(1, 3) == I_OK && load_history_page(1, 3, 0) == I_OK, 17);
    db_reset();
    CHECK(ui_history_entry(0, 0, 1) == I_OK &&
        ui_history_entry(1, 0, 1) == I_OK, 18);
    db_stats(&stats);
    CHECK(!stats.physical_reads && !stats.exact_lookups && !stats.cursor_rows, 19);
    memcpy(history_record, list_rows, HISTORY_SIZE);
    make_history_key(history_record, 1, 4);
    put_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN, 1);
    CHECK(db_add(history_table, history_record, HISTORY_SIZE) == I_OK &&
        load_history(1, 4) == I_OK && load_history_page(1, 4, 1) == I_OK &&
        read_number(list_rows, PATIENT_KEY_LEN, 3) == 2 &&
        read_number(list_rows + 2 * HISTORY_SIZE, PATIENT_KEY_LEN, 3) == 4, 19);
    CHECK(load_roster(1) == 30 && load_patient_page(30, 0) == I_OK &&
        read_number(list_rows, 0, 5) == 1, 20);
    slot = find_patient(1);
    CHECK(slot >= 0, 21);
    patient_record[P_AGE] = '9';
    patient_record[P_AGE + 1] = '8';
    patient_record[P_AGE + 2] = '7';
    put_number(patient_record, P_HISTORY_COUNT, 3, 4);
    CHECK(db_put(patient_table, slot, patient_record, PATIENT_SIZE) == I_OK &&
        load_patient_page(30, 0) == I_OK &&
        read_number(((struct patient_row *)list_rows)->age, 0, 3) == 987 &&
        read_number(((struct patient_row *)list_rows)->visits, 0, 3) == 4, 22);
    CHECK(load_doctor_page(0) == I_OK, 23);
    db_reset();
    CHECK(load_doctor_page(DOCTOR_PAGE_SIZE) == I_OK &&
        read_number(list_rows, D_ID, DOCTOR_KEY_LEN) == DOCTOR_PAGE_SIZE + 1 &&
        read_number(list_rows + (DOCTOR_COUNT - DOCTOR_PAGE_SIZE - 1) * DOCTOR_SIZE,
            D_ID, DOCTOR_KEY_LEN) == DOCTOR_COUNT, 23);
    db_stats(&stats);
    CHECK(stats.physical_reads == DOCTOR_COUNT - DOCTOR_PAGE_SIZE &&
        stats.cursor_rows == DOCTOR_COUNT - DOCTOR_PAGE_SIZE &&
        !stats.cursor_starts && !stats.index_comparisons &&
        !stats.table_scans, 23);
    CHECK(
        load_doctor_page(DOCTOR_COUNT) == I_EVERIFY &&
        load_doctor_page(0) == I_OK, 23);
    db_reset();
    CHECK(ui_doctor_row(1, 0, 2) == I_OK && ui_doctor_row(2, 0, 2) == I_OK, 24);
    db_stats(&stats);
    CHECK(!stats.physical_reads && !stats.exact_lookups && !stats.cursor_rows, 25);
    db_reset();
    outp(5, 43);
    rc = scalar_relationships();
    outp(5, 171);
    CHECK(rc == I_OK, 27);
    db_stats(&stats);
    CHECK(stats.exact_lookups == 961, 28);
    db_reset();
    outp(5, 44);
    rc = verify_relationships();
    outp(5, 172);
    if (rc != I_OK)
        printf("Relationship verification rc=%d\r\n", rc);
    CHECK(rc == I_OK, 29);
    db_stats(&stats);
    CHECK(stats.exact_lookups == 721, 30);
    CHECK(find_doctor(1) >= 0, 41);
    put_number(doctor_record, D_PATIENT_COUNT, 3, 0);
    put_number(doctor_record, D_FIRST_PATIENT, PATIENT_KEY_LEN, 0);
    for (i = DOCTOR_COUNT + 1; i <= 45; ++i)
    {
        put_number(doctor_record, D_ID, DOCTOR_KEY_LEN, i);
        CHECK(db_add(doctor_table, doctor_record, DOCTOR_SIZE) == I_OK, 41);
    }
    doctor_scan.cursor.entry_count = -1;
    CHECK(load_doctor_page(0) == I_OK && doctor_count == 45, 42);
    db_reset();
    CHECK(load_doctor_page(40) == I_OK &&
        read_number(list_rows, D_ID, DOCTOR_KEY_LEN) == 41 &&
        read_number(list_rows + 4 * DOCTOR_SIZE, D_ID, DOCTOR_KEY_LEN) == 45, 42);
    db_stats(&stats);
    CHECK(stats.physical_reads == 5 && stats.cursor_rows == 5 &&
        !stats.cursor_starts && !stats.index_comparisons &&
        !stats.table_scans, 42);
    CHECK(verify_database() == I_OK && doctor_count == 45, 43);
    puts("PASS: 45-doctor directory pages directly and verifies beyond generator count.");
    slot = find_history(1, 1);
    CHECK(slot >= 0, 31);
    put_number(history_record, H_DOCTOR_ID, DOCTOR_KEY_LEN, 2);
    CHECK(db_put(history_table, slot, history_record, HISTORY_SIZE) == I_OK &&
        verify_relationships() == I_EVERIFY, 32);
    CHECK(db_del(history_table, slot) == I_OK && verify_relationships() == I_ENREC, 33);
    slot = find_patient(1);
    CHECK(slot >= 0, 34);
    put_number(patient_record, P_DOCTOR_ID, DOCTOR_KEY_LEN, 999);
    CHECK(db_put(patient_table, slot, patient_record, PATIENT_SIZE) == I_OK &&
        verify_relationships() == I_ENREC, 35);
    puts("PASS: actual UI loaders batch records; render and highlight use local memory.");
    puts("PASS: batched relationship verification detects wrong, missing and dangling keys.");
    return I_OK;
}

static int history_workspace_checks(void)
{
    static struct db_schema schema;
    int sequence, row, count, completed;
    CHECK(db_close() == I_OK, 36);
    memset(&schema, 0, sizeof(schema));
    strcpy(schema.name, "HMAXTST");
    schema.tables = 1;
    strcpy(schema.table[0].name, "HMAXTST");
    schema.table[0].size = HISTORY_SIZE;
    schema.table[0].keys = 1;
    schema.table[0].widths[0] = HISTORY_KEY_LEN;
    CHECK(db_new("HMAXTST.CFG", &schema) == I_OK && db_build(0) == I_OK, 37);
    history_table = 0;
    list_rows[UI_WORK_BYTES] = 0x55;
    for (sequence = 1; sequence <= 999; sequence += count)
    {
        count = 1000 - sequence;
        if (count > UI_WORK_BYTES / HISTORY_SIZE)
            count = UI_WORK_BYTES / HISTORY_SIZE;
        memset(list_rows, 0, count * HISTORY_SIZE);
        for (row = 0; row < count; ++row)
            make_history_key(list_rows + row * HISTORY_SIZE, 1, sequence + row);
        CHECK(db_append(0, list_rows, count * HISTORY_SIZE, &completed) == I_OK &&
            completed == count, 38);
    }
    CHECK(db_finish(0) == I_OK && load_history(1, 999) == I_OK &&
        load_history_page(1, 999, 996) == I_OK, 39);
    memset(history_record, 0, HISTORY_SIZE);
    memcpy(history_record, "00001XYZ", HISTORY_KEY_LEN);
    CHECK(db_add(0, history_record, HISTORY_SIZE) == I_OK &&
        load_history(1, 999) == I_EVERIFY &&
        load_history(1, 1000) == I_EVERIFY &&
        list_rows[UI_WORK_BYTES] == 0x55, 40);
    puts("PASS: 2560-byte workspace holds 999 visits and rejects excess rows.");
    return I_OK;
}

int main(void)
{
    int rc;
    rc = page_checks();
    if (rc == I_OK)
        rc = history_workspace_checks();
    free(list_rows);
    if (db_close() != I_OK && !rc)
        rc = 26;
    if (rc)
        printf("FAIL: UI batch regression rc=%d\r\n", rc);
    return rc;
}
