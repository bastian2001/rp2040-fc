#include "global.h"

#define SIGNATURE_LENGTH 32
#define TARGET_IDENTIFIER_LENGTH 4

u8 accelCalDone = 0;

elapsedMillis mspOverrideMotors = 1001;

static const char targetIdentifier[] = "KD04";
static const char targetFullName[] = "Kolibri Dev v0.4";

elapsedMillis lastConfigPingRx = 0;
bool configuratorConnected = false;

u8 lastMspSerial = 0;
MspVersion lastMspVersion = MspVersion::V2;

void configuratorLoop() {
	if (lastConfigPingRx > 1000)
		configuratorConnected = false;
	if (accelCalDone) {
		accelCalDone = 0;
		char data = 1;
		sendMsp(lastMspSerial, MspMsgType::RESPONSE, MspFn::ACC_CALIBRATION, lastMspVersion, &data, 1);
		EEPROM.put((u16)EEPROM_POS::ACCEL_CALIBRATION, (i16)accelCalibrationOffset[0]);
		EEPROM.put((u16)EEPROM_POS::ACCEL_CALIBRATION + 2, (i16)accelCalibrationOffset[1]);
		EEPROM.put((u16)EEPROM_POS::ACCEL_CALIBRATION + 4, (i16)accelCalibrationOffset[2]);
		EEPROM.commit();
		sendMsp(lastMspSerial, MspMsgType::RESPONSE, MspFn::SAVE_SETTINGS, lastMspVersion);
	}
}

void sendMsp(u8 serialNum, MspMsgType type, MspFn fn, MspVersion version, const char *data, u16 len) {
	if (data == nullptr && len > 0) return;
	if (serialNum > ARRAYLEN(serials)) return;

	if (version == MspVersion::V1 && len > 254) version = MspVersion::V1_JUMBO;
	if (version == MspVersion::V1_JUMBO && len < 255) version = MspVersion::V1;
	if (version == MspVersion::V2_OVER_V1 && len > 248) version = MspVersion::V2_OVER_V1_JUMBO;
	if (version == MspVersion::V2_OVER_V1_JUMBO && len < 249) version = MspVersion::V2_OVER_V1;
	if (version == MspVersion::V1_OVER_CRSF && len > 254) version = MspVersion::V1_JUMBO_OVER_CRSF;
	if (version == MspVersion::V1_JUMBO_OVER_CRSF && len < 255) version = MspVersion::V1_OVER_CRSF;
	if (version == MspVersion::V2_OVER_V1_OVER_CRSF && len > 248) version = MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF;
	if (version == MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF && len < 249) version = MspVersion::V2_OVER_V1_OVER_CRSF;

	bool versionHasV1 = version == MspVersion::V1 || version == MspVersion::V1_JUMBO || version == MspVersion::V2_OVER_V1 || version == MspVersion::V2_OVER_V1_JUMBO || version == MspVersion::V1_OVER_CRSF || version == MspVersion::V1_JUMBO_OVER_CRSF || version == MspVersion::V2_OVER_V1_OVER_CRSF || version == MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF;
	bool versionHasV2 = version == MspVersion::V2 || version == MspVersion::V2_OVER_V1 || version == MspVersion::V2_OVER_V1_JUMBO || version == MspVersion::V2_OVER_CRSF || version == MspVersion::V2_OVER_V1_OVER_CRSF || version == MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF;
	bool versionHasJumbo = version == MspVersion::V1_JUMBO || version == MspVersion::V2_OVER_V1_JUMBO || version == MspVersion::V1_JUMBO_OVER_CRSF || version == MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF;
	bool versionHasCrsf = version == MspVersion::V2_OVER_CRSF || version == MspVersion::V1_OVER_CRSF || version == MspVersion::V2_OVER_V1_OVER_CRSF || version == MspVersion::V1_JUMBO_OVER_CRSF || version == MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF;
	if (!versionHasV2 && fn >= MspFn::MSP_V2_FRAME) return;
	Stream *ser = serials[serialNum];

	if (versionHasCrsf) {
		u8 headerSize = 0;
		switch (version) {
		case MspVersion::V1_OVER_CRSF:
			headerSize = 2; // size, cmd
			break;
		case MspVersion::V2_OVER_CRSF:
			headerSize = 5; // flag, cmd (2), size (2)
			break;
		case MspVersion::V1_JUMBO_OVER_CRSF:
			headerSize = 4; // sizeV1=255, sizeJumbo (2), cmd
			break;
		case MspVersion::V2_OVER_V1_OVER_CRSF:
			headerSize = 7; // sizeV1, cmdV1=255, flag, cmdV2 (2), sizeV2 (2)
			break;
		case MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF:
			headerSize = 9; // sizeV1=255, sizeJumbo (2), cmdV1=255, flag, cmdV2 (2), sizeV2 (2)
			break;
		}
		char buf[len + headerSize] = {0};
		switch (version) {
		case MspVersion::V1_OVER_CRSF:
			buf[0] = len;
			buf[1] = (u8)fn;
			break;
		case MspVersion::V2_OVER_CRSF:
			buf[0] = 0; // flag
			buf[1] = (u8)fn;
			buf[2] = (u16)fn >> 8;
			buf[3] = len & 0xFF;
			buf[4] = len >> 8;
			break;
		case MspVersion::V1_JUMBO_OVER_CRSF:
			buf[0] = 255; // trigger jumbo
			buf[1] = len & 0xFF;
			buf[2] = len >> 8;
			buf[3] = (u8)fn;
			break;
		case MspVersion::V2_OVER_V1_OVER_CRSF:
			buf[0] = len + 5;
			buf[1] = 255; // trigger v2 over v1
			buf[2] = 0; // flag
			buf[3] = (u8)fn;
			buf[4] = (u16)fn >> 8;
			buf[5] = len & 0xFF;
			buf[6] = len >> 8;
			break;
		case MspVersion::V2_OVER_V1_JUMBO_OVER_CRSF:
			buf[0] = 255; // trigger jumbo
			buf[1] = (len + 5) & 0xFF;
			buf[2] = (len + 5) >> 8;
			buf[3] = 255; // trigger v2 over v1
			buf[4] = 0; // flag
			buf[5] = (u8)fn;
			buf[6] = (u16)fn >> 8;
			buf[7] = len & 0xFF;
			buf[8] = len >> 8;
			break;
		}
		memcpy(&buf[headerSize], data, len);
		ELRS->sendMspMsg(type, version == MspVersion::V2 ? 2 : 1, buf, len + headerSize);
	} else {
		u8 pos = 0;
		u8 header[12];
		u32 crcV1 = 0; // u32 is faster
		u32 crcV2 = 0; // u32 is faster
		header[pos++] = '$';
		header[pos++] = versionHasV1 ? 'M' : 'X';
		header[pos++] = (u8)type;
		if (versionHasV1) {
			if (versionHasJumbo) {
				header[pos++] = 255;
				header[pos++] = len;
				header[pos++] = len >> 8;
			} else {
				header[pos++] = len;
			}
			if (versionHasV2) {
				header[pos++] = (u8)MspFn::MSP_V2_FRAME;
				header[pos++] = 0; // flag
				header[pos++] = (u16)fn;
				header[pos++] = (u16)fn >> 8;
				header[pos++] = len & 0xFF;
				header[pos++] = len >> 8;
			} else {
				header[pos++] = (u8)fn;
			}

		} else {
			// MSPv2
			header[pos++] = 0; // flag
			header[pos++] = (u16)fn;
			header[pos++] = (u16)fn >> 8;
			header[pos++] = len & 0xFF;
			header[pos++] = len >> 8;
		}
		if (versionHasV2) {
			CRC_LUT_D5_APPLY(crcV2, 0); // flag
			CRC_LUT_D5_APPLY(crcV2, (u16)fn);
			CRC_LUT_D5_APPLY(crcV2, (u16)fn >> 8);
			CRC_LUT_D5_APPLY(crcV2, len & 0xFF);
			CRC_LUT_D5_APPLY(crcV2, len >> 8);
			for (int i = 0; i < len; i++) {
				CRC_LUT_D5_APPLY(crcV2, data[i]);
			}
		}
		if (versionHasV1) {
			for (int i = 3; i < pos; i++) {
				crcV1 ^= header[i];
			}
			for (int i = 0; i < len; i++) {
				crcV1 ^= data[i];
			}
			if (versionHasV2) {
				crcV1 ^= crcV2;
			}
		}
		ser->write(header, pos);
		ser->write(data, len);
		if (versionHasV2)
			ser->write((u8)crcV2);
		if (versionHasV1)
			ser->write((u8)crcV1);
	}
}

