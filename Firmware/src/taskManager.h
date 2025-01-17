#include "typedefs.h"
#include <Arduino.h>

typedef struct task {
	u32 runCounter; // incremented every time the task is run, reset every second
	u32 minDuration; // minimum duration of the task
	u32 maxDuration; // maximum duration of the task
	u32 frequency; // how often the task is run in the last second
	u32 avgDuration; // average duration of the task in the last second
	u32 errorCount; // how often the task has thrown an error since boot
	u32 lastError; // last error code (different for each task)
	u32 totalDuration; // total duration the task has taken since the reset every second
	u32 debugInfo; // debug info (different for each task)
	u32 maxGap; // maximum gap between two runs of the task (from end to start)
} FCTask;
extern volatile FCTask tasks[32]; // holds all the task stats

enum Tasks {
	TASK_LOOP0,
	TASK_SPEAKER,
	TASK_BAROEVAL,
	TASK_BLACKBOX,
	TASK_ELRS,
	TASK_MODES,
	TASK_ADC,
	TASK_SERIAL,
	TASK_CONFIGURATOR,
	TASK_GPS,
	TASK_MAGNETOMETER,
	TASK_TASKMANAGER,
	TASK_LOOP1,
	TASK_GYROREAD,
	TASK_IMU,
	TASK_IMU_GYRO,
	TASK_IMU_ACCEL,
	TASK_IMU_ANGLE,
	TASK_IMU_SPEEDS,
	TASK_PID_MOTORS,
	TASK_ESC_RPM,
	TASK_OSD,
	TASK_BAROREAD
};

/// @brief resets all task stats
void initTaskManager();

/// @brief checks if a second has passed and updates the task stats
void taskManagerLoop();
