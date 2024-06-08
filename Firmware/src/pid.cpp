#include "global.h"

i16 bmiDataRaw[6] = {0, 0, 0, 0, 0, 0};
i16 *gyroDataRaw;
i16 *accelDataRaw;
FlightMode flightMode = FlightMode::ACRO;

#define MAX_ANGLE 35 // degrees

/*
 * To avoid a lot of floating point math, fixed point math is used.
 * The gyro data is 16 bit, with a range of +/- 2000 degrees per second.
 * All data is converted to degrees per second (both RC commands as well as gyro data),
 * and calculations will be performed on a 16.16 fixed point number.
 * Additions can be performed like normal, while multiplications require
 * the numbers to be converted to 64 bit before calculation.
 */

i16 throttles[4];

fix32 gyroData[3];

fix32 pidGains[3][7];
fix32 pidGainsVVel[4], pidGainsHVel[3];
fix32 angleModeP = 10, velocityModeP = 3;

fix32 rollSetpoint, pitchSetpoint, yawSetpoint, rollError, pitchError, yawError, rollLast, pitchLast, yawLast, vVelSetpoint, vVelError, vVelLast, eVelSetpoint, eVelError, eVelLast, nVelSetpoint, nVelError, nVelLast, vVelLastSetpoint;
fix64 rollErrorSum, pitchErrorSum, yawErrorSum, vVelErrorSum, eVelErrorSum, nVelErrorSum;
fix32 rollP, pitchP, yawP, rollI, pitchI, yawI, rollD, pitchD, yawD, rollFF, pitchFF, yawFF, rollS, pitchS, yawS, vVelP, vVelI, vVelD, vVelFF, eVelP, eVelI, eVelD, nVelP, nVelI, nVelD;
fix32 altSetpoint;
fix32 tRR, tRL, tFR, tFL;
fix32 throttle;
PT1 dFilterRoll(100, 3200), dFilterPitch(100, 3200), dFilterYaw(100, 3200);

fix32 rollSetpoints[8], pitchSetpoints[8], yawSetpoints[8];

u32 pidLoopCounter = 0;

fix32 rateFactors[5][3];
fix64 vVelMaxErrorSum, vVelMinErrorSum;
const fix32 TO_ANGLE = fix32(MAX_ANGLE) / fix32(512);
fix32 smoothChannels[4];
u16 condensedRpm[4];

#define RIGHT_BITS(x, n) ((u32)(-(x)) >> (32 - n))

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
	pidGainsVVel[P] = 100; // additional throttle if velocity is 1m/s too low
	pidGainsVVel[I] = .03; // increase throttle by 3200x this value, when error is 1m/s
	pidGainsVVel[D] = 20000; // additional throttle, if accelerating by 3200m/s^2
	pidGainsVVel[FF] = 30000;
	pidGainsHVel[P] = 6; // immediate target tilt in degree @ 1m/s too slow/fast
	pidGainsHVel[I] = 2.f / 3200.f; // additional tilt per 1/3200th of a second @ 1m/s too slow/fast
	pidGainsHVel[D] = 0; // tilt in degrees, if changing speed by 3200m/s /s
	vVelMaxErrorSum = 2000 / pidGainsVVel[I].getf32();
	vVelMinErrorSum = IDLE_PERMILLE * 2 / pidGainsVVel[I].getf32();
}

