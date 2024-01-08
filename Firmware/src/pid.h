#pragma once
#include "fixedPointInt.h"
#include <Arduino.h>
#define AXIS_ROLL 1
#define AXIS_PITCH 0
#define AXIS_YAW 2

#define P_SHIFT 11
#define I_SHIFT 3
#define D_SHIFT 10
#define FF_SHIFT D_SHIFT
#define S_SHIFT 8 // setpoint follow

extern i16 bmiDataRaw[6];
extern i16 *gyroDataRaw;
extern i16 *accelDataRaw;
extern fix32 imuData[6];
extern fix32 rateFactors[5][3];
enum class PID_GAINS {
	P,
	I,
	D,
	FF,
	S,
	iFalloff
};
extern fix32 pidGains[3][7];
extern fix32 rollSetpoint, pitchSetpoint, yawSetpoint, rollError, pitchError, yawError, rollLast, pitchLast, yawLast, rollLastSetpoint, pitchLastSetpoint, yawLastSetpoint;
extern fix32 rollP, pitchP, yawP, rollI, pitchI, yawI, rollD, pitchD, yawD, rollFF, pitchFF, yawFF, rollS, pitchS, yawS;
extern fix64 rollErrorSum, pitchErrorSum, yawErrorSum;
extern u16 smoothChannels[4];
extern i16 throttles[4];
extern u32 pidLoopCounter;
enum class FLIGHT_MODE {
	ACRO,
	ANGLE,
	ALT_HOLD,
	GPS_VEL, // control the velocity of the drone
	GPS_POS, // set a position and hold it/fly to it
	LENGTH	 // place behind all other modes, acts as a limit for loops etc.
};
extern FLIGHT_MODE flightMode;

void pidLoop();

void initPID();