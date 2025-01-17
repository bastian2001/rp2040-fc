#include "global.h"

// COORDINATE SYSTEM:
// X: forward / roll right
// Y: right / pitch up
// Z: down / yaw right
// (Tait-Bryan angles)

// on the PCB (v0.4):
// X: right / pitch up
// Y: forward / roll right
// Z: up / yaw left

const f32 RAW_TO_RAD_PER_SEC = PI * 4000 / 65536 / 180; // 2000deg per second, but raw is only +/-.5
const f32 FRAME_TIME = 1. / 3200;
const f32 RAW_TO_HALF_ANGLE = RAW_TO_RAD_PER_SEC * FRAME_TIME / 2;
const f32 ANGLE_CHANGE_LIMIT = .00002;
const fix32 RAW_TO_M_PER_SEC2 = (9.81 * 32 + 0.5) / 65536; // +/-16g (0.5 for rounding)
PT1 accelDataFiltered[3] = {PT1(100, 3200), PT1(100, 3200), PT1(100, 3200)};

fix32 roll, pitch, yaw;
fix32 combinedHeading; // NOT heading of motion, but heading of quad
fix32 cosPitch, cosRoll, sinPitch, sinRoll, cosHeading, sinHeading;
PT1 magHeadingCorrection(.02, 75); // 0.1Hz cutoff frequency with 75Hz update rate
fix32 vVel, combinedAltitude, vVelHelper;
PT1 eVel(.2, 10);
PT1 nVel(.2, 10);
fix32 vAccel;

Quaternion q;

void imuInit() {
	pitch = 0; // pitch up
	roll = 0; // roll right
	yaw = 0; // yaw right
	q.w = 1;
	q.v[0] = 0;
	q.v[1] = 0;
	q.v[2] = 0;
	initFixTrig();
	magHeadingCorrection.setRolloverParams(-PI, PI);
}

void __not_in_flash_func(updateFromGyro)() {
	// quaternion of all 3 axis rotations combined

	f32 all[] = {-gyroDataRaw[1] * RAW_TO_HALF_ANGLE, -gyroDataRaw[0] * RAW_TO_HALF_ANGLE, gyroDataRaw[2] * RAW_TO_HALF_ANGLE};
	Quaternion buffer = q;
	q.w += (-buffer.v[0] * all[0] - buffer.v[1] * all[1] - buffer.v[2] * all[2]);
	q.v[0] += (+buffer.w * all[0] - buffer.v[1] * all[2] + buffer.v[2] * all[1]);
	q.v[1] += (+buffer.w * all[1] + buffer.v[0] * all[2] - buffer.v[2] * all[0]);
	q.v[2] += (+buffer.w * all[2] - buffer.v[0] * all[1] + buffer.v[1] * all[0]);

	Quaternion_normalize(&q, &q);
}