void decodeErpm() {
	if (!enableDShot) return;
	tasks[TASK_ESC_RPM].runCounter++;
	elapsedMicros taskTimer = 0;
	for (int m = 0; m < 4; m++) {
		dma_channel_abort(escDmaChannel[m]);
		if (!erpmEdges[m][0]) {
			escErpmFail |= 1 << m;
			tasks[TASK_ESC_RPM].errorCount++;
			condensedRpm[m] = 0;
			continue;
		}
		u32 edgeDetectedReturn = 0;
		u32 *p = (u32 *)&erpmEdges[m][1]; // first packet is just ones before the actual packet starts
		// packet format: blllllll llllllll llllllll llllllll => b = the sampled bit, l = the duration of the bit
		u32 shifts = 0;
		for (int i = 1; i < 32; i++) {
			u32 packet = ~*p++;
			u32 duration = (packet & 0x7FFFFFFF) + 3;
			// PIO starts counting down from 0xFFFFFFFF, with the inversion that is 0, but it actually already counted 1. The other 2 come from rounding (with 4x oversampling, any duration >=2 is valid)
			duration /= 4;
			if (duration > 21) {
				break; // this is either a transmission error, or the end of the packet (((~0) & 0x7FF...) + 3 = a lot)
			} else {
				shifts += duration;
				edgeDetectedReturn = edgeDetectedReturn << duration | RIGHT_BITS(packet >> 31, duration);
			}
		}
		edgeDetectedReturn = edgeDetectedReturn << (21 - shifts) | 0x1FFFFF >> shifts;
		edgeDetectedReturn = edgeDetectedReturn ^ (edgeDetectedReturn >> 1);
		u32 rpm = escDecodeLut[edgeDetectedReturn & 0x1F];
		rpm |= escDecodeLut[(edgeDetectedReturn >> 5) & 0x1F] << 4;
		rpm |= escDecodeLut[(edgeDetectedReturn >> 10) & 0x1F] << 8;
		rpm |= escDecodeLut[(edgeDetectedReturn >> 15) & 0x1F] << 12;
		u32 csum = (rpm >> 8) ^ rpm;
		csum ^= csum >> 4;
		csum &= 0xF;
		if (csum != 0x0F || rpm > 0xFFFF) {
			escErpmFail |= 1 << m;
			tasks[TASK_ESC_RPM].errorCount++;
			condensedRpm[m] = 0;
			continue;
		}
		rpm >>= 4;
		condensedRpm[m] = rpm;
		if (rpm == 0xFFF) {
			escRpm[m] = 0;
		} else {
			rpm = (rpm & 0x1FF) << (rpm >> 9); // eeem mmmm mmmm
			if (!rpm) {
				escErpmFail |= 1 << m;
				continue;
			}
			rpm = (60000000 + 50 * rpm) / rpm;
			escRpm[m] = rpm / (MOTOR_POLES / 2);
			escErpmFail &= ~(1 << m);
		}
	}
	memset((u32 *)erpmEdges, 0, sizeof(erpmEdges));
	dma_channel_set_trans_count(escClearDmaChannel, 128, false); // clear out all the edges (4 motors * 32 edges)
	dma_channel_set_write_addr(escClearDmaChannel, &erpmEdges[0], true);
	u32 duration = taskTimer;
	tasks[TASK_ESC_RPM].totalDuration += duration;
	if (duration > tasks[TASK_ESC_RPM].maxDuration)
		tasks[TASK_ESC_RPM].maxDuration = duration;
	if (duration < tasks[TASK_ESC_RPM].minDuration)
		tasks[TASK_ESC_RPM].minDuration = duration;
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
		gyroData[i].setRaw((i32)gyroDataRaw[i] * 4000); // gyro data in range of -.5 ... +.5 due to fixed point math,gyro data in range of -2000 ... +2000 (degrees per second)
	}
	gyroData[AXIS_PITCH] = -gyroData[AXIS_PITCH];
	gyroData[AXIS_YAW] = -gyroData[AXIS_YAW];
	duration = taskTimerGyro;
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

	decodeErpm();

	if (armed) {
		// Quad armed
		static u32 ffBufPos = 0;
		static fix32 polynomials[5][3];
		ELRS->getSmoothChannels(smoothChannels);
		// calculate setpoints
		polynomials[0][0] = (smoothChannels[0] - 1500) >> 9; //-1...+1
		polynomials[0][1] = (smoothChannels[1] - 1500) >> 9;
		polynomials[0][2] = (smoothChannels[3] - 1500) >> 9;
		throttle = (smoothChannels[2] - 1000) * 2; // 0...2000
		rollSetpoint = 0;
		pitchSetpoint = 0;
		yawSetpoint = 0;
		if (flightMode == FlightMode::ANGLE || flightMode == FlightMode::ALT_HOLD || flightMode == FlightMode::GPS_VEL) {
			fix32 dRoll;
			fix32 dPitch;
			if (flightMode < FlightMode::GPS_VEL) {
				dRoll = (smoothChannels[0] - 1500) * TO_ANGLE + (FIX_RAD_TO_DEG * roll);
				dPitch = (smoothChannels[1] - 1500) * TO_ANGLE - (FIX_RAD_TO_DEG * pitch);
				rollSetpoint = dRoll * angleModeP;
				pitchSetpoint = dPitch * angleModeP;
			} else if (flightMode == FlightMode::GPS_VEL) {
				fix32 cosfhead = cosFix(magHeading * FIX_DEG_TO_RAD);
				fix32 sinfhead = sinFix(magHeading * FIX_DEG_TO_RAD);
				eVelSetpoint = cosfhead * (smoothChannels[0] - 1500) + sinfhead * (smoothChannels[1] - 1500);
				nVelSetpoint = -sinfhead * (smoothChannels[0] - 1500) + cosfhead * (smoothChannels[1] - 1500);
				eVelSetpoint = eVelSetpoint >> 9; //+-512 => +-1
				nVelSetpoint = nVelSetpoint >> 9; //+-512 => +-1
				eVelSetpoint *= 12; //+-12m/s
				nVelSetpoint *= 12; //+-12m/s
				eVelError = eVelSetpoint - eVel;
				nVelError = nVelSetpoint - nVel;
				eVelErrorSum = eVelErrorSum + eVelError;
				nVelErrorSum = nVelErrorSum + nVelError;
				eVelP = pidGainsHVel[P] * eVelError;
				nVelP = pidGainsHVel[P] * nVelError;
				eVelI = pidGainsHVel[I] * eVelErrorSum;
				nVelI = pidGainsHVel[I] * nVelErrorSum;
				eVelD = pidGainsHVel[D] * (eVel - eVelLast);
				nVelD = pidGainsHVel[D] * (nVel - nVelLast);

				fix32 eVelPID = eVelP + eVelI + eVelD;
				fix32 nVelPID = nVelP + nVelI + nVelD;
				fix32 targetRoll = eVelPID * cosfhead - nVelPID * sinfhead;
				fix32 targetPitch = eVelPID * sinfhead + nVelPID * cosfhead;
				targetRoll = constrain(targetRoll, -MAX_ANGLE, MAX_ANGLE);
				targetPitch = constrain(targetPitch, -MAX_ANGLE, MAX_ANGLE);
				dRoll = targetRoll + (FIX_RAD_TO_DEG * roll);
				dPitch = targetPitch - (FIX_RAD_TO_DEG * pitch);
				rollSetpoint = dRoll * velocityModeP;
				pitchSetpoint = dPitch * velocityModeP;
			}
			for (int i = 1; i < 5; i++) {
				polynomials[i][2] = polynomials[i - 1][2] * polynomials[0][2];
				if (polynomials[0][2] < 0)
					polynomials[i][2] = -polynomials[i][2];
			}

			yawSetpoint = 0;
			for (int i = 0; i < 5; i++)
				yawSetpoint += rateFactors[i][2] * polynomials[i][2];

			if (flightMode == FlightMode::ALT_HOLD || flightMode == FlightMode::GPS_VEL) {
				fix32 t = throttle - 1000;
				static PT1 vVelDFilter(15, 3200);
				static PT1 vVelFFFilter(2, 3200);
				static elapsedMillis setAltSetpointTimer;
				static u32 stickWasCentered = 0;
				// deadband in center of stick
				if (t > 0) {
					t -= 100;
					if (t < 0) t = 0;
				} else {
					t += 100;
					if (t > 0) t = 0;
				}
				// estimate throttle
				vVelSetpoint = t / 180; // +/- 5 m/s
				if (vVelSetpoint == 0) {
					if (!stickWasCentered) {
						setAltSetpointTimer = 0;
						stickWasCentered = 1;
					} else if (setAltSetpointTimer > 1000) {
						if (stickWasCentered == 1) {
							// set altitude setpoint 1s after throttle is centered
							altSetpoint = combinedAltitude;
							stickWasCentered = 2;
						}
						vVelSetpoint += (altSetpoint - combinedAltitude) / 5; // prevent vVel drift slowly
					}
				} else {
					stickWasCentered = 0;
				}
				vVelError = vVelSetpoint - vVel;
				vVelErrorSum = vVelErrorSum + ((vVelFFFilter.update(vVelSetpoint - vVelLastSetpoint).abs() < fix32(0.001f)) ? vVelError : vVelError / 2); // reduce windup during fast changes
				vVelErrorSum = constrain(vVelErrorSum, vVelMinErrorSum, vVelMaxErrorSum);
				vVelP = pidGainsVVel[P] * vVelError;
				vVelI = pidGainsVVel[I] * vVelErrorSum;
				vVelD = pidGainsVVel[D] * vVelDFilter.update(vVelLast - vVel);
				vVelFF = pidGainsVVel[FF] * vVelFFFilter * 2;
				vVelLastSetpoint = vVelSetpoint;
				throttle = vVelP + vVelI + vVelD + vVelFF;
				throttle = constrain(throttle.geti32(), IDLE_PERMILLE * 2, 2000);
			}
			vVelLast = vVel;
		} else if (flightMode == FlightMode::ACRO) {
			/*
			 * at full stick deflection, ...Raw values are either +1 or -1. That will make all the
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
		rollError = rollSetpoint - gyroData[AXIS_ROLL];
		pitchError = pitchSetpoint - gyroData[AXIS_PITCH];
		yawError = yawSetpoint - gyroData[AXIS_YAW];
		if (ELRS->channels[2] > 1020)
			takeoffCounter++;
		else if (takeoffCounter < 1000) // 1000 = ca. 0.3s
			takeoffCounter = 0; // if the quad hasn't "taken off" yet, reset the counter
		if (takeoffCounter < 1000) // enable i term falloff (windup prevention) only before takeoff
		{
			rollErrorSum = rollErrorSum * pidGains[0][iFalloff];
			pitchErrorSum = pitchErrorSum * pidGains[1][iFalloff];
			yawErrorSum = yawErrorSum * pidGains[2][iFalloff];
		}

		rollErrorSum = rollErrorSum + rollError;
		pitchErrorSum = pitchErrorSum + pitchError;
		yawErrorSum = yawErrorSum + yawError;

		rollP = pidGains[0][P] * rollError;
		pitchP = pidGains[1][P] * pitchError;
		yawP = pidGains[2][P] * yawError;
		rollI = pidGains[0][I] * rollErrorSum;
		pitchI = pidGains[1][I] * pitchErrorSum;
		yawI = pidGains[2][I] * yawErrorSum;
		rollD = pidGains[0][D] * dFilterRoll.update(rollLast - gyroData[AXIS_ROLL]);
		pitchD = pidGains[1][D] * dFilterPitch.update(pitchLast - gyroData[AXIS_PITCH]);
		yawD = pidGains[2][D] * dFilterYaw.update(yawLast - gyroData[AXIS_YAW]);
		rollFF = pidGains[0][FF] * (rollSetpoint - rollSetpoints[ffBufPos]);
		pitchFF = pidGains[1][FF] * (pitchSetpoint - pitchSetpoints[ffBufPos]);
		yawFF = pidGains[2][FF] * (yawSetpoint - yawSetpoints[ffBufPos]);
		rollS = pidGains[0][S] * rollSetpoint;
		pitchS = pidGains[1][S] * pitchSetpoint;
		yawS = pidGains[2][S] * yawSetpoint;

		rollSetpoints[ffBufPos] = rollSetpoint;
		pitchSetpoints[ffBufPos] = pitchSetpoint;
		yawSetpoints[ffBufPos] = yawSetpoint;
		ffBufPos++;
		ffBufPos &= 7;

		fix32 rollTerm = rollP + rollI + rollD + rollFF + rollS;
		fix32 pitchTerm = pitchP + pitchI + pitchD + pitchFF + pitchS;
		fix32 yawTerm = yawP + yawI + yawD + yawFF + yawS;
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
		throttles[(u8)MOTOR::RR] = map(tRR.geti32(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		throttles[(u8)MOTOR::RL] = map(tRL.geti32(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		throttles[(u8)MOTOR::FR] = map(tFR.geti32(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		throttles[(u8)MOTOR::FL] = map(tFL.geti32(), 0, 2000, IDLE_PERMILLE * 2, 2000);
		if (throttles[(u8)MOTOR::RR] > 2000) {
			i16 diff = throttles[(u8)MOTOR::RR] - 2000;
			throttles[(u8)MOTOR::RR] = 2000;
			throttles[(u8)MOTOR::FL] -= diff;
			throttles[(u8)MOTOR::FR] -= diff;
			throttles[(u8)MOTOR::RL] -= diff;
		}
		if (throttles[(u8)MOTOR::RL] > 2000) {
			i16 diff = throttles[(u8)MOTOR::RL] - 2000;
			throttles[(u8)MOTOR::RL] = 2000;
			throttles[(u8)MOTOR::FL] -= diff;
			throttles[(u8)MOTOR::FR] -= diff;
			throttles[(u8)MOTOR::RR] -= diff;
		}
		if (throttles[(u8)MOTOR::FR] > 2000) {
			i16 diff = throttles[(u8)MOTOR::FR] - 2000;
			throttles[(u8)MOTOR::FR] = 2000;
			throttles[(u8)MOTOR::FL] -= diff;
			throttles[(u8)MOTOR::RL] -= diff;
			throttles[(u8)MOTOR::RR] -= diff;
		}
		if (throttles[(u8)MOTOR::FL] > 2000) {
			i16 diff = throttles[(u8)MOTOR::FL] - 2000;
			throttles[(u8)MOTOR::FL] = 2000;
			throttles[(u8)MOTOR::RL] -= diff;
			throttles[(u8)MOTOR::RR] -= diff;
			throttles[(u8)MOTOR::FR] -= diff;
		}
		if (throttles[(u8)MOTOR::RR] < IDLE_PERMILLE * 2) {
			i16 diff = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::RR];
			throttles[(u8)MOTOR::RR] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::FL] += diff;
			throttles[(u8)MOTOR::FR] += diff;
			throttles[(u8)MOTOR::RL] += diff;
		}
		if (throttles[(u8)MOTOR::RL] < IDLE_PERMILLE * 2) {
			i16 diff = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::RL];
			throttles[(u8)MOTOR::RL] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::FL] += diff;
			throttles[(u8)MOTOR::FR] += diff;
			throttles[(u8)MOTOR::RR] += diff;
		}
		if (throttles[(u8)MOTOR::FR] < IDLE_PERMILLE * 2) {
			i16 diff = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::FR];
			throttles[(u8)MOTOR::FR] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::FL] += diff;
			throttles[(u8)MOTOR::RL] += diff;
			throttles[(u8)MOTOR::RR] += diff;
		}
		if (throttles[(u8)MOTOR::FL] < IDLE_PERMILLE * 2) {
			i16 diff = IDLE_PERMILLE * 2 - throttles[(u8)MOTOR::FL];
			throttles[(u8)MOTOR::FL] = IDLE_PERMILLE * 2;
			throttles[(u8)MOTOR::RL] += diff;
			throttles[(u8)MOTOR::RR] += diff;
			throttles[(u8)MOTOR::FR] += diff;
		}
		for (int i = 0; i < 4; i++)
			throttles[i] = throttles[i] > 2000 ? 2000 : throttles[i];
		sendThrottles(throttles);
		rollLast = gyroData[AXIS_ROLL];
		pitchLast = gyroData[AXIS_PITCH];
		yawLast = gyroData[AXIS_YAW];
		if ((pidLoopCounter % bbFreqDivider) == 0 && bbFreqDivider) {
			writeSingleFrame();
		}
		pidLoopCounter++;
	} else {
		// Quad disarmed or RC disconnected
		// all motors off
		if (mspOverrideMotors > 1000)
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
		rollErrorSum = 0;
		pitchErrorSum = 0;
		yawErrorSum = 0;
		rollLast = 0;
		pitchLast = 0;
		yawLast = 0;
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