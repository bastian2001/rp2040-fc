/**
 * MSP Guidelines, emphasis is used to clarify.
 *
 * Each FlightController (FC, Server) MUST change the API version when any MSP command is added, deleted, or changed.
 *
 * If you fork the FC source code and release your own version, you MUST change the Flight Controller Identifier.
 *
 * NEVER release a modified copy of this code that shares the same Flight controller IDENT and API version
 * if the API doesn't match EXACTLY.
 *
 * Consumers of the API (API clients) SHOULD first attempt to get a response from the MspFn::API_VERSION command.
 * If no response is obtained then client MAY try the legacy MSP_IDENT (100) command.
 *
 * API consumers should ALWAYS handle communication failures gracefully and attempt to continue
 * without the information if possible.  Clients MAY log/display a suitable message.
 *
 * API clients should NOT attempt any communication if they can't handle the returned API MAJOR VERSION.
 *
 * API clients SHOULD attempt communication if the API MINOR VERSION has increased from the time
 * the API client was written and handle command failures gracefully.  Clients MAY disable
 * functionality that depends on the commands while still leaving other functionality intact.
 * that the newer API version may cause problems before using API commands that change FC state.
 *
 * It is for this reason that each MSP command should be specific as possible, such that changes
 * to commands break as little functionality as possible.
 *
 * API client authors MAY use a compatibility matrix/table when determining if they can support
 * a given command from a given flight controller at a given api version level.
 *
 * Developers MUST NOT create new MSP commands that do more than one thing.
 *
 * Failure to follow these guidelines will likely invoke the wrath of developers trying to write tools
 * that use the API and the users of those tools.
 */

#pragma once
#include <Arduino.h>

extern bool configuratorConnected; // true if the configurator is connected
extern u8 accelCalDone; // accel calibration flag to send a message to the configurator

/**
 * @brief MSP Serial Functions
 *
 * @details These commands are used to communicate with the configurator and other peripherals. For Kolibri specific functions, the space 0x4000-0x4FFF is used. More details: https://github.com/iNavFlight/inav/wiki/MSP-V2
 */
enum class MspFn {
	API_VERSION = 1,
	FIRMWARE_VARIANT = 2,
	FIRMWARE_VERSION = 3,
	BOARD_INFO = 4,
	BUILD_INFO = 5,
	GET_NAME = 10,
	SET_NAME = 11,
	GET_FEATURE_CONFIG = 36,
	REBOOT = 68,
	GET_ADVANCED_CONFIG = 90,
	SET_ARMING_DISABLED = 99,
	MSP_STATUS = 101,
	GET_MOTOR = 104,
	RC = 105,
	MSP_ATTITUDE = 108,
	BOXIDS = 119,
	GET_MOTOR_3D_CONFIG = 124,
	GET_MOTOR_CONFIG = 131,
	UID = 160,
	ACC_CALIBRATION = 205,
	MAG_CALIBRATION = 206,
	SET_MOTOR = 214,
	ENABLE_4WAY_IF = 245,
	SET_RTC = 246,
	GET_RTC = 247,
	MSP_V2_FRAME = 255,

	// 0x400_ Configurator related commands
	STATUS = 0x4000,
	CONFIGURATOR_PING = 0x4001,
	IND_MESSAGE = 0x4002,

	// 0x401_ Entering special modes
	SERIAL_PASSTHROUGH = 0x4010,

	// 0x410_ Settings Meta commands
	SAVE_SETTINGS = 0x4100,

	// 0x411_ OSD settings
	WRITE_OSD_FONT_CHARACTER = 0x4110,

	// 0x412_ Blackbox
	GET_BB_SETTINGS = 0x4120,
	SET_BB_SETTINGS = 0x4121,
	BB_FILE_LIST = 0x4122,
	BB_FILE_INFO = 0x4123,
	BB_FILE_DOWNLOAD = 0x4124,
	BB_FILE_DELETE = 0x4125,
	BB_FORMAT = 0x4126,

	// 0x413_ GPS
	GET_GPS_STATUS = 0x4130,
	GET_GPS_ACCURACY = 0x4131,
	GET_GPS_TIME = 0x4132,
	GET_GPS_MOTION = 0x4133,

	// 0x414_ Magnetometer
	GET_MAG_DATA = 0x4140,

	// 0x415_ Gyro/Accel
	GET_ROTATION = 0x4150,

	// 0x416_ Barometer

