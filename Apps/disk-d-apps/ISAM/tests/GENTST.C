#define main generator_main
#include "../docgen.c"
#undef main

#define CHECK(test, code) if (!(test)) return code

static int number(const char *record, int width)
{
    int value, i;
    value = 0;
    for (i = 0; i < width; ++i)
    {
        if (record[i] < '0' || record[i] > '9')
            return -1;
        value = value * 10 + record[i] - '0';
    }
    return value;
}

static int generated_records(void)
{
    int id, doctor, sequence, count, total, first, next;
    int buckets[HISTORY_MAX + 1];
    memset(buckets, 0, sizeof(buckets));
    total = 0;
    CHECK(DOCTOR_COUNT == 6 && PATIENT_COUNT == 300, 1);
    first = 1;
    for (doctor = 1; doctor <= DOCTOR_COUNT; ++doctor)
    {
        build_doctor(doctor);
        CHECK(number(doctor_record, DOCTOR_KEY_LEN) == doctor &&
            number(doctor_record + D_PATIENT_COUNT, 3) == 40 + 4 * (doctor - 1) &&
            number(doctor_record + D_FIRST_PATIENT, PATIENT_KEY_LEN) ==
                first &&
            number(doctor_record + D_ROOM, 3) == 100 + doctor &&
            !strcmp(doctor_record + D_FIRST, doctor_first[doctor - 1]) &&
            !strcmp(doctor_record + D_LAST, doctor_last[doctor - 1]) &&
            !strcmp(doctor_record + D_SPECIALTY, specialties[(doctor - 1) % 8]), 2);
        first += 40 + 4 * (doctor - 1);
    }
    CHECK(first == PATIENT_COUNT + 1, 2);
    doctor = 1;
    next = 41;
    for (id = 1; id <= PATIENT_COUNT; ++id)
    {
        build_patient(id);
        count = number(patient_record + P_HISTORY_COUNT, 3);
        if (id == next)
        {
            ++doctor;
            next += 40 + 4 * (doctor - 1);
        }
        CHECK(number(patient_record, PATIENT_KEY_LEN) == id &&
            number(patient_record + P_DOCTOR_ID, DOCTOR_KEY_LEN) == doctor &&
            count == patient_history_count(id) && count >= 5 && count <= 30, 3);
        ++buckets[count];
        for (sequence = 1; sequence <= count; ++sequence)
        {
            build_history(id, sequence);
            CHECK(number(history_record, PATIENT_KEY_LEN) == id &&
                number(history_record + PATIENT_KEY_LEN, 3) == sequence &&
                number(history_record + H_DOCTOR_ID, DOCTOR_KEY_LEN) == doctor &&
                number(history_record + H_DATE, 4) == 2023 + (sequence - 1) / 12 &&
                number(history_record + H_DATE + 4, 2) == 1 + (sequence - 1) % 12 &&
                number(history_record + H_DATE + 6, 2) == 1 + id % 28 &&
                !strncmp(history_record + H_TYPE,
                    visit_types[sequence == 1 ? 0 : 1 + sequence % 2], 12) &&
                history_record[H_NOTES], 4);
            ++total;
        }
    }
    CHECK(total == 1998 && total <= HISTORY_LIMIT, 5);
    for (count = 5; count <= 30; ++count)
        CHECK(buckets[count] ==
            (count <= 7 ? 96 : (count == 15 || count == 30 ? 6 : 0)), 6);
    puts("PASS: 6 doctors, 300 patients, 1998 history records; rosters 40-60, histories 5-30.");
    return I_OK;
}

int main(void)
{
    int rc;
    rc = generated_records();
    if (rc != I_OK)
        printf("FAIL: generator regression rc=%d\r\n", rc);
    return rc;
}
