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

#ifndef effect_gain_h_
#define effect_gain_h_

#include <Arduino.h>     // github.com/PaulStoffregen/cores/blob/master/teensy4/Arduino.h
#include <AudioStream.h> // github.com/PaulStoffregen/cores/blob/master/teensy4/AudioStream.h

// Gain with a de-zippered ("smoothed") gain change.
//
// AudioAmplifier and AudioMixer4 apply a new gain at the start of the next
// block, which is a step discontinuity in the signal.  When the gain is
// changed at control rate (an encoder, a pot read in loop(), a MIDI CC) those
// steps are audible as zipper noise.  This object instead moves the gain
// toward the requested value over setRampMillis() milliseconds, multiplicatively
// (a constant number of dB per sample), which is the taper the ear expects for
// volume.  AudioEffectFade is the one-shot version of the same idea.
//
// The multiply is done in float and rounded to nearest, so unlike a
// fixed point gain it introduces no bias of its own.  At unity gain the input
// block is passed through untouched.
//
// setGain()/setRampMillis() are meant to be called from the sketch, update()
// runs from the audio interrupt.  They share only `target` and `rampSamples`,
// and a single aligned 32 bit store/load is atomic on Cortex-M, so no
// interrupt masking is needed.
class AudioEffectGain : public AudioStream
{
public:
	AudioEffectGain(void) : AudioStream(1, inputQueueArray),
		gain(1.0f), target(1.0f), rampTarget(1.0f), step(1.0f),
		rampCount(0), rampSamples(441) {}
	// Requested gain, reached over the ramp time set by setRampMillis().
	// 1.0 is unity, 0.0 is silence, up to 32767.0 like AudioAmplifier.
	void setGain(float n) {
		if (n > 32767.0f) n = 32767.0f;
		else if (n < 0.0f) n = 0.0f;
		target = n;
	}
	void setGain_dB(float dB) {
		setGain(powf(10.0f, dB * 0.05f));
	}
	// Length of the fade to a new gain, 0 = change instantly (default 10 ms)
	void setRampMillis(float milliseconds) {
		if (milliseconds <= 0.0f) rampSamples = 0;
		else rampSamples = (int32_t)(milliseconds * (AUDIO_SAMPLE_RATE_EXACT / 1000.0f) + 0.5f);
	}
	float getGain(void) { return target; }
	float getGain_dB(void) { return 20.0f * log10f(target); }
	virtual void update(void);

private:
	float gain;          // current gain, moving toward target
	float target;        // gain most recently requested
	float rampTarget;    // target the in-progress ramp was planned for
	float step;          // per sample multiplier while ramping
	int32_t rampCount;   // samples left in the in-progress ramp
	int32_t rampSamples; // length of a ramp, 0 = change instantly
	audio_block_t *inputQueueArray[1];
};

#endif