f32 orientation_vector[3];
void __not_in_flash_func(updateFromAccel)() {
	// filter accel data
	accelDataFiltered[0].update(accelDataRaw[0]);
	accelDataFiltered[1].update(accelDataRaw[1]);
	accelDataFiltered[2].update(accelDataRaw[2]);

	// Formula from http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/transforms/index.htm
	// p2.x = w*w*p1.x + 2*y*w*p1.z - 2*z*w*p1.y + x*x*p1.x + 2*y*x*p1.y + 2*z*x*p1.z - z*z*p1.x - y*y*p1.x;
	// p2.y = 2*x*y*p1.x + y*y*p1.y + 2*z*y*p1.z + 2*w*z*p1.x - z*z*p1.y + w*w*p1.y - 2*x*w*p1.z - x*x*p1.y;
	// p2.z = 2*x*z*p1.x + 2*y*z*p1.y + z*z*p1.z - 2*w*y*p1.x - y*y*p1.z + 2*w*x*p1.y - x*x*p1.z + w*w*p1.z;
	// with p1.x = 0, p1.y = 0, p1.z = -1, things can be simplified

	orientation_vector[0] = q.w * q.v[1] * -2 + q.v[0] * q.v[2] * -2;
	orientation_vector[1] = q.v[1] * q.v[2] * -2 + q.w * q.v[0] * 2;
	orientation_vector[2] = -q.v[2] * q.v[2] + q.v[1] * q.v[1] + q.v[0] * q.v[0] - q.w * q.w;

	f32 accelVectorNorm = sqrtf((i32)accelDataRaw[1] * (i32)accelDataRaw[1] + (i32)accelDataRaw[0] * (i32)accelDataRaw[0] + (i32)accelDataRaw[2] * (i32)accelDataRaw[2]);
	f32 accelVector[3];
	if (accelVectorNorm > 0.01f) {
		f32 invAccelVectorNorm = 1 / accelVectorNorm;
		accelVector[0] = invAccelVectorNorm * accelDataRaw[1];
		accelVector[1] = invAccelVectorNorm * accelDataRaw[0];
		accelVector[2] = invAccelVectorNorm * -accelDataRaw[2];
	} else
		return;
	Quaternion shortest_path;
	Quaternion_from_unit_vecs(orientation_vector, accelVector, &shortest_path);

	f32 axis[3];
	f32 accAngle = Quaternion_toAxisAngle(&shortest_path, axis); // reduces effect of accel noise on attitude

	if (accAngle > ANGLE_CHANGE_LIMIT) accAngle = ANGLE_CHANGE_LIMIT;

	// Quaternion c;
	// Quaternion_fromAxisAngle(axis, accAngle, &c);
	// Quaternion_multiply(&c, &q, &q);
	f32 c[3]; // correction quaternion, but w is 1
	f32 co = accAngle * 0.5f;
	c[0] = axis[0] * co;
	c[1] = axis[1] * co;
	c[2] = axis[2] * co;

	Quaternion buffer;
	buffer.w = q.w - c[0] * q.v[0] - c[1] * q.v[1] - c[2] * q.v[2];
	buffer.v[0] = c[0] * q.w + q.v[0] + c[1] * q.v[2] - c[2] * q.v[1];
	buffer.v[1] = q.v[1] - c[0] * q.v[2] + c[1] * q.w + c[2] * q.v[0];
	buffer.v[2] = q.v[2] + c[0] * q.v[1] - c[1] * q.v[0] + c[2] * q.w;
	q = buffer;

	Quaternion_normalize(&q, &q);
}

void __not_in_flash_func(updatePitchRollValues)() {
	startFixTrig();
	roll = atan2Fix(2 * (q.w * q.v[0] - q.v[1] * q.v[2]), 1 - 2 * (q.v[0] * q.v[0] + q.v[1] * q.v[1]));
	pitch = asinf(2 * (q.w * q.v[1] + q.v[2] * q.v[0]));
	yaw = atan2Fix(2 * (q.v[0] * q.v[1] - q.w * q.v[2]), 1 - 2 * (q.v[1] * q.v[1] + q.v[2] * q.v[2]));
	fix32 temp = (fix32)magHeadingCorrection + yaw;
	if (temp >= FIX_PI) {
		temp -= FIX_2PI;
	} else if (temp < -FIX_PI) {
		temp += FIX_2PI;
	}
	combinedHeading = temp;
}

fix32 rAccel, fAccel;
fix32 nAccel, eAccel;

