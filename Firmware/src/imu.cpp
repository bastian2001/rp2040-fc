#include "global.h"

// COORDINATE SYSTEM:
// X: forward / roll right
// Y: right / pitch up
// Z: down / yaw right
// (Tait-Bryan angles)

const f32 RAW_TO_RAD_PER_SEC	 = (f32)(PI * 4000 / 65536 / 180); // 2000deg per second, but raw is only +/-.5
const f32 FRAME_TIME			 = (f32)(1. / 3200);
const f32 RAW_TO_HALF_ANGLE		 = (f32)(RAW_TO_RAD_PER_SEC * FRAME_TIME / 2);
const f32 ANGLE_CHANGE_LIMIT	 = .0002;
const f32 RAW_TO_DELTA_M_PER_SEC = (f32)(9.81 * 32 / 65536); // +/-16g

f32 pitch, roll, yaw, rpAngle;
i32 headMotAtt;		 // heading of motion by attitude, i.e. yaw but with pitch/roll compensation
i32 combinedHeading; // NOT heading of motion, but heading of quad
i32 combinedHeadMot; // heading of motion, but with headingAdjustment applied
fix32 vVel, combinedAltitude;
fix32 eVel, nVel;

Quaternion q;

void imuInit() {
	pitch  = 0; // pitch up
	roll   = 0; // roll right
	yaw	   = 0; // yaw right
	q.w	   = 1;
	q.v[0] = 0;
	q.v[1] = 0;
	q.v[2] = 0;
}

