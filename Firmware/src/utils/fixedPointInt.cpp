#include "global.h"
#include "hardware/interp.h"

fix32 sinLut[257];
fix32 atanLut[257];
interp_config sinInterpConfig0, sinInterpConfig1;
const fix32 FIX_PI = PI;
const fix32 FIX_2PI = 2 * PI;
const fix32 FIX_PI_2 = PI / 2;
const fix32 FIX_RAD_TO_DEG = 180 / PI;
const fix32 FIX_DEG_TO_RAD = PI / 180;

void initFixTrig() {
	for (int i = 0; i <= 256; i++) {
		sinLut[i] = sin(i * PI / 256);
		atanLut[i] = atan((f64)i / 256);
	}
	sinInterpConfig0 = interp_default_config();
	sinInterpConfig1 = interp_default_config();
	interp_config_set_blend(&sinInterpConfig0, 1);
}

/**
 * @brief calculates the sine of a fixed point number, faster than sinf
 * @details accurate to about 0.0001. Important: Call initFixTrig() once at the start.
 * Also call startFixTrig() once before every sinFix/cosFix calculation batch to prepare the interpolator for blend mode
 * @param x
 * @return fix32
 */
fix32 sinFix(const fix32 x) {
	i32 xNew = (x / FIX_PI).raw;
	i32 sign = 1 - ((xNew >> 16) & 1) * 2; // 1 if 0 <= x < PI +/-2n*PI, -1 otherwise
	xNew &= 0xFFFF; // %= PI
	u32 high = xNew >> 8;
	interp0->accum[1] = xNew & 0xFF;
	interp0->base[0] = sinLut[high].raw;
	interp0->base[1] = sinLut[high + 1].raw;
	return fix32().setRaw(interp0->peek[1] * sign);
}
/**
 * @brief calculates the cosine of a fixed point number, faster than cosf
 * @details accurate to about 0.0001. Important: Call initFixTrig() once at the start. Also call startFixTrig() once before every sinFix/cosFix/atanFix calculation batch to prepare the interpolator for blend mode
 * @param x radians
 * @return fix32 radians
 */
fix32 cosFix(const fix32 x) { return sinFix(x + FIX_PI_2); }

/**
 * @brief calculates the atan of a fixed point number, about as fast as atanf
 * @details accurate to about +-0.00003. Important: Call initFixTrig() once at the start. Also call startFixTrig() once before every sinFix/cosFix/atanFix calculation batch to prepare the interpolator for blend mode
 *
 * @param x radians
 * @return fix32 radians
 */
fix32 atanFix(fix32 x) {
	i32 sign = x.sign();
	i32 offset = 0;
	x *= sign;
	if (x > fix32(1)) {
		x = fix32(1) / x;
		offset = FIX_PI_2.raw * sign;
		sign = -sign;
	} else if (x == 1) {
		return fix32().setRaw((FIX_PI_2.raw >> 1) * sign);
	}
	i32 &xRaw = x.raw;
	u32 high = xRaw >> 8;
	interp0->accum[1] = xRaw & 0xFF;
	interp0->base[0] = atanLut[high].raw;
	interp0->base[1] = atanLut[high + 1].raw;
	return fix32().setRaw((i32)interp0->peek[1] * sign + offset);
}
fix32 atan2Fix(const fix32 y, const fix32 x) {
	if (x != 0)
		return atanFix(y / x) + FIX_PI * (x.raw < 0) * y.sign();
	return FIX_PI_2 * y.sign();
}