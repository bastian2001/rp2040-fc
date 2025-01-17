#pragma once
#include "fixedPointInt.h"
#include "typedefs.h"

/**
 * @brief A first order low pass filter
 *
 * @details This filter is a simple first order low pass filter with a rollover function. Internally, it uses fix32 variables
 */
class PT1 {
public:
	/**
	 * @brief Construct a new PT1 object
	 *
	 * @param cutoffFreq Cutoff frequency of the filter
	 * @param sampleFreq Sample frequency of the filter (rate at which .update() is called)
	 */
	PT1(fix32 cutoffFreq, u32 sampleFreq);
	/**
	 * @brief Construct a new PT1 object
	 *
	 * @param alpha Alpha value of the filter, typically close to 0
	 */
	PT1(fix32 alpha);
	/**
	 * @brief provide a new value to the filter
	 *
	 * @param value The new value/sample to be filtered
	 * @return fix32 The filtered value
	 */
	inline fix32 update(fix32 value) {
		y = y + alpha * (value - y);
		return y;
	}
	/**
	 * @brief rollover if the value is out of bounds
	 *
	 * @return fix32 The new value after rollover
	 */
	fix32 rollover();
	/**
	 * @brief Set the Rollover Params object
	 *
	 * @param lowerBound The lower bound of the rollover (inclusive)
	 * @param upperBound The upper bound of the rollover (exclusive)
	 */
	void setRolloverParams(fix32 lowerBound, fix32 upperBound);
	/**
	 * @brief Set a new cutoff frequency for the filter, useful for dynamic filters
	 *
	 * @param cutoffFreq The new cutoff frequency
	 */
	void updateCutoffFreq(fix32 cutoffFreq);
	/**
	 * @brief Set a new alpha value for the filter, useful for dynamic filters
	 *
	 * @param alpha The new alpha value
	 */
	void updateAlpha(fix32 alpha);
	void add(fix32 value) {
		y += value;
	}
	/// @brief Get the current value of the filter
	inline operator fix32() const { return y; }

private:
	fix32 alpha; // close to 0, higher = less filtering
	fix32 y = 0;
	u32 sampleFreq;
	fix32 lowerBound = 0;
	fix32 upperBound = 0;
	fix32 boundDiff = 0;
};