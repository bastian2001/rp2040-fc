#include "FS.h"
#include "elapsedMillis.h"
#include "typedefs.h"
#include <Arduino.h>
#define LOG_ROLL_ELRS_RAW 1 << 0     // 2 bytes
#define LOG_PITCH_ELRS_RAW 1 << 1    // 2 bytes
#define LOG_THROTTLE_ELRS_RAW 1 << 2 // 2 bytes
#define LOG_YAW_ELRS_RAW 1 << 3      // 2 bytes
#define LOG_ROLL_SETPOINT 1 << 4     // 2 bytes
#define LOG_PITCH_SETPOINT 1 << 5    // 2 bytes
#define LOG_THROTTLE_SETPOINT 1 << 6 // 2 bytes
#define LOG_YAW_SETPOINT 1 << 7      // 2 bytes
#define LOG_ROLL_GYRO_RAW 1 << 8     // 2 bytes
#define LOG_PITCH_GYRO_RAW 1 << 9    // 2 bytes
#define LOG_YAW_GYRO_RAW 1 << 10     // 2 bytes
#define LOG_ROLL_PID_P 1 << 11       // 2 bytes
#define LOG_ROLL_PID_I 1 << 12       // 2 bytes
#define LOG_ROLL_PID_D 1 << 13       // 2 bytes
#define LOG_ROLL_PID_FF 1 << 14      // 2 bytes
#define LOG_ROLL_PID_S 1 << 15       // 2 bytes
#define LOG_PITCH_PID_P 1 << 16      // 2 bytes
#define LOG_PITCH_PID_I 1 << 17      // 2 bytes
#define LOG_PITCH_PID_D 1 << 18      // 2 bytes
#define LOG_PITCH_PID_FF 1 << 19     // 2 bytes
#define LOG_PITCH_PID_S 1 << 20      // 2 bytes
#define LOG_YAW_PID_P 1 << 21        // 2 bytes
#define LOG_YAW_PID_I 1 << 22        // 2 bytes
#define LOG_YAW_PID_D 1 << 23        // 2 bytes
#define LOG_YAW_PID_FF 1 << 24       // 2 bytes
#define LOG_YAW_PID_S 1 << 25        // 2 bytes
#define LOG_MOTOR_OUTPUTS 1 << 26    // 6 bytes
#define LOG_FRAMETIME 1 << 27        // 2 bytes
#define LOG_FLIGHT_MODE 1 << 28      // 1 byte
#define LOG_ALTITUDE 1 << 29         // 2 bytes
#define LOG_VVEL 1 << 30             // 2 bytes
#define LOG_GPS 1U << 31             // 2 bytes
#define LOG_ATT_ROLL 1LL << 32       // 2 bytes
#define LOG_ATT_PITCH 1LL << 33      // 2 bytes
#define LOG_ATT_YAW 1LL << 34        // 2 bytes
#define LOG_MOTOR_RPM 1LL << 35      // 6 bytes
#define LOG_ACCEL_RAW 1LL << 36      // 6 bytes
#define LOG_VERTICAL_ACCEL 1LL << 37 // 2 bytes

#define LOG_HEAD_MAGIC 0
#define LOG_HEAD_BB_VERSION 4
#define LOG_HEAD_TIMESTAMP 7
#define LOG_HEAD_PID_FREQ 11
#define LOG_HEAD_LOOP_DIV 12
#define LOG_HEAD_GYRO_ACCEL_RANGE 13
#define LOG_HEAD_RATE_FACTORS 14
#define LOG_HEAD_PID_GAINS 74
#define LOG_HEAD_LOGGED_FIELDS 158
#define LOG_HEAD_MOTOR_POLES 166
#define LOG_HEAD_LENGTH 256

extern u64 bbFlags;
extern volatile bool bbLogging, fsReady;
extern FSInfo64 fs_info;
extern elapsedMillis infoAge;
extern u8 bbFreqDivider;

void initBlackbox();

void startLogging();

void endLogging();

bool clearBlackbox();

void writeSingleFrame();

void printLogBin(u8 logNum, i16 chunkNum = -1);
void printLogBinRaw(u8 logNum);

void blackboxLoop();