void updateSpeeds() {
	fix32 preHelper = vVelHelper;
	cosPitch = cosFix(pitch);
	cosRoll = cosFix(roll);
	cosHeading = cosFix(combinedHeading);
	sinPitch = sinFix(pitch);
	sinRoll = sinFix(roll);
	sinHeading = sinFix(combinedHeading);
	vAccel = cosRoll * cosPitch * accelDataFiltered[2] * RAW_TO_M_PER_SEC2;
	vAccel += sinRoll * cosPitch * accelDataFiltered[0] * RAW_TO_M_PER_SEC2;
	vAccel -= sinPitch * accelDataFiltered[1] * RAW_TO_M_PER_SEC2;
	vVelHelper += (vAccel - fix32(9.81f)) / 3200;
	vVelHelper = fix32(0.9999f) * vVelHelper + 0.0001f * baroUpVel; // this leaves a steady-state error if the accelerometer has a DC offset
	vVel += vVelHelper - preHelper;
	f32 measVel;
	if (gpsStatus.fixType == FIX_3D) {
		measVel = -gpsMotion.velD * 0.0000001f;
	} else {
		measVel = 0.0001f * baroUpVel;
	}
	vVel = 0.9999f * vVel.getf32() + measVel; // this eliminates that error without introducing a lot of lag
	combinedAltitude += vVel / 3200;
	combinedAltitude = 0.9999f * combinedAltitude.getf32() + 0.0001f * gpsBaroAlt.getf32();

	fix32 rightAccel = cosRoll * accelDataFiltered[0] - sinRoll * accelDataFiltered[2];
	fix32 forwardAccel = cosPitch * accelDataFiltered[1] + sinPitch * sinRoll * accelDataFiltered[0] + sinPitch * cosRoll * accelDataFiltered[2];
	fix32 northAccel = forwardAccel * cosHeading - rightAccel * sinHeading;
	fix32 eastAccel = rightAccel * cosHeading + forwardAccel * sinHeading;
	rAccel = rightAccel;
	fAccel = forwardAccel;
	nAccel = northAccel;
	eAccel = eastAccel;

	// static u8 counter = 0;
	// if (++counter == 10) {
	// 	counter = 0;
	// 	Serial.printf("%d\n", eastAccel.geti32());
	// }

	eVel.add(eastAccel * RAW_TO_M_PER_SEC2 / 3200);
	nVel.add(northAccel * RAW_TO_M_PER_SEC2 / 3200);
}

void imuUpdate() {
	elapsedMicros taskTimer = 0;
	tasks[TASK_IMU].runCounter++;
	u32 t0, t1, t2, t3;
	elapsedMicros timer = 0;
	updateFromGyro();
	t0 = timer;
	updateFromAccel();
	t1 = timer;
	updatePitchRollValues();
	t2 = timer;
	updateSpeeds();
	t3 = timer;
	tasks[TASK_IMU_GYRO].totalDuration += t0;
	tasks[TASK_IMU_ACCEL].totalDuration += t1 - t0;
	tasks[TASK_IMU_ANGLE].totalDuration += t2 - t1;
	tasks[TASK_IMU_SPEEDS].totalDuration += t3 - t2;
	if (t0 < tasks[TASK_IMU_GYRO].minDuration) {
		tasks[TASK_IMU_GYRO].minDuration = t0;
	}
	if (t0 > tasks[TASK_IMU_GYRO].maxDuration) {
		tasks[TASK_IMU_GYRO].maxDuration = t0;
	}
	if (t1 - t0 < tasks[TASK_IMU_ACCEL].minDuration) {
		tasks[TASK_IMU_ACCEL].minDuration = t1 - t0;
	}
	if (t1 - t0 > tasks[TASK_IMU_ACCEL].maxDuration) {
		tasks[TASK_IMU_ACCEL].maxDuration = t1 - t0;
	}
	if (t2 - t1 < tasks[TASK_IMU_ANGLE].minDuration) {
		tasks[TASK_IMU_ANGLE].minDuration = t2 - t1;
	}
	if (t2 - t1 > tasks[TASK_IMU_ANGLE].maxDuration) {
		tasks[TASK_IMU_ANGLE].maxDuration = t2 - t1;
	}
	if (t3 - t2 > tasks[TASK_IMU_SPEEDS].maxDuration) {
		tasks[TASK_IMU_SPEEDS].maxDuration = t3 - t2;
	}
	if (t3 - t2 < tasks[TASK_IMU_SPEEDS].minDuration) {
		tasks[TASK_IMU_SPEEDS].minDuration = t3 - t2;
	}
	tasks[TASK_IMU_GYRO].runCounter++;
	tasks[TASK_IMU_ACCEL].runCounter++;
	tasks[TASK_IMU_ANGLE].runCounter++;
	tasks[TASK_IMU_SPEEDS].runCounter++;
	u32 duration = taskTimer;
	tasks[TASK_IMU].totalDuration += duration;
	if (duration < tasks[TASK_IMU].minDuration) {
		tasks[TASK_IMU].minDuration = duration;
	}
	if (duration > tasks[TASK_IMU].maxDuration) {
		tasks[TASK_IMU].maxDuration = duration;
	}
}