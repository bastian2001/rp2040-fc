#include "global.h"

i16 bmiDataRaw[6] = {0, 0, 0, 0, 0, 0};
i16 *gyroDataRaw;
i16 *accelDataRaw;
FLIGHT_MODE flightMode = FLIGHT_MODE::ACRO;

#define IDLE_PERMILLE 25
#define MAX_ANGLE 35 // degrees

/*
 * To avoid a lot of floating point math, fixed point math is used.
 * The gyro data is 16 bit, with a range of +/- 2000 degrees per second.
 * All data is converted to degrees per second (both RC commands as well as gyro data),
 * and calculations will be performed on a 16.16 fixed point number.
 * Additions can be performed like normal, while multiplications require
 * the numbers to be converted to 64 bit before calculation.
 * Accel data is also 16.16 bit fixed point math, just the unit is g.
 */

i16 throttles[4];

fix32 imuData[6];

fix32 pidGains[3][7];
fix32 pidGainsVVel[3], pidGainsHVel[3], pidGainsAlt[3];
fix32 angleModeP = 10, velocityModeP = 3;

fix32 rollSetpoint, pitchSetpoint, yawSetpoint, rollError, pitchError, yawError, rollLast, pitchLast, yawLast, rollLastSetpoint, pitchLastSetpoint, yawLastSetpoint, vVelSetpoint, vVelError, vVelLast, eVelSetpoint, eVelError, eVelLast, nVelSetpoint, nVelError, nVelLast;
fix64 rollErrorSum, pitchErrorSum, yawErrorSum, vVelErrorSum, eVelErrorSum, nVelErrorSum, altErrorSum;
fix32 rollP, pitchP, yawP, rollI, pitchI, yawI, rollD, pitchD, yawD, rollFF, pitchFF, yawFF, rollS, pitchS, yawS, vVelP, vVelI, vVelD, eVelP, eVelI, eVelD, nVelP, nVelI, nVelD;
fix32 altSetpoint, altError, altLast, altP, altI, altD;
fix32 tRR, tRL, tFR, tFL;
fix32 throttle;

u32 pidLoopCounter = 0;

fix32 rateFactors[5][3];
#undef RAD_TO_DEG
const fix32 RAD_TO_DEG = fix32(180) / PI;
const fix32 TO_ANGLE   = fix32(MAX_ANGLE) / fix32(512);
u16 smoothChannels[4];

void initPID() {
	for (int i = 0; i < 3; i++) {
		pidGains[i][P].setRaw(40 << P_SHIFT);
		pidGains[i][I].setRaw(20 << I_SHIFT);
		pidGains[i][D].setRaw(100 << D_SHIFT);
		pidGains[i][FF].setRaw(0 << FF_SHIFT);
		pidGains[i][S].setRaw(0 << S_SHIFT);
		pidGains[i][iFalloff] = .998;
	}
	for (int i = 0; i < 3; i++) {
		rateFactors[0][i] = 100; // first order, center rate
		rateFactors[1][i] = 0;
		rateFactors[2][i] = 200;
		rateFactors[3][i] = 0;
		rateFactors[4][i] = 800;
	}
	pidGainsVVel[P] = 800;           // additional throttle if velocity is 1m/s too low
	pidGainsVVel[I] = .02;           // increase throttle by 3200x this value, when error is 1m/s
	pidGainsVVel[D] = 0;             // additional throttle, if accelerating by 3200m/s^2
	pidGainsAlt[P]  = 60;            // additional throttle if altitude is 1m too low
	pidGainsAlt[I]  = 0.001;         // increase throttle by 3200x this value per second, when error is 1m
	pidGainsAlt[D]  = 20;            // additional throttle, if changing altitude by 3200m/s
	pidGainsHVel[P] = 12;            // immediate target tilt in degree @ 1m/s too slow/fast
	pidGainsHVel[I] = 10.f / 3200.f; // additional tilt per 1/3200th of a second @ 1m/s too slow/fast
	pidGainsHVel[D] = 7;             // tilt in degrees, if changing speed by 3200m/s /s
}

