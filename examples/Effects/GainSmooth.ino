/* AudioEffectGain: smoothed ("de-zippered") gain change
 *
 * AudioAmplifier and AudioMixer4 apply a new gain at the start of the next
 * block, so changing the gain at control rate (an encoder, a pot read from
 * loop(), a MIDI CC) steps the signal and is audible as zipper noise.  This
 * sketch steps the gain the way a quickly turned encoder would, and alternates
 * every 4 seconds between instant steps and 20 ms smoothed steps so the two
 * can be compared by ear.
 *
 * Written for a Teensy 4.x with an I2S DAC such as the PCM5102A, which needs
 * no control interface.  Using the Teensy audio adaptor instead?  Add
 *    AudioControlSGTL5000 sgtl5000;
 * to the objects below and
 *    sgtl5000.setup(); sgtl5000.volume(0.5);
 * to setup().
 *
 * This example code is in the public domain.
 */

#include <Audio.h>

AudioSynthWaveform       waveform;
AudioEffectGain          gain;
AudioOutputI2S           i2s;

AudioConnection          patchCord1(waveform, 0, gain, 0);
AudioConnection          patchCord2(gain, 0, i2s, 0);
AudioConnection          patchCord3(gain, 0, i2s, 1);

elapsedMillis modeTimer;
elapsedMillis stepTimer;

void setup() {
	AudioMemory(8);
	waveform.begin(WAVEFORM_SINE);
	waveform.frequency(1000.0f);
	waveform.amplitude(0.5f);
	gain.setGain_dB(-6.0f);
	gain.setRampMillis(0); // start with the abrupt behaviour
	Serial.begin(9600);
	Serial.println("listening test: 4 s of instant gain steps, then 4 s smoothed");
}

void loop() {
	static float dB = -6.0f;
	static float dir = -1.5f;
	static bool smoothed = false;

	if (modeTimer >= 4000) {
		modeTimer = 0;
		smoothed = !smoothed;
		gain.setRampMillis(smoothed ? 20.0f : 0.0f);
		Serial.printf("ramp: %s\n", smoothed ? "20 ms (smoothed)" : "0 ms (instant steps)");
	}
	if (stepTimer >= 50) { // 20 gain steps per second, as a fast encoder gives
		stepTimer = 0;
		dB += dir;
		if (dB <= -30.0f) { dB = -30.0f; dir = -dir; }
		if (dB >= 0.0f)   { dB = 0.0f;   dir = -dir; }
		gain.setGain_dB(dB);
	}
}
