/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <Arduino.h>
#include "effect_gain.h"
#include "utility/dspinst.h"

// Gain of exactly zero cannot be approached multiplicatively, so a ramp to
// silence goes down to -100 dB and snaps to zero at the end.  That last step
// is far below audibility for any signal.
#define GAIN_SILENCE 1e-5f

void AudioEffectGain::update(void)
{
	audio_block_t *block;
	float t = target;	// read once, the sketch may change it at any time
	float g = gain;

	if (g == t) {
		// already at the requested gain, nothing to ramp
		rampCount = 0;
		if (g == 1.0f) {
			// unity gain, pass the input through without touching it
			block = receiveReadOnly(0);
			if (block) {
				transmit(block);
				release(block);
			}
			return;
		}
		if (g == 0.0f) {
			// silence, discard any input and transmit nothing
			block = receiveReadOnly(0);
			if (block) release(block);
			return;
		}
	} else if (rampCount == 0 || t != rampTarget) {
		// start a new ramp, from wherever the gain is right now
		rampTarget = t;
		if (rampSamples <= 0) {
			g = t;
			step = 1.0f;
			rampCount = 0;
		} else {
			float gEnd = (t > 0.0f) ? t : GAIN_SILENCE;
			float gStart = (g > 0.0f) ? g : GAIN_SILENCE;
			step = powf(gEnd / gStart, 1.0f / (float)rampSamples);
			rampCount = rampSamples;
		}
	}

	block = receiveWritable(0);
	if (!block) {
		// no audio this time, but keep any ramp moving so a gain change made
		// while the source was silent is finished when it comes back
		for (int i=0; i < AUDIO_BLOCK_SAMPLES && rampCount > 0; i++) {
			g *= step;
			if (--rampCount == 0) g = t;
		}
		gain = g;
		return;
	}

	if (rampCount > 0) {
		float s = step;
		int32_t n = rampCount;
		for (int i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
			int16_t in = block->data[i];
			block->data[i] = float_to_int16_rounded((float)in * g);
			if (n > 0) {
				g *= s;
				if (--n == 0) g = t; // land exactly on the target
			}
		}
		rampCount = n;
	} else {
		for (int i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
			block->data[i] = float_to_int16_rounded((float)block->data[i] * g);
		}
	}
	gain = g;

	transmit(block);
	release(block);
}
