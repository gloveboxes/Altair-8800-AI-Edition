#ifndef SURGERY_H
#define SURGERY_H

#define CFG_FILE "SURGERY.CFG"
#define DOCTOR_TABLE "DOCTORS"
#define PATIENT_TABLE "PATIENTS"
#define HISTORY_TABLE "HISTORY"

#define DOCTOR_SIZE 64
#define PATIENT_SIZE 91
#define HISTORY_SIZE 112
#ifndef DOCTOR_COUNT
#define DOCTOR_COUNT 6
#endif
#ifndef PATIENT_COUNT
#define PATIENT_COUNT 300
#endif
#ifndef PATIENT_MIN
#define PATIENT_MIN 40
#endif
#ifndef PATIENT_MAX
#define PATIENT_MAX 60
#endif
#ifndef HISTORY_MIN
#define HISTORY_MIN 5
#endif
#ifndef HISTORY_MAX
#define HISTORY_MAX 30
#endif
#define HISTORY_LIMIT 2040
#define DOCTOR_KEY_LEN 3
#define PATIENT_KEY_LEN 5
#define HISTORY_KEY_LEN 8

#define D_ID 0
#define D_FIRST 3
#define D_LAST 15
#define D_SPECIALTY 31
#define D_ROOM 51
#define D_FIRST_PATIENT 54
#define D_PATIENT_COUNT 59

#define P_ID 0
#define P_DOCTOR_ID 5
#define P_FIRST 8
#define P_LAST 20
#define P_ADDRESS 36
#define P_AGE 72
#define P_GENDER 75
#define P_PHONE 76
#define P_HISTORY_COUNT 88
#define P_LAST_KEY_SIZE 15
#define P_FIRST_KEY_SIZE 12
#define P_DOCTOR_KEY_SIZE 8

#define H_KEY 0
#define H_DOCTOR_ID 8
#define H_DATE 11
#define H_TYPE 19
#define H_NOTES 31
#define H_NOTES_SIZE (HISTORY_SIZE - H_NOTES)

#endif