void processMspCmd(u8 serialNum, MspMsgType mspType, MspFn fn, MspVersion version, const char *reqPayload, u16 reqLen) {
	char buf[256] = {0};
	u16 len = 0;
	if (mspType == MspMsgType::REQUEST) {
		switch (fn) {
		case MspFn::API_VERSION:
			buf[len++] = MSP_PROTOCOL_VERSION;
			buf[len++] = API_VERSION_MAJOR;
			buf[len++] = API_VERSION_MINOR;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::FIRMWARE_VARIANT:
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, KOLIBRI_IDENTIFIER, FIRMWARE_IDENTIFIER_LENGTH);
			break;
		case MspFn::FIRMWARE_VERSION:
			buf[len++] = FIRMWARE_VERSION_MAJOR;
			buf[len++] = FIRMWARE_VERSION_MINOR;
			buf[len++] = FIRMWARE_VERSION_PATCH;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::BOARD_INFO: {
			memcpy(&buf[len], targetIdentifier, TARGET_IDENTIFIER_LENGTH);
			len += 4;
			buf[len++] = 0; // board revision
			buf[len++] = 0;
			buf[len++] = 2; // 2 == FC with MAX7456
			u8 targetCapabilities = 0;
			targetCapabilities |= 1 << 0; // VCP / CDC
			targetCapabilities |= 0 << 1; // no soft serial
			buf[len++] = targetCapabilities;
			u8 targetNameLen = strlen(targetFullName);
			buf[len++] = targetNameLen;
			memcpy(&buf[len], targetFullName, targetNameLen);
			len += targetNameLen;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		case MspFn::BUILD_INFO:
			memcpy(&buf[len], __DATE__, 11);
			len += 11;
			memcpy(&buf[len], __TIME__, 8);
			len += 8;
			memcpy(&buf[len], GIT_HASH, 7);
			len += 7;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::GET_NAME: {
			char name[20] = {0};
			for (int i = 0; i < 20; i++)
				name[i] = EEPROM.read((u16)EEPROM_POS::UAV_NAME + i);
			name[19] = '\0';
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, name, strlen(name));
		} break;
		case MspFn::SET_NAME: {
			u8 len = reqLen;
			if (len > 20) len = 20;
			for (int i = 0; i < len; i++)
				EEPROM.write((u16)EEPROM_POS::UAV_NAME + i, reqPayload[i]);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
		} break;
		case MspFn::GET_FEATURE_CONFIG: {
			// only exists for compatibility with BLHeliSuite32
			u32 features = 0;
			features |= 1 << 3; // FEATURE_RX_SERIAL
			features |= 1 << 4; // FEATURE_MOTOR_STOP
			features |= 1 << 7; // FEATURE_GPS
			features |= 1 << 10; // FEATURE_TELEMETRY
			features |= 1 << 18; // FEATURE_OSD
			features |= 1 << 22; // FEATURE_AIRMODE
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)features, len);
		} break;
		case MspFn::REBOOT:
			switch (reqPayload[0]) {
			case MSP_REBOOT_FIRMWARE:
				sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
				Serial.flush();
				rebootReason = BootReason::CMD_REBOOT;
				sleep_ms(100);
				rp2040.reboot();
				break;
			case MSP_REBOOT_BOOTLOADER_FLASH:
			case MSP_REBOOT_BOOTLOADER_ROM:
				sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
				Serial.flush();
				rebootReason = BootReason::CMD_BOOTLOADER;
				sleep_ms(100);
				rp2040.rebootToBootloader();
				break;
			default:
				sendMsp(serialNum, MspMsgType::ERROR, fn, version, "Invalid reboot mode");
				break;
			}
			break;
		case MspFn::GET_ADVANCED_CONFIG:
			// only exists for compatibility with BLHeliSuite32
			buf[len++] = 1; // gyro_sync_denom
			buf[len++] = 1; // pid_process_denom
			buf[len++] = 0; // useUnsyncedPwm => true if motors are updated asynchronously from the PID
			buf[len++] = 6; // motorPwmProtocol, 6 = DShot 300
			buf[len++] = 3200 & 0xFF;
			buf[len++] = 3200 >> 8;
			buf[len++] = (IDLE_PERMILLE * 10) & 0xFF;
			buf[len++] = (IDLE_PERMILLE * 10) >> 8;
			buf[len++] = 0; // gyro_use_32kHz
			buf[len++] = 0; // motorPwmInversion
			buf[len++] = 0; // gyro_to_use
			buf[len++] = 0; // gyro_high_fsr (true if > 2000dps)
			buf[len++] = CALIBRATION_TOLERANCE;
			buf[len++] = (CALIBRATION_SAMPLES * 100 / 3200) & 0xFF; // calibration duration in centiseconds
			buf[len++] = (CALIBRATION_SAMPLES * 100 / 3200) >> 8;
			buf[len++] = 0; // gyro_offset_yaw
			buf[len++] = 0;
			buf[len++] = 0; // checkOverflow, no overflow
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::SET_ARMING_DISABLED:
			// not used by Kolibri configurator
			if (reqLen < 1) {
				sendMsp(serialNum, MspMsgType::ERROR, fn, version);
				break;
			}
			if (reqPayload[0]) {
				armingDisableFlags |= 1 << 7;
			} else {
				armingDisableFlags &= ~(1 << 7);
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
		case MspFn::MSP_STATUS:
			// only exists for compatibility with BLHeliSuite32
			buf[len++] = 312 & 0xFF;
			buf[len++] = 312 >> 8;
			buf[len++] = 0; // I2C error count
			buf[len++] = 0;
			buf[len++] = 0b101111; // gyro, no rangefinder, gps, mag, baro, accel
			buf[len++] = 0; // no other sensors
			buf[len++] = 0; // flight mode flags
			buf[len++] = 0;
			buf[len++] = 0;
			buf[len++] = 0;
			buf[len++] = 0; // PID profile index
			buf[len++] = 0; // CPU load (%)
			buf[len++] = 0; // gyro cycle time
			buf[len++] = 0;
			buf[len++] = 0; // flight mode flags count
			buf[len++] = 7; // arming disable flags count
			buf[len++] = armingDisableFlags;
			buf[len++] = armingDisableFlags >> 8;
			buf[len++] = armingDisableFlags >> 16;
			buf[len++] = armingDisableFlags >> 24;
			buf[len++] = 0; // config state flags, e.g. reboot required
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::GET_MOTOR: {
			u16 motors[8];
			for (int i = 0; i < 4; i++) {
				motors[i] = throttles[i] / 2 + 1000;
			}
			for (int i = 4; i < 8; i++) {
				motors[i] = 0;
			}
			memcpy(buf, motors, 16);
			len += 16;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		case MspFn::RC: {
			u16 channels[16];
			for (int i = 0; i < 16; i++)
				channels[i] = ELRS->channels[i];
			memcpy(buf, channels, 32);
			len += 32;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		case MspFn::MSP_ATTITUDE: {
			// not used by Kolibri configurator, that uses GET_ROTATION
			i16 rollInt = (-roll * (FIX_RAD_TO_DEG * 10)).geti32(); // decidegrees
			i16 pitchInt = (pitch * (FIX_RAD_TO_DEG * 10)).geti32(); // decidegrees
			i16 yawInt = (combinedHeading * FIX_RAD_TO_DEG).geti32(); // degrees
			buf[len++] = rollInt & 0xFF;
			buf[len++] = rollInt >> 8;
			buf[len++] = pitchInt & 0xFF;
			buf[len++] = pitchInt >> 8;
			buf[len++] = yawInt & 0xFF;
			buf[len++] = yawInt >> 8;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		case MspFn::BOXIDS:
			// only exists for compatibility with BLHeliSuite32
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
		case MspFn::GET_MOTOR_3D_CONFIG:
			buf[len++] = 1450 & 0xFF; // deadband low
			buf[len++] = 1450 >> 8;
			buf[len++] = 1550 & 0xFF; // deadband high
			buf[len++] = 1550 >> 8;
			buf[len++] = 1500 & 0xFF; // neutral
			buf[len++] = 1500 >> 8;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::GET_MOTOR_CONFIG:
			buf[len++] = (1000 + IDLE_PERMILLE) & 0xFF; // min throttle
			buf[len++] = (1000 + IDLE_PERMILLE) >> 8;
			buf[len++] = 2000 & 0xFF; // max throttle
			buf[len++] = 2000 >> 8;
			buf[len++] = 1000 & 0xFF; // min command
			buf[len++] = 1000 >> 8;
			buf[len++] = 4; // motor count
			buf[len++] = MOTOR_POLES;
			buf[len++] = 1; // use dshot telemetry
			buf[len++] = 0; // esc sensor
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
			break;
		case MspFn::UID: {
			const char *chipId = rp2040.getChipID();
			memcpy(buf, chipId, 12);
			len = 12;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		case MspFn::ACC_CALIBRATION:
			accelCalibrationCycles = QUIET_SAMPLES + CALIBRATION_SAMPLES;
			armingDisableFlags |= 0x40;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 1);
			break;
		case MspFn::MAG_CALIBRATION:
			magStateAfterRead = MAG_CALIBRATE;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 1);
			snprintf(buf, 32, "Offsets: %d %d %d", magOffset[0], magOffset[1], magOffset[2]);
			sendMsp(serialNum, MspMsgType::REQUEST, MspFn::IND_MESSAGE, version, buf, strlen(buf));
			break;
		case MspFn::SET_MOTOR:
			if (!armed) {
				throttles[(u8)MOTOR::RR] = ((u16)reqPayload[0] + ((u16)reqPayload[1] << 8)) * 2 - 2000;
				throttles[(u8)MOTOR::FR] = ((u16)reqPayload[2] + ((u16)reqPayload[3] << 8)) * 2 - 2000;
				throttles[(u8)MOTOR::RL] = ((u16)reqPayload[4] + ((u16)reqPayload[5] << 8)) * 2 - 2000;
				throttles[(u8)MOTOR::FL] = ((u16)reqPayload[6] + ((u16)reqPayload[7] << 8)) * 2 - 2000;
			}
			mspOverrideMotors = 0;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
		case MspFn::ENABLE_4WAY_IF:
			begin4Way();
			buf[0] = 4; // ESC count
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 1);
			break;
		case MspFn::SET_RTC: {
			if (reqLen < 4) {
				sendMsp(serialNum, MspMsgType::ERROR, fn, version);
				break;
			}
			datetime_t t;
			rtcConvertToDatetime(DECODE_U4((u8 *)reqPayload), &t);
			rtcSetDatetime(&t, TIME_QUALITY_MSP);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
		} break;
		case MspFn::GET_RTC: {
			datetime_t t;
			rtcGetDatetime(&t);
			buf[0] = t.year & 0xFF;
			buf[1] = t.year >> 8;
			buf[2] = t.month;
			buf[3] = t.day;
			buf[4] = t.hour;
			buf[5] = t.min;
			buf[6] = t.sec;
			buf[7] = 0; // millis
			buf[8] = 0; // millis
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 9);
		} break;
		case MspFn::STATUS: {
			u16 voltage = adcVoltage;
			buf[len++] = voltage & 0xFF;
			buf[len++] = voltage >> 8;
			buf[len++] = armed;
			buf[len++] = (u8)flightMode;
			buf[len++] = (u8)(armingDisableFlags & 0xFF);
			buf[len++] = (u8)(armingDisableFlags >> 8);
			buf[len++] = (u8)(armingDisableFlags >> 16);
			buf[len++] = (u8)(armingDisableFlags >> 24);
			buf[len++] = (u8)(configuratorConnected & 0xFF);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		case MspFn::CONFIGURATOR_PING:
			configuratorConnected = true;
			lastConfigPingRx = 0;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
			break;
		case MspFn::SERIAL_PASSTHROUGH: {
			u8 serialNum = reqPayload[0];
			u32 baud = DECODE_U4((u8 *)&reqPayload[1]);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)reqPayload, 5);
			Serial.flush();
			u8 plusCount = 0;
			elapsedMillis breakoutCounter = 0;
			switch (serialNum) {
			case 1:
				Serial1.end();
				Serial1.begin(baud);
				while (true) {
					if (breakoutCounter > 1000 && plusCount >= 3) break;
					if (Serial.available()) {
						breakoutCounter = 0;

						char c = Serial.read();
						if (c == '+')
							plusCount++;
						else
							plusCount = 0;
						Serial1.write(c);
					}
					if (Serial1.available()) {
						Serial.write(Serial1.read());
						Serial.flush();
					}
					rp2040.wdt_reset();
				}
				break;
			case 2:
				Serial2.end();
				Serial2.begin(baud);
				while (true) {
					if (breakoutCounter > 1000 && plusCount == 3) break;
					if (Serial.available()) {
						breakoutCounter = 0;

						char c = Serial.read();
						if (c == '+')
							plusCount++;
						else
							plusCount = 0;
						Serial2.write(c);
					}
					if (Serial2.available()) {
						Serial.write(Serial2.read());
						Serial.flush();
					}
					rp2040.wdt_reset();
				}
				break;
			}
		} break;
		case MspFn::SAVE_SETTINGS:
			rp2040.wdt_reset();
			EEPROM.commit();
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
		case MspFn::WRITE_OSD_FONT_CHARACTER:
			if (reqLen < 55) {
				sendMsp(serialNum, MspMsgType::ERROR, fn, version);
				break;
			}
			updateCharacter(reqPayload[0], (u8 *)&reqPayload[1]);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, reqPayload, 1);
			break;
		case MspFn::GET_BB_SETTINGS: {
			u8 bbSettings[9];
			bbSettings[0] = bbFreqDivider;
			memcpy(&bbSettings[1], &bbFlags, 8);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)bbSettings, sizeof(bbSettings));
		} break;
		case MspFn::SET_BB_SETTINGS: {
			u8 bbSettings[9];
			memcpy(bbSettings, reqPayload, sizeof(bbSettings));
			bbFreqDivider = bbSettings[0];
			memcpy(&bbFlags, &bbSettings[1], 8);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			EEPROM.put((u16)EEPROM_POS::BB_FLAGS, bbFlags);
			EEPROM.put((u16)EEPROM_POS::BB_FREQ_DIVIDER, bbFreqDivider);
		} break;
		case MspFn::BB_FILE_LIST: {
			int index = 0;
			char shortbuf[16] = {0};
			for (int i = 0; i < 100; i++) {
				rp2040.wdt_reset();
#if BLACKBOX_STORAGE == LITTLEFS_BB
				snprintf(shortbuf, 16, "/logs%01d/%01d.kbb", i / 10, i % 10);
				if (LittleFS.exists(shortbuf)) {
					buf[index++] = i;
				}
#elif BLACKBOX_STORAGE == SD_BB
				snprintf(shortbuf, 16, "/kolibri/%01d.kbb", i);
				if (SDFS.exists(shortbuf)) {
					buf[index++] = i;
				}
#endif
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, index);
		} break;
		case MspFn::BB_FILE_INFO: {
			/* data of command
			 * 0...len-1: file numbers
			 *
			 * data of response
			 * 0. file number
			 * 1-4. file size in bytes
			 * 5-7. version of bb file format
			 * 8-11: time of recording start
			 * 12. byte that indicates PID frequency
			 * 13. byte that indicates frequency divider
			 * 14-21: recording flags
			 */
			u8 len = reqLen;
			len = len > 12 ? 12 : len;
			u8 *fileNums = (u8 *)reqPayload;
			u8 buffer[22 * len];
			u8 index = 0;
			for (int i = 0; i < len; i++) {
				rp2040.wdt_reset();
				char path[32];
				u8 fileNum = fileNums[i];
#if BLACKBOX_STORAGE == LITTLEFS_BB
				snprintf(path, 32, "/logs%01d/%01d.kbb", fileNum / 10, fileNum % 10);
				File logFile = LittleFS.open(path, "r");
#elif BLACKBOX_STORAGE == SD_BB
				snprintf(path, 32, "/kolibri/%01d.kbb", fileNum);
				File logFile = SDFS.open(path, "r");
#endif
				if (!logFile)
					continue;
				buffer[index++] = fileNum;
				buffer[index++] = logFile.size() & 0xFF;
				buffer[index++] = (logFile.size() >> 8) & 0xFF;
				buffer[index++] = (logFile.size() >> 16) & 0xFF;
				buffer[index++] = (logFile.size() >> 24) & 0xFF;
				logFile.seek(LOG_HEAD_BB_VERSION, SeekSet);
				// version, timestamp, pid and divider can directly be read from the file
				for (int i = 0; i < 9; i++)
					buffer[index++] = logFile.read();
				logFile.seek(LOG_HEAD_LOGGED_FIELDS, SeekSet);
				// flags
				for (int i = 0; i < 8; i++)
					buffer[index++] = logFile.read();
				logFile.close();
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)buffer, index);
		} break;
		case MspFn::BB_FILE_DOWNLOAD: {
			u8 fileNum = reqPayload[0];
			i32 chunkNum = -1;
			if (reqLen > 1) {
				chunkNum = DECODE_I4((u8 *)&reqPayload[1]);
			}
			printLogBin(serialNum, version, fileNum, chunkNum);
		} break;
		case MspFn::BB_FILE_DELETE: {
			// data just includes one byte of file number
			u8 fileNum = reqPayload[0];
			char path[32];
#if BLACKBOX_STORAGE == LITTLEFS_BB
			snprintf(path, 32, "/logs%01d/%01d.kbb", fileNum / 10, fileNum % 10);
			if (LittleFS.remove(path))
#elif BLACKBOX_STORAGE == SD_BB
			snprintf(path, 32, "/kolibri/%01d.kbb", fileNum);
			if (SDFS.remove(path))
#endif
				sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)&fileNum, 1);
			else
				sendMsp(serialNum, MspMsgType::ERROR, fn, version, (char *)&fileNum, 1);
		} break;
		case MspFn::BB_FORMAT:
			if (clearBlackbox())
				sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			else
				sendMsp(serialNum, MspMsgType::ERROR, fn, version);
			break;
		case MspFn::GET_GPS_STATUS:
			buf[0] = gpsStatus.gpsInited;
			buf[1] = gpsStatus.initStep;
			buf[2] = gpsStatus.fixType;
			buf[3] = gpsStatus.timeValidityFlags;
			buf[4] = gpsStatus.flags;
			buf[5] = gpsStatus.flags2;
			buf[6] = gpsStatus.flags3 & 0xFF;
			buf[7] = gpsStatus.flags3 >> 8;
			buf[8] = gpsStatus.satCount;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 9);
			break;
		case MspFn::GET_GPS_ACCURACY:
			// through padding and compiler optimization, it is not possible to just memcpy the struct
			memcpy(buf, &gpsAcc.tAcc, 4);
			memcpy(&buf[4], &gpsAcc.hAcc, 4);
			memcpy(&buf[8], &gpsAcc.vAcc, 4);
			memcpy(&buf[12], &gpsAcc.sAcc, 4);
			memcpy(&buf[16], &gpsAcc.headAcc, 4);
			memcpy(&buf[20], &gpsAcc.pDop, 4);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 24);
			break;
		case MspFn::GET_GPS_TIME:
			buf[0] = gpsTime.year & 0xFF;
			buf[1] = gpsTime.year >> 8;
			buf[2] = gpsTime.month;
			buf[3] = gpsTime.day;
			buf[4] = gpsTime.hour;
			buf[5] = gpsTime.min;
			buf[6] = gpsTime.sec;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 7);
			break;
		case MspFn::GET_GPS_MOTION: {
			memcpy(buf, &gpsMotion.lat, 4);
			memcpy(&buf[4], &gpsMotion.lon, 4);
			memcpy(&buf[8], &gpsMotion.alt, 4);
			memcpy(&buf[12], &gpsMotion.velN, 4);
			memcpy(&buf[16], &gpsMotion.velE, 4);
			memcpy(&buf[20], &gpsMotion.velD, 4);
			memcpy(&buf[24], &gpsMotion.gSpeed, 4);
			memcpy(&buf[28], &gpsMotion.headMot, 4);
			memcpy(&buf[32], &combinedAltitude.raw, 4);
			memcpy(&buf[36], &vVel.raw, 4);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 40);
		} break;
		case MspFn::GET_MAG_DATA: {
			i16 raw[6] = {(i16)magData[0], (i16)magData[1], (i16)magData[2], (i16)magX.geti32(), (i16)magY.geti32(), (i16)(magHeading * FIX_RAD_TO_DEG).geti32()};
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)raw, 12);
		} break;
		case MspFn::GET_ROTATION: {
			// https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
			int rotationPitch = (pitch * 8192).geti32();
			int rotationRoll = (roll * 8192).geti32();
			int rotationYaw = (yaw * 8192).geti32();
			int heading = combinedHeading.raw >> 3;
			buf[0] = rotationPitch & 0xFF;
			buf[1] = rotationPitch >> 8;
			buf[2] = rotationRoll & 0xFF;
			buf[3] = rotationRoll >> 8;
			buf[4] = rotationYaw & 0xFF;
			buf[5] = rotationYaw >> 8;
			buf[6] = heading & 0xFF;
			buf[7] = heading >> 8;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 8);
		} break;
		case MspFn::TASK_STATUS: {
			u32 buf[256];
			for (int i = 0; i < 32; i++) {
				buf[i * 8 + 0] = tasks[i].debugInfo;
				buf[i * 8 + 1] = tasks[i].minDuration;
				buf[i * 8 + 2] = tasks[i].maxDuration;
				buf[i * 8 + 3] = tasks[i].frequency;
				buf[i * 8 + 4] = tasks[i].avgDuration;
				buf[i * 8 + 5] = tasks[i].errorCount;
				buf[i * 8 + 6] = tasks[i].lastError;
				buf[i * 8 + 7] = tasks[i].maxGap;
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)buf, sizeof(buf));
			for (int i = 0; i < 32; i++) {
				tasks[i].minDuration = 0xFFFFFFFF;
				tasks[i].maxDuration = 0;
				tasks[i].maxGap = 0;
			}
		} break;
		case MspFn::GET_RX_STATUS: {
			buf[0] = ELRS->isReceiverUp;
			buf[1] = ELRS->isLinkUp;
			buf[2] = ELRS->uplinkRssi[0];
			buf[3] = ELRS->uplinkRssi[1];
			buf[4] = ELRS->uplinkLinkQuality;
			buf[5] = ELRS->uplinkSNR;
			buf[6] = ELRS->antennaSelection;
			buf[7] = ELRS->packetRateIdx;
			memcpy(&buf[8], &ELRS->txPower, 2);
			memcpy(&buf[10], &ELRS->targetPacketRate, 2);
			memcpy(&buf[12], &ELRS->actualPacketRate, 2);
			memcpy(&buf[14], &ELRS->rcMsgCount, 4);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 18);
		} break;
		case MspFn::GET_TZ_OFFSET: {
			buf[0] = rtcTimezoneOffset;
			buf[1] = rtcTimezoneOffset >> 8;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 2);
		} break;
		case MspFn::SET_TZ_OFFSET: {
			i16 offset = DECODE_I2((u8 *)reqPayload);
			if (offset > 14 * 60 || offset < -12 * 60) {
				sendMsp(serialNum, MspMsgType::ERROR, fn, version);
				break;
			}
			rtcTimezoneOffset = offset;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
		} break;
		case MspFn::GET_PIDS: {
			u16 pids[3][7];
			for (int i = 0; i < 3; i++) {
				pids[i][0] = pidGains[i][0].raw >> P_SHIFT;
				pids[i][1] = pidGains[i][1].raw >> I_SHIFT;
				pids[i][2] = pidGains[i][2].raw >> D_SHIFT;
				pids[i][3] = pidGains[i][3].raw >> FF_SHIFT;
				pids[i][4] = pidGains[i][4].raw >> S_SHIFT;
				pids[i][5] = pidGains[i][5].raw & 0xFFFF;
				pids[i][6] = 0;
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)pids, sizeof(pids));
		} break;
		case MspFn::SET_PIDS: {
			u16 pids[3][7];
			memcpy(pids, reqPayload, sizeof(pids));
			for (int i = 0; i < 3; i++) {
				pidGains[i][0].setRaw(pids[i][0] << P_SHIFT);
				pidGains[i][1].setRaw(pids[i][1] << I_SHIFT);
				pidGains[i][2].setRaw(pids[i][2] << D_SHIFT);
				pidGains[i][3].setRaw(pids[i][3] << FF_SHIFT);
				pidGains[i][4].setRaw(pids[i][4] << S_SHIFT);
				pidGains[i][5].setRaw(pids[i][5]);
			}
			EEPROM.put((u16)EEPROM_POS::PID_GAINS, pidGains);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
		} break;
		case MspFn::GET_RATES: {
			u16 rates[3][5];
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 5; j++)
					rates[i][j] = rateFactors[j][i].geti32();
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, (char *)rates, sizeof(rates));
		} break;
		case MspFn::SET_RATES: {
			u16 rates[3][5];
			memcpy(rates, reqPayload, sizeof(rates));
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 5; j++)
					rateFactors[j][i] = rates[i][j];
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			EEPROM.put((u16)EEPROM_POS::RATE_FACTORS, rateFactors);
		} break;
		case MspFn::GET_CRASH_DUMP:
			for (int i = 0; i < 256; i++) {
				rp2040.wdt_reset();
				buf[i] = EEPROM.read(4096 - 256 + i);
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, 256);
			break;
		case MspFn::CLEAR_CRASH_DUMP:
			for (int i = 0; i < 256; i++) {
				rp2040.wdt_reset();
				EEPROM.write(4096 - 256 + i, 0);
			}
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
		case MspFn::SET_DEBUG_LED:
			gpio_put(PIN_LED_DEBUG, reqPayload[0]);
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version);
			break;
		case MspFn::PLAY_SOUND: {
			const u16 startFreq = random(1000, 5000);
			const u16 endFreq = random(1000, 5000);
			const u16 sweepDuration = random(400, 1000);
			u16 pauseDuration = random(100, 1000);
			const u16 pauseEn = random(0, 2);
			pauseDuration *= pauseEn;
			const u16 repeat = random(1, 11);
			makeSweepSound(startFreq, endFreq, ((sweepDuration + pauseDuration) * repeat) - 1, sweepDuration, pauseDuration);
			u8 len = 0;
			buf[len++] = startFreq & 0xFF;
			buf[len++] = startFreq >> 8;
			buf[len++] = endFreq & 0xFF;
			buf[len++] = endFreq >> 8;
			buf[len++] = sweepDuration & 0xFF;
			buf[len++] = sweepDuration >> 8;
			buf[len++] = pauseDuration & 0xFF;
			buf[len++] = pauseDuration >> 8;
			buf[len++] = pauseEn;
			buf[len++] = repeat;
			buf[len++] = (((sweepDuration + pauseDuration) * repeat) - 1) & 0xFF;
			buf[len++] = (((sweepDuration + pauseDuration) * repeat) - 1) >> 8;
			sendMsp(serialNum, MspMsgType::RESPONSE, fn, version, buf, len);
		} break;
		default:
			sendMsp(serialNum, MspMsgType::ERROR, fn, version, "Unknown command", strlen("Unknown command"));
			break;
		}
	}
}