void decodeErpm() {
	tasks[TASK_ESC_RPM].runCounter++;
	elapsedMicros taskTimer = 0;
	u32 edr;
	for (i32 m = 0; m < 4; m++) {
		u32 edgeDetectedReturn = 0;
		u8 currentBitValue     = 0;
		u8 bitCount            = 0;
		u32 totalShifted       = 0;
		u8 started             = 0;
		for (u8 p = 0; p < 16 && totalShifted != 21; p++) {
			u32 err = escRawReturn[p] >> m;
			for (u8 b = 0; b < 8; b++) {
				u8 bit = (err >> ((7 - b) << 2)) & 1;
				started |= !bit;
				if (started) {
					if (bit == currentBitValue) {
						if (++bitCount == 4) {
							bitCount = 0;
							edgeDetectedReturn <<= 1;
							edgeDetectedReturn |= currentBitValue;
							totalShifted++;
						}
					} else {
						if (bitCount >= 2) {
							edgeDetectedReturn <<= 1;
							edgeDetectedReturn |= currentBitValue;
							totalShifted++;
						}
						bitCount        = 1;
						currentBitValue = bit;
					}
					if (totalShifted == 21) break;
				}
			}
		}
		edr                = edgeDetectedReturn;
		edgeDetectedReturn = edgeDetectedReturn ^ (edgeDetectedReturn >> 1);
		u32 rpm            = escDecodeLut[edgeDetectedReturn & 0x1F];
		rpm |= escDecodeLut[(edgeDetectedReturn >> 5) & 0x1F] << 4;
		rpm |= escDecodeLut[(edgeDetectedReturn >> 10) & 0x1F] << 8;
		rpm |= escDecodeLut[(edgeDetectedReturn >> 15) & 0x1F] << 12;
		u32 csum = (rpm >> 8) ^ rpm;
		csum ^= csum >> 4;
		csum &= 0xF;
		if (csum != 0x0F || rpm > 0xFFFF) {
			// transmission error
			escErpmFail |= 1 << m;
			continue;
		}
		escErpmFail &= ~(1 << m);
		rpm >>= 4;
		if (rpm == 0xFFF) {
			escRpm[m] = 0;
		} else {
			rpm       = (rpm & 0x1FF) << (rpm >> 9); // eeem mmmm mmmm
			rpm       = (60000000 + 50 * rpm) / rpm;
			escRpm[m] = rpm / (MOTOR_POLES / 2);
		}
	}
	if (escErpmFail) tasks[TASK_ESC_RPM].errorCount++;
	escErpmReady = 0;
	u32 duration = taskTimer;
	tasks[TASK_ESC_RPM].totalDuration += duration;
	if (duration > tasks[TASK_ESC_RPM].maxDuration) {
		tasks[TASK_ESC_RPM].maxDuration = duration;
	}
	if (duration < tasks[TASK_ESC_RPM].minDuration) {
		tasks[TASK_ESC_RPM].minDuration = duration;
	}
}

