#include <Arduino.h>

/// @brief Positions of the different values in the EEPROM (4KiB max)
enum class EEPROM_POS : u16 {
	EEPROM_INIT = 0,
	EEPROM_VERSION = 1,
	UAV_NAME = 2,
	PID_GAINS = 22,
	RATE_FACTORS = 106,
	MOTOR_ORDER = 166, // 2 bits per motor: 7,6: fl, 5,4: rl, 3,2: fr, 1,0: rr
	PROP_DIRECTION = 167, // 1 = PROPS_OUT, -1 = PROPS_IN
	BATTERY_EMPTY_THRESHOLD = 168, // 2 bytes, centivolts
	UAV_TYPE = 169,
	BB_FLAGS = 170,
	BB_FREQ_DIVIDER = 178,
	ACCEL_CALIBRATION = 179, // 6 bytes: 2 bytes each for x, y, z
	MAG_CALIBRATION_HARD = 185, // two bytes per axis, 6 in total
	TIMEZONE_OFFSET_MINS = 191, // 2 bytes, e.g. 60 for GMT+1
};

/// @brief Write configuration values from EEPROM, write default if the EEPROM is not initialized
void readEEPROM();