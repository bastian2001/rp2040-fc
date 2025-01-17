#include "utils/filters.h"
#include "utils/fixedPointInt.h"
#include "utils/quaternion.h"
#include <Arduino.h>

extern fix32 roll, pitch, yaw; // Euler angles of the drone
extern fix32 combinedHeading; // heading of the drone (in rad) by combining the magnetometer and the gyro
extern fix32 cosPitch, cosRoll, sinPitch, sinRoll, cosHeading, sinHeading; // trigonometric values of the Euler angles
extern PT1 magHeadingCorrection; // PT1 filter for the magnetometer heading correction (updated on every compass read)
extern fix32 vVel; // vertical velocity of the drone (up = positive, m/s)
extern fix32 combinedAltitude; // altitude of the drone (in meters ASL) by combining the barometer, GPS and the accelerometer
extern PT1 eVel; // east velocity of the drone (m/s) by GPS + accel (filtered)
extern PT1 nVel; // north velocity of the drone (m/s) by GPS + accel (filtered)
extern PT1 accelDataFiltered[3]; // PT1 filters for the accelerometer data
extern fix32 vAccel; // vertical up acceleration of the drone (m/s^2) provided by the accelerometer

/**
 * @brief initialize the IMU
 * @details setting start values for quaternion, attitude angles and mag filter rollover
 */
void imuInit();
/**
 * @brief update the attitude of the drone
 * @details 1. feeds gyro data into the attitude quaternion, 2. filters and feeds accelerometer values into the quaternion to prevent drift, 3. updates roll, pitch and yaw values, as well as combined heading via the filtered data, 4. updates altitude, vVel and horizontal velocities from accelerometer (and barometer) data
 */
void imuUpdate();