u32 takeoffCounter = 0;
elapsedMicros taskTimerGyro, taskTimerPid;
void pidLoop() {
	u32 duration = taskTimerGyro;
	if (tasks[TASK_GYROREAD].maxGap < duration)
		tasks[TASK_GYROREAD].maxGap = duration;
	taskTimerGyro = 0;
	tasks[TASK_GYROREAD].runCounter++;
	gyroGetData(bmiDataRaw);
	for (int i = 0; i < 3; i++) {
		imuData[i].setRaw((i32)gyroDataRaw[i] * 4000); // gyro data in range of -.5 ... +.5 due to fixed point math,gyro data in range of -2000 ... +2000 (degrees per second)
													   // imuData[i + 3].setRaw(accelDataRaw[i]);
													   // imuData[i + 3] *= 32;
	}
	imuData[AXIS_PITCH] = -imuData[AXIS_PITCH];
	imuData[AXIS_YAW]   = -imuData[AXIS_YAW];
	duration            = taskTimerGyro;
	tasks[TASK_GYROREAD].totalDuration += duration;
	if (duration > tasks[TASK_GYROREAD].maxDuration)
		tasks[TASK_GYROREAD].maxDuration = duration;
	if (duration < tasks[TASK_GYROREAD].minDuration)
		tasks[TASK_GYROREAD].minDuration = duration;
	taskTimerGyro = 0;

	updateAttitude();
	duration = taskTimerPid;
	if (tasks[TASK_PID_MOTORS].maxGap < duration)
		tasks[TASK_PID_MOTORS].maxGap = duration;
	taskTimerPid = 0;
	tasks[TASK_PID_MOTORS].runCounter++;

	if (escErpmReady)
		decodeErpm();
	else
		escErpmFail = 0b1111;

	if (armed) {
		// Quad armed
		static fix32 polynomials[5][3]; // always recreating variables is slow, but exposing is bad, hence static
		ELRS->getSmoothChannels(smoothChannels);
		// calculate setpoints
		polynomials[0][0].setRaw(((i32)smoothChannels[0] - 1500) << 7); //-1...+1 in fixed point notation;
		polynomials[0][1].setRaw(((i32)smoothChannels[1] - 1500) << 7);
		polynomials[0][2].setRaw(((i32)smoothChannels[3] - 1500) << 7);
		throttle      = (smoothChannels[2] - 1000) * 2;
		rollSetpoint  = 0;
		pitchSetpoint = 0;
		yawSetpoint   = 0;
		if (flightMode == FLIGHT_MODE::ANGLE || flightMode == FLIGHT_MODE::ALT_HOLD || flightMode == FLIGHT_MODE::GPS_VEL) {
			fix32 dRoll;
			fix32 dPitch;
			if (flightMode < FLIGHT_MODE::GPS_VEL) {
				dRoll         = fix32(smoothChannels[0] - 1500) * TO_ANGLE - (RAD_TO_DEG * roll);
				dPitch        = fix32(smoothChannels[1] - 1500) * TO_ANGLE + (RAD_TO_DEG * pitch);
				rollSetpoint  = dRoll * angleModeP;
				pitchSetpoint = dPitch * angleModeP;
			} else if (flightMode == FLIGHT_MODE::GPS_VEL) {
				fix32 cosfhead = cosFix(fix32::fromRaw(combinedHeading) / fix32(87.43f));
				fix32 sinfhead = sinFix(fix32::fromRaw(combinedHeading) / fix32(87.43f));
				eVelSetpoint   = cosfhead * (smoothChannels[0] - 1500) + sinfhead * (smoothChannels[1] - 1500);
				nVelSetpoint   = -sinfhead * (smoothChannels[0] - 1500) + cosfhead * (smoothChannels[1] - 1500);
				eVelSetpoint   = eVelSetpoint >> 9; //+-500 => +-1
				nVelSetpoint   = nVelSetpoint >> 9; //+-500 => +-1
				eVelSetpoint *= 12;                 //+-12m/s
				nVelSetpoint *= 12;                 //+-12m/s
				eVelError = eVelSetpoint - eVel;
				nVelError = nVelSetpoint - nVel;
				eVelErrorSum += eVelError;
				nVelErrorSum += nVelError;
				eVelP = pidGainsHVel[P] * eVelError;
				nVelP = pidGainsHVel[P] * nVelError;
				eVelI = pidGainsHVel[I] * eVelErrorSum;
				nVelI = pidGainsHVel[I] * nVelErrorSum;
				eVelD = pidGainsHVel[D] * (eVel - eVelLast);
				nVelD = pidGainsHVel[D] * (nVel - nVelLast);

				fix32 eVelPID     = eVelP + eVelI + eVelD;
				fix32 nVelPID     = nVelP + nVelI + nVelD;
				fix32 targetRoll  = eVelPID * cosfhead - nVelPID * sinfhead;
				fix32 targetPitch = eVelPID * sinfhead + nVelPID * cosfhead;
				targetRoll        = constrain(targetRoll, -MAX_ANGLE, MAX_ANGLE);
				targetPitch       = constrain(targetPitch, -MAX_ANGLE, MAX_ANGLE);
				dRoll             = targetRoll - (RAD_TO_DEG * roll);
				dPitch            = targetPitch + (RAD_TO_DEG * pitch);
				rollSetpoint      = dRoll * velocityModeP;
				pitchSetpoint     = dPitch * velocityModeP;
			}
			for (int i = 1; i < 5; i++) {
				polynomials[i][2] = polynomials[i - 1][2] * polynomials[0][2];
				if (polynomials[0][2] < 0)
					polynomials[i][2] = -polynomials[i][2];
			}

			yawSetpoint = 0;
			for (int i = 0; i < 5; i++)
				yawSetpoint += rateFactors[i][2] * polynomials[i][2];

			if (flightMode == FLIGHT_MODE::ALT_HOLD || flightMode == FLIGHT_MODE::GPS_VEL) {
				fix32 t = throttle - 1000;
				// deadband in center of stick
				if (t > 0) {
					t -= 100;
					if (t < 0) t = 0;
				} else if (t < 0) {
					t += 100;
					if (t > 0) t = 0;
				}
				// estimate throttle
				// vVelSetpoint = t / 180; // +/- 5 m/s
				// vVelError    = vVelSetpoint - vVel;
				// vVelErrorSum += vVelError;
				// vVelP    = pidGainsVVel[P] * vVelError;
				// vVelI    = pidGainsVVel[I] * vVelErrorSum;
				// vVelD    = pidGainsVVel[D] * (vVel - vVelLast);
				// throttle = vVelP + vVelI + vVelD;
				// estimate throttle based on altitude error
				altSetpoint += t / 180 / 3200;
				altError = altSetpoint - combinedAltitude;
				altErrorSum += altError;
				altP     = pidGainsAlt[P] * altError;
				altI     = pidGainsAlt[I] * altErrorSum;
				altD     = pidGainsAlt[D] * (altLast - combinedAltitude) * 3200;
				throttle = altP + altI + altD;
			} else {
				vVelErrorSum = 0;
			}
			altLast  = combinedAltitude;
			vVelLast = vVel;
		} else if (flightMode == FLIGHT_MODE::ACRO) {
			/* at full stick deflection, ...Raw values are either +1 or -1. That will make all the
			 * polynomials also +/-1. Thus, the total rate for each axis is equal to the sum of all 5 rateFactors
			 * of that axis. The center rate is the ratefactor[x][0].
			 */
			for (int i = 1; i < 5; i++) {
				for (int j = 0; j < 3; j++) {
					polynomials[i][j] = polynomials[i - 1][j] * polynomials[0][j];
					if (polynomials[0][j] < 0) // on second and fourth order, preserve initial sign
						polynomials[i][j] = -polynomials[i][j];
				}
			}
			for (int i = 0; i < 5; i++) {
				rollSetpoint += rateFactors[i][0] * polynomials[i][0];
				pitchSetpoint += rateFactors[i][1] * polynomials[i][1];
				yawSetpoint += rateFactors[i][2] * polynomials[i][2];
			}
		}
		rollError  = rollSetpoint - imuData[AXIS_ROLL];
		pitchError = pitchSetpoint - imuData[AXIS_PITCH];
		yawError   = yawSetpoint - imuData[AXIS_YAW];
		if (ELRS->channels[2] > 1020)
			takeoffCounter++;
		else if (takeoffCounter < 1000) // 1000 = ca. 0.3s
			takeoffCounter = 0;         // if the quad hasn't "taken off" yet, reset the counter
		if (takeoffCounter < 1000)      // enable i term falloff (windup prevention) only before takeoff
		{
			rollErrorSum  = pidGains[0][iFalloff].multiply64(rollErrorSum);
			pitchErrorSum = pidGains[1][iFalloff].multiply64(pitchErrorSum);
			yawErrorSum   = pidGains[2][iFalloff].multiply64(yawErrorSum);
		}

		rollErrorSum += rollError;
		pitchErrorSum += pitchError;
		yawErrorSum += yawError;
		rollP   = pidGains[0][P] * rollError;
		pitchP  = pidGains[1][P] * pitchError;
		yawP    = pidGains[2][P] * yawError;
		rollI   = pidGains[0][I] * rollErrorSum;
		pitchI  = pidGains[1][I] * pitchErrorSum;
		yawI    = pidGains[2][I] * yawErrorSum;
		rollD   = pidGains[0][D] * (imuData[AXIS_ROLL] - rollLast);
		pitchD  = pidGains[1][D] * (imuData[AXIS_PITCH] - pitchLast);
		yawD    = pidGains[2][D] * (imuData[AXIS_YAW] - yawLast);
		rollFF  = pidGains[0][FF] * (rollSetpoint - rollLastSetpoint);
		pitchFF = pidGains[1][FF] * (pitchSetpoint - pitchLastSetpoint);
		yawFF   = pidGains[2][FF] * (yawSetpoint - yawLastSetpoint);
		rollS   = pidGains[0][S] * rollSetpoint;
		pitchS  = pidGains[1][S] * pitchSetpoint;
		yawS    = pidGains[2][S] * yawSetpoint;

		fix32 rollTerm  = rollP + rollI + rollD + rollFF + rollS;
		fix32 pitchTerm = pitchP + pitchI + pitchD + pitchFF + pitchS;
		fix32 yawTerm   = yawP + yawI + yawD + yawFF + yawS;
#ifdef PROPS_OUT
		tRR = throttle - rollTerm + pitchTerm + yawTerm;
		tFR = throttle - rollTerm - pitchTerm - yawTerm;
		tRL = throttle + rollTerm + pitchTerm - yawTerm;
		tFL = throttle + rollTerm - pitchTerm + yawTerm;
#else
		tRR = throttle - rollTerm + pitchTerm - yawTerm;
		tFR = throttle - rollTerm - pitchTerm + yawTerm;
		tRL = throttle + rollTerm + pitchTerm + yawTerm;
		tFL = throttle + rollTerm - pitchTerm - yawTerm;
#endif
		throttles[(u8)MOTOR::RR] = map(tRR.getInt(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		throttles[(u8)MOTOR::RL] = map(tRL.getInt(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		throttles[(u8)MOTOR::FR] = map(tFR.getInt(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		throttles[(u8)MOTOR::FL] = map(tFL.getInt(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		if (throttles[(u8)MOTOR::RR] > 2000) {
			i16 diff                 = throttles[(u8)MOTOR::RR] - 2000;
			throttles[(u8)MOTOR::RR] = 2000;
			throttles[(u8)MOTOR::FL] -= diff;
			throttles[(u8)MOTOR::FR] -= diff;
			throttles[(u8)MOTOR::RL] -= diff;
		}
		if (throttles[(u8)MOTOR::RL] > 2000) {
			i16 diff                 = throttles[(u8)MOTOR::RL] - 2000;
			throttles[(u8)MOTOR::RL] = 2000;
			throttles[(u8)MOTOR::FL] -= diff;
			throttles[(u8)MOTOR::FR] -= diff;
			throttles[(u8)MOTOR::RR] -= diff;
		}
		if (throttles[(u8)MOTOR::FR] > 2000) {
			i16 diff                 = throttles[(u8)MOTOR::FR] - 2000;
			throttles[(u8)MOTOR::FR] = 2000;
			throttles[(u8)MOTOR::FL] -= diff;
			throttles[(u8)MOTOR::RL] -= diff;
			throttles[(u8)MOTOR::RR] -= diff;
		}
		if (throttles[(u8)MOTOR::FL] > 2000) {
			i16 diff                 = throttles[(u8)MOTOR::FL] - 2000;
			throttles[(u8)MOTOR::FL] = 2000;
			throttles[(u8)MOTOR::RL] -= diff;
			throttles[(u8)MOTOR::RR] -= diff;
			throttles[(u8)MOTOR::FR] -= diff;
		}
		if (throttles[(u8)MOTOR::RR] < IDLE_PERMILLE * 2) {
			i16 diff                 = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::RR];
			throttles[(u8)MOTOR::RR] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::FL] += diff;
			throttles[(u8)MOTOR::FR] += diff;
			throttles[(u8)MOTOR::RL] += diff;
		}
		if (throttles[(u8)MOTOR::RL] < IDLE_PERMILLE * 2) {
			i16 diff                 = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::RL];
			throttles[(u8)MOTOR::RL] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::FL] += diff;
			throttles[(u8)MOTOR::FR] += diff;
			throttles[(u8)MOTOR::RR] += diff;
		}
		if (throttles[(u8)MOTOR::FR] < IDLE_PERMILLE * 2) {
			i16 diff                 = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::FR];
			throttles[(u8)MOTOR::FR] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::FL] += diff;
			throttles[(u8)MOTOR::RL] += diff;
			throttles[(u8)MOTOR::RR] += diff;
		}
		if (throttles[(u8)MOTOR::FL] < IDLE_PERMILLE * 2) {
			i16 diff                 = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::FL];
			throttles[(u8)MOTOR::FL] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::RL] += diff;
			throttles[(u8)MOTOR::RR] += diff;
			throttles[(u8)MOTOR::FR] += diff;
		}
		for (int i = 0; i < 4; i++)
			throttles[i] = throttles[i] > 2000 ? 2000 : throttles[i];
		sendThrottles(throttles);
		rollLast          = imuData[AXIS_ROLL];
		pitchLast         = imuData[AXIS_PITCH];
		yawLast           = imuData[AXIS_YAW];
		rollLastSetpoint  = rollSetpoint;
		pitchLastSetpoint = pitchSetpoint;
		yawLastSetpoint   = yawSetpoint;
		if ((pidLoopCounter % bbFreqDivider) == 0 && bbFreqDivider) {
			writeSingleFrame();
		}
		pidLoopCounter++;
	} else {
		// Quad disarmed or RC disconnected
		// all motors off
		if (configOverrideMotors > 1000)
			for (int i = 0; i < 4; i++)
				throttles[i] = 0;
		if (ELRS->channels[9] < 1500) {
			sendThrottles(throttles);
		} else {
			static elapsedMillis motorBeepTimer = 0;
			if (motorBeepTimer > 500)
				motorBeepTimer = 0;
			if (motorBeepTimer < 200) {
				u16 motors[4] = {DSHOT_CMD_BEACON2, DSHOT_CMD_BEACON2, DSHOT_CMD_BEACON2, DSHOT_CMD_BEACON2};
				sendRaw11Bit(motors);
			} else {
				u16 motors[4] = {0, 0, 0, 0};
				sendRaw11Bit(motors);
			}
		}
		rollErrorSum   = 0;
		pitchErrorSum  = 0;
		yawErrorSum    = 0;
		rollLast       = 0;
		pitchLast      = 0;
		yawLast        = 0;
		takeoffCounter = 0;
	}
	duration = taskTimerPid;
	tasks[TASK_PID_MOTORS].totalDuration += duration;
	if (duration < tasks[TASK_PID_MOTORS].minDuration) {
		tasks[TASK_PID_MOTORS].minDuration = duration;
	}
	if (duration > tasks[TASK_PID_MOTORS].maxDuration) {
		tasks[TASK_PID_MOTORS].maxDuration = duration;
	}
	taskTimerPid = 0;
}