f32 map(f32 x, f32 in_min, f32 in_max, f32 out_min, f32 out_max) {
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void updateFromGyro() {
	// quaternion of all 3 axis rotations combined. All the other terms are dependent on the order of multiplication, therefore we leave them out, as they are arbitrarily defined
	// e.g. defining a q_x, q_y and q_z and then multiplying them in a certain order would result in a different quaternion than multiplying them in a different order. Therefore we omit these inconsistent terms completely
	// f32 all[]	   = {gyroDataRaw[1] * RAW_TO_HALF_ANGLE, gyroDataRaw[0] * RAW_TO_HALF_ANGLE, gyroDataRaw[2] * RAW_TO_HALF_ANGLE};
	Quaternion all = {gyroDataRaw[1] * RAW_TO_HALF_ANGLE, gyroDataRaw[0] * RAW_TO_HALF_ANGLE, gyroDataRaw[2] * RAW_TO_HALF_ANGLE, 1};

	Quaternion_multiply(&all, &q, &q);
	// Quaternion buffer = q;
	// q.w += (-buffer.v[0] * all[0] - buffer.v[1] * all[1] - buffer.v[2] * all[2]);
	// q.v[0] += (+buffer.w * all[0] + buffer.v[1] * all[2] - buffer.v[2] * all[1]);
	// q.v[1] += (+buffer.w * all[1] - buffer.v[0] * all[2] + buffer.v[2] * all[0]);
	// q.v[2] += (+buffer.w * all[2] + buffer.v[0] * all[1] - buffer.v[1] * all[0]);

	Quaternion_normalize(&q, &q);
}

f32 orientation_vector[3];
void updateFromAccel() {
	// Formula from http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/transforms/index.htm
	// p2.x = w*w*p1.x + 2*y*w*p1.z - 2*z*w*p1.y + x*x*p1.x + 2*y*x*p1.y + 2*z*x*p1.z - z*z*p1.x - y*y*p1.x;
	// p2.y = 2*x*y*p1.x + y*y*p1.y + 2*z*y*p1.z + 2*w*z*p1.x - z*z*p1.y + w*w*p1.y - 2*x*w*p1.z - x*x*p1.y;
	// p2.z = 2*x*z*p1.x + 2*y*z*p1.y + z*z*p1.z - 2*w*y*p1.x - y*y*p1.z + 2*w*x*p1.y - x*x*p1.z + w*w*p1.z;
	// with p1.x = 0, p1.y = 0, p1.z = -1, things can be simplified

	orientation_vector[0] = q.w * q.v[1] * -2 + q.v[0] * q.v[2] * -2;
	orientation_vector[1] = q.v[1] * q.v[2] * -2 + q.w * q.v[0] * 2;
	orientation_vector[2] = -q.v[2] * q.v[2] + q.v[1] * q.v[1] + q.v[0] * q.v[0] - q.w * q.w;

	f32 accelVector[3]	= {(f32) - (accelDataRaw[1]), (f32) - (accelDataRaw[0]), (f32) - (accelDataRaw[2])};
	f32 accelVectorNorm = sqrtf((int)accelDataRaw[1] * (int)accelDataRaw[1] + (int)accelDataRaw[0] * (int)accelDataRaw[0] + (int)accelDataRaw[2] * (int)accelDataRaw[2]);
	if (accelVectorNorm > 0.01f) {
		f32 invAccelVectorNorm = 1 / accelVectorNorm;
		accelVector[0] *= invAccelVectorNorm;
		accelVector[1] *= invAccelVectorNorm;
		accelVector[2] *= invAccelVectorNorm;
	}
	Quaternion shortest_path;
	Quaternion_from_unit_vecs(orientation_vector, accelVector, &shortest_path);

	f32 axis[3];
	f32 accAngle = Quaternion_toAxisAngle(&shortest_path, axis); // reduces effect of accel noise on attitude

	if (accAngle > ANGLE_CHANGE_LIMIT) accAngle = ANGLE_CHANGE_LIMIT;

	Quaternion correction;
	Quaternion_fromAxisAngle(axis, accAngle, &correction);

	Quaternion_multiply(&correction, &q, &q);
	Quaternion_normalize(&q, &q);
}

void updatePitchRollValues() {
	Quaternion shortest_path;
	// no recalculation of orientation_vector, as this saves 50µs @132MHz at only a slight loss in precision (accel update delayed by 1 cycle)
	// orientation_vector[0] = q.w * q.v[1] * -2 + q.v[0] * q.v[2] * -2;
	// orientation_vector[1] = q.v[1] * q.v[2] * -2 + q.w * q.v[0] * 2;
	// orientation_vector[2] = -q.v[2] * q.v[2] + q.v[1] * q.v[1] + q.v[0] * q.v[0] - q.w * q.w;

	f32 dot = -orientation_vector[2];

	if (dot > ONE_MINUS_EPS) {
		Quaternion_setIdentity(&shortest_path);
	} else if (dot < -ONE_MINUS_EPS) {
		// Rotate along any orthonormal vec to vec1 or vec2 as the axis.
		shortest_path.w	   = 0;
		shortest_path.v[0] = 0;
		shortest_path.v[1] = -orientation_vector[2];
		shortest_path.v[2] = orientation_vector[1];
	} else {
		shortest_path.w	   = dot + 1;
		shortest_path.v[0] = -orientation_vector[1];
		shortest_path.v[1] = orientation_vector[0];
		shortest_path.v[2] = 0;
		f32 len			   = sqrtf(shortest_path.v[0] * shortest_path.v[0] + shortest_path.v[1] * shortest_path.v[1] + shortest_path.w * shortest_path.w);
		if (len == 0) {
			shortest_path.w	   = 1;
			shortest_path.v[0] = 0;
			shortest_path.v[1] = 0;
			shortest_path.v[2] = 0;
		} else {
			f32 oneOverLen = 1 / len;
			shortest_path.w *= oneOverLen;
			shortest_path.v[0] *= oneOverLen;
			shortest_path.v[1] *= oneOverLen;
			shortest_path.v[2] = 0;
		}
	}

	f32 orientation_correction_axes[3];
	rpAngle = Quaternion_toAxisAngle(&shortest_path, orientation_correction_axes);

	roll  = orientation_correction_axes[0] * rpAngle;
	pitch = orientation_correction_axes[1] * rpAngle;
	yaw	  = atan2f(-2 * (q.v[2] * q.w + q.v[1] * q.v[0]), -1 + 2 * (q.v[0] * q.v[0] + q.w * q.w));

	headMotAtt		= yaw * 5729578;				  // 5729578 = 360 / (2 * PI) * 100000
	combinedHeading = headMotAtt + headingAdjustment; // heading of quad
	if (combinedHeading > 18000000) combinedHeading -= 36000000;
	if (combinedHeading < -18000000) combinedHeading += 36000000;
	if (rpAngle > .2618f) {
		// assume the quad is flying into the direction of pitch and roll, if the angle is larger than 15°
		headMotAtt += atan2f(-orientation_vector[1], -orientation_vector[0]) * 5729578;
		combinedHeadMot = headMotAtt + headingAdjustment; // heading of motion
		if (combinedHeadMot > 18000000) combinedHeadMot -= 36000000;
		if (combinedHeadMot < -18000000) combinedHeadMot += 36000000;
	} else
		combinedHeadMot = combinedHeading;
	vVel += fix32(RAW_TO_DELTA_M_PER_SEC * accelDataRaw[2] * cosf(rpAngle)) / fix32(3200);
	// vVel += fix32(RAW_TO_DELTA_M_PER_SEC * accelDataRaw[0] * sinf(roll)) / fix32(3200);
	// vVel += fix32(RAW_TO_DELTA_M_PER_SEC * accelDataRaw[1] * sinf(pitch)) / fix32(3200);
	vVel -= fix32(9.81f / 3200);
	combinedAltitude += vVel / fix32(3200);
}

void updateAttitude() {
	elapsedMicros taskTimer = 0;
	tasks[TASK_IMU].runCounter++;
	u32 t0, t1, t2;
	elapsedMicros timer = 0;
	updateFromGyro();
	t0 = timer;
	updateFromAccel();
	t1 = timer;
	updatePitchRollValues();
	t2 = timer;
	u32 duration = taskTimer;
	tasks[TASK_IMU].totalDuration += duration;
	if (duration < tasks[TASK_IMU].minDuration) {
		tasks[TASK_IMU].minDuration = duration;
	}
	if (duration > tasks[TASK_IMU].maxDuration) {
		tasks[TASK_IMU].maxDuration = duration;
	}
}