	// 0x417_ Task Manager
	TASK_STATUS = 0x4170,

	// 0x418_ Receiver
	GET_RX_STATUS = 0x4180,

	// 0x41F_ Misc (not worth a category)
	GET_TZ_OFFSET = 0x41F0,
	SET_TZ_OFFSET = 0x41F1,

	// 0x42__ Tuning
	GET_PIDS = 0x4200,
	SET_PIDS = 0x4201,
	GET_RATES = 0x4202,
	SET_RATES = 0x4203,

	// 0x4F00-0x4F1F general debug tools
	GET_CRASH_DUMP = 0x4F00,
	CLEAR_CRASH_DUMP = 0x4F01,
	SET_DEBUG_LED = 0x4F02,
	PLAY_SOUND = 0x4F03,

	// 0x4F20-0x4FFF temporary debug tools
};

enum class MspState {
	IDLE, // waiting for $
	PACKET_START, // receiving M or X
	TYPE_V1, // got M, receiving type byte (<, >, !)
	LEN_V1, // if 255 is received in this step, inject jumbo len bytes
	JUMBO_LEN_LO_V1,
	JUMBO_LEN_HI_V1,
	CMD_V1,
	PAYLOAD_V1,
	FLAG_V2_OVER_V1,
	CMD_LO_V2_OVER_V1,
	CMD_HI_V2_OVER_V1,
	LEN_LO_V2_OVER_V1,
	LEN_HI_V2_OVER_V1,
	PAYLOAD_V2_OVER_V1,
	CHECKSUM_V2_OVER_V1,
	CHECKSUM_V1,
	TYPE_V2, // got X, receiving type byte (<, >, !)
	FLAG_V2,
	CMD_LO_V2,
	CMD_HI_V2,
	LEN_LO_V2,
	LEN_HI_V2,
	PAYLOAD_V2,
	CHECKSUM_V2,
};

#define MSP_PROTOCOL_VERSION 0
#define API_VERSION_MAJOR 3
#define API_VERSION_MINOR 0

#define KOLIBRI_IDENTIFIER "KOLI" // Baseflight: BAFL, Betaflight: BTFL, Cleanflight: CLFL, iNav: INAV, MultiWii: MWII, Raceflight: RCFL
#define FIRMWARE_IDENTIFIER_LENGTH 4

enum class McuType : u8 {
	SIMULATOR = 0,
	F40X,
	F411,
	F446,
	F722,
	F745,
	F746,
	F765,
	H750,
	H743_REV_UNKNOWN,
	H743_REV_Y,
	H743_REV_X,
	H743_REV_V,
	H7A3,
	H723_725,
	G474,
	H730,
	AT32,
	RP2040 = 64,
	UNKNOWN = 255,
};

enum class MspMsgType {
	REQUEST = '<',
	RESPONSE = '>',
	ERROR = '!',
};

enum class MspVersion {
	V2,
	V1,
	V1_JUMBO,
	V2_OVER_V1,
	V2_OVER_V1_JUMBO,

	V2_OVER_CRSF,
	V1_OVER_CRSF,
	V1_JUMBO_OVER_CRSF,
	V2_OVER_V1_OVER_CRSF,
	V2_OVER_V1_JUMBO_OVER_CRSF,
};

extern u8 lastMspSerial;
extern MspVersion lastMspVersion;

/**
 * @brief Send an MSP packet to the configurator
 *
 * @param serialNum serial port number to send the response to
 * @param type message type (request, response, error)
 * @param fn function to send
 * @param version MSP version to use
 * @param data data buffer, default is nullptr for no data
 * @param len length of the data buffer, default is 0
 */
void sendMsp(u8 serialNum, MspMsgType type, MspFn fn, MspVersion version, const char *data = nullptr, u16 len = 0);

/**
 * @brief Process a byte from the configurator
 *
 * @param c data byte
 * @param serialNum serial port number to send the response to
 */
void mspHandleByte(u8 c, u8 serialNum);

/// @brief counter for the motor override timeout
/// @details If the configurator is connected, it can override the motor values when this is < 1000, and it is possible to arm with an attached configurator
extern elapsedMillis mspOverrideMotors;

/// @brief handles configurator pings and asynchronous operations
void configuratorLoop();

void processMspCmd(u8 serialNum, MspMsgType mspType, MspFn fn, MspVersion version, const char *reqPayload, u16 reqLen);