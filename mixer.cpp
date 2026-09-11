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
#include "mixer.h"
#include "utility/dspinst.h"

#if defined(__ARM_ARCH_7EM__)
#define MULTI_UNITYGAIN 65536

// Gain multiply with round-to-nearest instead of the floor() that a plain
// smulwb()/smulwt() gives.  The 16 bit sample is passed zero extended in the
// low half of `sample`; moving it up into the high half scales by 1/65536 for
// the >>32 of smmulr(), and smmulr()'s added half-LSB (0x80000000) becomes the
// rounding term.
static inline int32_t multiplyGain(int32_t mult, uint32_t sample)
{
	return multiply_32x32_rshift32_rounded(mult, (int32_t)((sample & 0xFFFF) << 16));
}

static void applyGain(int16_t *data, int32_t mult)
{
	uint32_t *p = (uint32_t *)data;
	const uint32_t *end = (uint32_t *)(data + AUDIO_BLOCK_SAMPLES);

	do {
		uint32_t tmp32 = *p; // read 2 samples from *data
		int32_t val1 = multiplyGain(mult, tmp32);
		int32_t val2 = multiplyGain(mult, tmp32 >> 16);
		val1 = signed_saturate_rshift(val1, 16, 0);
		val2 = signed_saturate_rshift(val2, 16, 0);
		*p++ = pack_16b_16b(val2, val1);
	} while (p < end);
}

// acc[i] += round(mult * in[i] / 65536), keeping the full 32 bit result.
// Quantizing and saturating once on the final mix avoids requantizing
// once per channel, this avoids clipping partial sums along the way.
static void applyGainAccumulate(int32_t *acc, const int16_t *in, int32_t mult)
{
	const uint32_t *p = (const uint32_t *)in;
	const uint32_t *end = (const uint32_t *)(in + AUDIO_BLOCK_SAMPLES);

	if (mult == MULTI_UNITYGAIN) {
		do {
			uint32_t tmp32 = *p++;
			acc[0] = add_32_saturate(acc[0], (int16_t)tmp32);
			acc[1] = add_32_saturate(acc[1], (int16_t)(tmp32 >> 16));
			acc += 2;
		} while (p < end);
	} else if (mult == 0) {
		// no contribution, accumulator unchanged
	} else {
		do {
			uint32_t tmp32 = *p++;
			acc[0] = add_32_saturate(acc[0], multiplyGain(mult, tmp32));
			acc[1] = add_32_saturate(acc[1], multiplyGain(mult, tmp32 >> 16));
			acc += 2;
		} while (p < end);
	}
}

#elif defined(KINETISL)
#define MULTI_UNITYGAIN 256

static void applyGain(int16_t *data, int32_t mult)
{
	const int16_t *end = data + AUDIO_BLOCK_SAMPLES;

	do {
		// mult is Q8.8 here, so this needs the >> 8 that the ARM version
		// gets for free from smulwb()'s >> 16
		int32_t val = *data * mult + 0x80;
		*data++ = signed_saturate_rshift(val, 16, 8);
	} while (data < end);
}

static void applyGainThenAdd(int16_t *dst, const int16_t *src, int32_t mult)
{
	const int16_t *end = dst + AUDIO_BLOCK_SAMPLES;

	if (mult == MULTI_UNITYGAIN) {
		do {
			int32_t val = *dst + *src++;
			*dst++ = signed_saturate_rshift(val, 16, 0);
		} while (dst < end);
	} else {
		do {
			// |src * mult| <= 32768 * 32512, so no 32 bit overflow here
			int32_t val = *dst + ((*src++ * mult + 0x80) >> 8);
			*dst++ = signed_saturate_rshift(val, 16, 0);
		} while (dst < end);
	}
}

#endif

#if defined(__ARM_ARCH_7EM__)
void AudioMixer4::update(void)
{
	audio_block_t *in, *out=NULL;
	unsigned int channel;
	// Only one object's update() runs at a time (update_all() walks the graph
	// from the audio interrupt), so one shared accumulator is enough.  Too much
	// RAM to spend on Teensy LC, which keeps the old per-add saturation below.
	static int32_t acc[AUDIO_BLOCK_SAMPLES];

	for (channel=0; channel < 4; channel++) {
		if (!out) {
			out = receiveWritable(channel);
			if (out) {
				memset(acc, 0, sizeof(acc));
				applyGainAccumulate(acc, out->data, multiplier[channel]);
			}
		} else {
			in = receiveReadOnly(channel);
			if (in) {
				applyGainAccumulate(acc, in->data, multiplier[channel]);
				release(in);
			}
		}
	}
	if (out) {
		// the whole mix is quantized and clipped exactly once, here
		int16_t *dest = out->data;
		const int32_t *src = acc;
		const int32_t *end = acc + AUDIO_BLOCK_SAMPLES;
		do {
			uint32_t tmp32 = pack_16b_16b(saturate16(src[1]), saturate16(src[0]));
			*(uint32_t *)dest = tmp32;
			dest += 2;
			src += 2;
		} while (src < end);
		transmit(out);
		release(out);
	}
}
#elif defined(KINETISL)
void AudioMixer4::update(void)
{
	audio_block_t *in, *out=NULL;
	unsigned int channel;

	for (channel=0; channel < 4; channel++) {
		if (!out) {
			out = receiveWritable(channel);
			if (out) {
				int32_t mult = multiplier[channel];
				if (mult != MULTI_UNITYGAIN) applyGain(out->data, mult);
			}
		} else {
			in = receiveReadOnly(channel);
			if (in) {
				applyGainThenAdd(out->data, in->data, multiplier[channel]);
				release(in);
			}
		}
	}
	if (out) {
		transmit(out);
		release(out);
	}
}
#endif

void AudioAmplifier::update(void)
{
	audio_block_t *block;
	int32_t mult = multiplier;

	if (mult == 0) {
		// zero gain, discard any input and transmit nothing
		block = receiveReadOnly(0);
		if (block) release(block);
	} else if (mult == MULTI_UNITYGAIN) {
		// unity gain, pass input to output without any change
		block = receiveReadOnly(0);
		if (block) {
			transmit(block);
			release(block);
		}
	} else {
		// apply gain to signal
		block = receiveWritable(0);
		if (block) {
			applyGain(block->data, mult);
			transmit(block);
			release(block);
		}
	}
}
