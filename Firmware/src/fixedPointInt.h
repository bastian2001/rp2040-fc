#include "hardware/interp.h"
#include <Arduino.h>
#pragma once

class fix32;
extern interp_config sinInterpConfig0, sinInterpConfig1;
void initFixTrig();
/**
 * @brief prepares the interpolator for blend mode
 * Call this once before every sinFix/cosFix calculation batch
 */
inline void startFixTrig() {
	interp_set_config(interp0, 0, &sinInterpConfig0);
	interp_set_config(interp0, 1, &sinInterpConfig1);
}
fix32 sinFix(const fix32 x);
fix32 cosFix(const fix32 x);
class fix64 {
	// 48.16 fixed point
private:
	i64 value = 0;

public:
	static fix64 fromRaw(const i64 value);
	fix64(const i32 value = 0);
	fix64(const int value);
	fix64(const f32 value);
	fix64 setRaw(const i64 value);
	i64 getRaw() const;
	f32 getf32() const;
	f64 getf64() const;
	i32 getInt() const;
	fix32 toFixed32() const;
	fix64 operator+(const fix64 other) const;
	fix64 operator-(const fix64 other) const;
	fix64 operator+(const fix32 other) const;
	fix64 operator-(const fix32 other) const;
	fix64 operator+=(const fix32 other);
	fix64 operator*(const fix64 other) const;
	fix32 operator*(const fix32 other) const;
	fix64 operator*(const int other) const;
	fix64 multiply64(const fix32 other) const;
	fix64 operator=(const i32 other);
	fix64 operator=(const int other);
	fix64 operator=(const i64 other);
	fix64 operator=(const fix32 other);
	fix64 operator=(const f32 other);
	fix64 operator=(const f64 other);
	fix64 operator>>(const i32 other) const;
	fix64 operator-() const;
	inline bool operator<(const fix64 other) const {
		return this->value < other.getRaw();
	}
	inline bool operator>(const fix64 other) const {
		return this->value > other.getRaw();
	}
};

class fix32 {
	// 16.16 fixed point
private:
	i32 value = 0;

public:
	static inline fix32 fromRaw(const i32 v) {
		fix32 result;
		return result.setRaw(v);
	};
	inline constexpr fix32(const i32 v = 0) {
		this->value = v << 16;
	};
	inline constexpr fix32(const f32 v) {
		this->value = (i32)(v * 65536);
	};
	inline constexpr fix32(const f64 v) {
		this->value = (i32)(v * 65536);
	};
	inline constexpr fix32(const int v) {
		this->value = v << 16;
	};
	inline constexpr fix32 setRaw(const i32 v) {
		this->value = v;
		return *this;
	};
	inline i32 getRaw() const {
		return this->value;
	};
	inline f32 getf32() const {
		return (f32)this->value / 65536;
	};
	inline f64 getf64() const {
		return (f64)this->value / 65536;
	};
	inline i32 getInt() const {
		return (i32)(this->value >> 16);
	};
	inline fix64 toFixed64() const {
		fix64 result;
		return result.setRaw(((i64)this->value) << 16);
	};
	inline fix32 operator+(const fix32 other) const {
		fix32 result;
		return result.setRaw(this->value + other.getRaw());
	};
	inline fix32 operator-(const fix32 other) const {
		fix32 result;
		return result.setRaw(this->value - other.getRaw());
	};
	inline fix32 operator*(const fix32 other) const {
		fix32 result;
		return result.setRaw((i32)(((i64)this->value * (i64)other.getRaw()) >> 16));
	};
	inline fix32 operator/(const fix32 other) const {
		fix32 result;
		return result.setRaw((i32)((((i64)this->value) << 16) / (i64)other.getRaw()));
	};
	inline fix32 operator/(const i32 other) const {
		fix32 result;
		return result.setRaw(this->value / other);
	};
	inline fix32 operator+=(const fix32 other) {
		this->value += other.getRaw();
		return *this;
	};
	inline fix32 operator-=(const fix32 other) {
		this->value -= other.getRaw();
		return *this;
	};
	inline fix32 operator*=(const fix32 other) {
		this->value = (i32)(((i64)this->value * (i64)other.getRaw()) >> 16);
		return *this;
	};
	inline fix32 operator/=(const fix32 other) {
		this->value = (i32)(((i64)this->value << 16) / (i64)other.getRaw());
		return *this;
	};
	inline fix32 operator/=(const i32 other) {
		this->value /= other;
		return *this;
	};
	inline fix32 operator+(const int other) const {
		fix32 result;
		return result.setRaw(this->value + (other << 16));
	};
	inline fix32 operator+(const f32 other) const {
		fix32 result;
		return result.setRaw(this->value + (i32)(other * 65536));
	};
	inline fix32 operator-(const int other) const {
		fix32 result;
		return result.setRaw(this->value - (other << 16));
	};
	inline fix32 operator*(const int other) const {
		fix32 result;
		return result.setRaw(this->value * other);
	};
	inline fix32 operator*(const i32 other) const {
		fix32 result;
		return result.setRaw(this->value * other);
	};
	inline fix32 operator*(const f64 other) const {
		return *this * fix32(other);
	};
	inline fix32 operator+=(const int other) {
		this->value += (other << 16);
		return *this;
	};
	inline fix32 operator-=(const int other) {
		this->value -= (other << 16);
		return *this;
	};
	inline fix32 operator*=(const int other) {
		this->value *= other;
		return *this;
	};
	inline fix32 operator*=(const f64 other) {
		return *this *= fix32(other);
	};
	inline fix32 operator=(const i32 other) {
		this->value = (other << 16);
		return *this;
	};
	inline fix32 operator=(const int other) {
		this->value = (other << 16);
		return *this;
	};
	inline fix32 operator=(const fix64 other) {
		this->value = (i32)(other.getRaw() >> 16);
		return *this;
	};
	inline fix32 operator=(f32 other) {
		this->value = (i32)(other * 65536);
		return *this;
	};
	inline fix32 operator=(f64 other) {
		this->value = (i32)(other * 65536);
		return *this;
	};
	inline bool operator==(const i32 other) const {
		return this->value == (other << 16);
	};
	inline bool operator==(const fix32 other) const {
		return this->value == other.getRaw();
	};
	inline fix32 operator*(const fix64 other) const {
		fix32 result;
		return result.setRaw((i32)(((i64)this->value * (i64)other.getRaw()) >> 32));
	};
	inline fix64 multiply64(const fix64 other) const {
		fix64 result;
		return result.setRaw((i32)(((i64)this->value * (i64)other.getRaw()) >> 32));
	};
	inline bool operator<(const fix32 other) const {
		return this->value < other.getRaw();
	};
	inline bool operator>(const fix32 other) const {
		return this->value > other.getRaw();
	};
	inline bool operator<=(const fix32 other) const {
		return this->value <= other.getRaw();
	};
	inline bool operator>=(const fix32 other) const {
		return this->value >= other.getRaw();
	};
	inline fix32 operator%=(const fix32 other) {
		this->value %= other.getRaw();
		return *this;
	};
	inline fix32 operator>>(const i32 other) const {
		return fix32::fromRaw(this->value >> other);
	};
	inline fix32 operator<<(const i32 other) const {
		return fix32::fromRaw(this->value << other);
	};
	inline fix32 operator-() const {
		return fix32::fromRaw(-this->value);
	};
};