void mspHandleByte(u8 c, u8 serialNum) {
	elapsedMicros taskTimer = 0;
	static char payloadBuf[2052] = {0}; // worst case: 2048 bytes payload + 3 bytes checksum (v2 over v1 jumbo) + 1 byte start. After the start byte, the index is reset to 0
	static u16 payloadBufIndex = 0;
	static u16 payloadLen = 0;
	static MspFn fn = MspFn::API_VERSION;
	static MspMsgType msgType = MspMsgType::ERROR;
	static u8 msgFlag = 0;
	static u32 crcV1 = 0;
	static u32 crcV2 = 0;
	static MspVersion msgMspVer = MspVersion::V2;
	static MspState mspState = MspState::IDLE;

	tasks[TASK_CONFIGURATOR].runCounter++;
	payloadBuf[payloadBufIndex++] = c;

	switch (mspState) {
	case MspState::IDLE:
		payloadBufIndex = 0;
		if (c == '$')
			mspState = MspState::PACKET_START;
		break;
	case MspState::PACKET_START:
		switch (c) {
		case 'M':
			mspState = MspState::TYPE_V1;
			break;
		case 'X':
			mspState = MspState::TYPE_V2;
			break;
		default:
			mspState = MspState::IDLE;
			break;
		}
		break;
	case MspState::TYPE_V1:
		crcV1 = 0;
		mspState = MspState::LEN_V1;
		switch (c) {
		case '<':
			msgType = MspMsgType::REQUEST;
			break;
		case '>':
			msgType = MspMsgType::RESPONSE;
			break;
		case '!':
			msgType = MspMsgType::ERROR;
			break;
		default:
			mspState = MspState::IDLE;
			break;
		}
		break;
	case MspState::LEN_V1:
		crcV1 ^= c;
		if (c == 255) {
			msgMspVer = MspVersion::V1_JUMBO;
			mspState = MspState::JUMBO_LEN_LO_V1;
		} else {
			payloadLen = c;
			msgMspVer = MspVersion::V1;
			mspState = MspState::CMD_V1;
		}
		break;
	case MspState::JUMBO_LEN_LO_V1:
		payloadLen = c;
		crcV1 ^= c;
		mspState = MspState::JUMBO_LEN_HI_V1;
		break;
	case MspState::JUMBO_LEN_HI_V1:
		payloadLen |= ((u16)c << 8);
		crcV1 ^= c;
		mspState = MspState::CMD_V1;
		break;
	case MspState::CMD_V1:
		crcV1 ^= c;
		if (c == (u8)MspFn::MSP_V2_FRAME) {
			msgMspVer = msgMspVer == MspVersion::V1 ? MspVersion::V2_OVER_V1 : MspVersion::V2_OVER_V1_JUMBO;
			mspState = MspState::FLAG_V2_OVER_V1;
		} else {
			fn = (MspFn)c;
			mspState = payloadLen ? MspState::PAYLOAD_V1 : MspState::CHECKSUM_V1;
			payloadBufIndex = 0;
		}
		break;
	case MspState::PAYLOAD_V1:
		crcV1 ^= c;
		if (payloadBufIndex == payloadLen)
			mspState = MspState::CHECKSUM_V1;
		break;
	case MspState::FLAG_V2_OVER_V1:
		msgFlag = c;
		crcV1 ^= c;
		crcV2 = crcLutD5[c];
		mspState = MspState::CMD_LO_V2_OVER_V1;
		break;
	case MspState::CMD_LO_V2_OVER_V1:
		fn = (MspFn)c;
		crcV1 ^= c;
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = MspState::CMD_HI_V2_OVER_V1;
		break;
	case MspState::CMD_HI_V2_OVER_V1:
		fn = (MspFn)((u32)fn | (u32)c << 8);
		crcV1 ^= c;
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = MspState::LEN_LO_V2_OVER_V1;
		break;
	case MspState::LEN_LO_V2_OVER_V1:
		payloadLen = c;
		crcV1 ^= c;
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = MspState::LEN_HI_V2_OVER_V1;
		break;
	case MspState::LEN_HI_V2_OVER_V1:
		payloadLen |= ((u16)c << 8);
		if (payloadLen > 2048) {
			mspState = MspState::IDLE;
			break;
		}
		crcV1 ^= c;
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = payloadLen ? MspState::PAYLOAD_V2_OVER_V1 : MspState::CHECKSUM_V2_OVER_V1;
		payloadBufIndex = 0;
		break;
	case MspState::PAYLOAD_V2_OVER_V1:
		crcV1 ^= c;
		crcV2 = crcLutD5[c ^ crcV2];
		if (payloadBufIndex == payloadLen)
			mspState = MspState::CHECKSUM_V2_OVER_V1;
		break;
	case MspState::CHECKSUM_V2_OVER_V1:
		if (c != crcV2) {
			mspState = MspState::IDLE;
			sendMsp(serialNum, MspMsgType::ERROR, fn, msgMspVer, "CRCv2", 5);
			break;
		}
		crcV1 ^= c;
		mspState = MspState::CHECKSUM_V1;
		break;
	case MspState::CHECKSUM_V1:
		if (c == crcV1)
			processMspCmd(serialNum, msgType, fn, msgMspVer, payloadBuf, payloadLen);
		else
			sendMsp(serialNum, MspMsgType::ERROR, fn, msgMspVer, "CRCv1", 5);
		mspState = MspState::IDLE;
		break;
	case MspState::TYPE_V2:
		mspState = MspState::FLAG_V2;
		msgMspVer = MspVersion::V2;
		switch (c) {
		case '<':
			msgType = MspMsgType::REQUEST;
			break;
		case '>':
			msgType = MspMsgType::RESPONSE;
			break;
		case '!':
			msgType = MspMsgType::ERROR;
			break;
		default:
			mspState = MspState::IDLE;
			break;
		}
		break;
	case MspState::FLAG_V2:
		msgFlag = c;
		crcV2 = crcLutD5[c];
		mspState = MspState::CMD_LO_V2;
		break;
	case MspState::CMD_LO_V2:
		fn = (MspFn)c;
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = MspState::CMD_HI_V2;
		break;
	case MspState::CMD_HI_V2:
		fn = (MspFn)((u32)fn | (u32)c << 8);
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = MspState::LEN_LO_V2;
		break;
	case MspState::LEN_LO_V2:
		payloadLen = c;
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = MspState::LEN_HI_V2;
		break;
	case MspState::LEN_HI_V2:
		payloadLen |= ((u16)c << 8);
		if (payloadLen > 2048) {
			mspState = MspState::IDLE;
			break;
		}
		crcV2 = crcLutD5[c ^ crcV2];
		mspState = payloadLen ? MspState::PAYLOAD_V2 : MspState::CHECKSUM_V2;
		payloadBufIndex = 0;
		break;
	case MspState::PAYLOAD_V2:
		crcV2 = crcLutD5[c ^ crcV2];
		if (payloadBufIndex == payloadLen)
			mspState = MspState::CHECKSUM_V2;
		break;
	case MspState::CHECKSUM_V2:
		if (c == crcV2)
			processMspCmd(serialNum, msgType, fn, msgMspVer, payloadBuf, payloadLen);
		else
			sendMsp(serialNum, MspMsgType::ERROR, fn, msgMspVer, "CRCv2", 5);
		mspState = MspState::IDLE;
		break;
	}
	u32 duration = taskTimer;
	tasks[TASK_CONFIGURATOR].totalDuration += duration;
	if (duration < tasks[TASK_CONFIGURATOR].minDuration) {
		tasks[TASK_CONFIGURATOR].minDuration = duration;
	}
	if (duration > tasks[TASK_CONFIGURATOR].maxDuration) {
		tasks[TASK_CONFIGURATOR].maxDuration = duration;
		tasks[TASK_CONFIGURATOR].debugInfo = (u32)fn;
	}
}