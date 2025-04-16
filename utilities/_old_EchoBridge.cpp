// Echo Bridge Reverb for Hothouse DIY DSP Platform
// Copyright (C) 2025 Your Name <your.email@example.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "daisysp.h"
#include "hothouse.h"
#include <arm_math.h>
#include <string.h>

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;

// Hothouse hardware
Hothouse hw;

// Bypass vars
Led led_bypass;
Led led_freeze;
bool bypass = true;
bool freeze = false;

// Convolution implementation
class ConvolutionReverb {
private:
    // FFT size - adjust based on available memory and desired quality
    static const size_t FFT_SIZE = 1024;
    static const size_t FFT_SIZE_X2 = FFT_SIZE * 2;
    
    // IR parameters
    float* irBuffer;
    size_t irLength;
    float* irFreqReal;
    float* irFreqImag;
    
    // Input buffer
    float* inputBuffer;
    size_t inputBufferPos;
    
    // Output buffer
    float* outputBuffer;
    size_t outputBufferPos;
    
    // Processing buffers
    float* fftBuffer;
    
    // CMSIS FFT instance
    arm_cfft_instance_f32 fftInstance;
    
    // Parameters
    float irLengthMultiplier;  // 0.0-1.0
    float predelayMs;
    size_t predelaySamples;
    float sampleRate;
    float lowCutFreq;
    float highCutFreq;
    bool freezeActive;
    
    // Predelay buffer
    float* predelayBuffer;
    size_t predelayBufferSize;
    size_t predelayBufferPos;
    
    // Filters
    daisysp::Svf lowCutFilter;
    daisysp::Svf highCutFilter;
    
public:
    ConvolutionReverb() {
        // Initialize to nullptr
        irBuffer = nullptr;
        irFreqReal = nullptr;
        irFreqImag = nullptr;
        inputBuffer = nullptr;
        outputBuffer = nullptr;
        fftBuffer = nullptr;
        predelayBuffer = nullptr;
        
        // Default values
        irLength = 0;
        inputBufferPos = 0;
        outputBufferPos = 0;
        irLengthMultiplier = 1.0f;
        predelayMs = 0.0f;
        predelaySamples = 0;
        sampleRate = 48000.0f;
        lowCutFreq = 20.0f;
        highCutFreq = 20000.0f;
        freezeActive = false;
        predelayBufferSize = 0;
        predelayBufferPos = 0;
    }
    
    ~ConvolutionReverb() {
        // Free allocated memory
        if (irBuffer != nullptr) delete[] irBuffer;
        if (irFreqReal != nullptr) delete[] irFreqReal;
        if (irFreqImag != nullptr) delete[] irFreqImag;
        if (inputBuffer != nullptr) delete[] inputBuffer;
        if (outputBuffer != nullptr) delete[] outputBuffer;
        if (fftBuffer != nullptr) delete[] fftBuffer;
        if (predelayBuffer != nullptr) delete[] predelayBuffer;
    }
    
    void Init(float sampleRate, size_t maxIrLength) {
        this->sampleRate = sampleRate;
        
        // Allocate memory for buffers
        irBuffer = new float[maxIrLength]();
        irFreqReal = new float[FFT_SIZE]();
        irFreqImag = new float[FFT_SIZE]();
        inputBuffer = new float[FFT_SIZE]();
        outputBuffer = new float[FFT_SIZE * 2](); // Double size for overlap-add
        fftBuffer = new float[FFT_SIZE * 2]();    // Complex buffer (real, imag pairs)
        
        // Maximum predelay of 500ms
        predelayBufferSize = static_cast<size_t>(sampleRate * 0.5f);
        predelayBuffer = new float[predelayBufferSize]();
        
        // Initialize positions
        inputBufferPos = 0;
        outputBufferPos = 0;
        predelayBufferPos = 0;
        
        // Initialize FFT
        arm_cfft_init_f32(&fftInstance, FFT_SIZE);
        
        // Initialize filters
        lowCutFilter.Init(sampleRate);
        highCutFilter.Init(sampleRate);
        lowCutFilter.SetFreq(lowCutFreq);
        highCutFilter.SetFreq(highCutFreq);
        lowCutFilter.SetRes(0.7f);
        highCutFilter.SetRes(0.7f);
        lowCutFilter.SetDrive(1.0f);
        highCutFilter.SetDrive(1.0f);
        lowCutFilter.SetMode(daisysp::Svf::Mode::HPF);
        highCutFilter.SetMode(daisysp::Svf::Mode::LPF);
    }
    
    bool LoadIR(const float* newIR, size_t newIrLength) {
        // Store the original IR
        irLength = newIrLength;
        memcpy(irBuffer, newIR, newIrLength * sizeof(float));
        
        // Calculate IR in frequency domain
        return UpdateIRFrequencyDomain();
    }
    
    // Update IR frequency domain representation based on current settings
    bool UpdateIRFrequencyDomain() {
        // Clear frequency domain buffers
        memset(irFreqReal, 0, FFT_SIZE * sizeof(float));
        memset(irFreqImag, 0, FFT_SIZE * sizeof(float));
        
        // Create a temporary buffer for FFT processing
        float temp[FFT_SIZE * 2] = {0};
        
        // Calculate effective IR length based on multiplier
        size_t effectiveIrLength = static_cast<size_t>(irLength * irLengthMultiplier);
        if (effectiveIrLength > FFT_SIZE) effectiveIrLength = FFT_SIZE;
        
        // Copy IR to temporary buffer (real parts only)
        for (size_t i = 0; i < effectiveIrLength; i++) {
            temp[i * 2] = irBuffer[i];
            temp[i * 2 + 1] = 0.0f;  // Imaginary part is zero
        }
        
        // Perform FFT
        arm_cfft_f32(&fftInstance, temp, 0, 1);
        
        // Store results
        for (size_t i = 0; i < FFT_SIZE; i++) {
            irFreqReal[i] = temp[i * 2];
            irFreqImag[i] = temp[i * 2 + 1];
        }
        
        return true;
    }
    
    // Set IR length multiplier (0.0-1.0)
    void SetIRLength(float multiplier) {
        if (multiplier < 0.0f) multiplier = 0.0f;
        if (multiplier > 1.0f) multiplier = 1.0f;
        
        if (multiplier != irLengthMultiplier) {
            irLengthMultiplier = multiplier;
            UpdateIRFrequencyDomain();
        }
    }
    
    // Set predelay in milliseconds
    void SetPredelay(float ms) {
        predelayMs = ms;
        predelaySamples = static_cast<size_t>(predelayMs * sampleRate / 1000.0f);
        if (predelaySamples >= predelayBufferSize) {
            predelaySamples = predelayBufferSize - 1;
        }
    }
    
    // Set low cut frequency
    void SetLowCut(float freq) {
        if (freq < 20.0f) freq = 20.0f;
        if (freq > 2000.0f) freq = 2000.0f;
        lowCutFreq = freq;
        lowCutFilter.SetFreq(lowCutFreq);
    }
    
    // Set high cut frequency
    void SetHighCut(float freq) {
        if (freq < 1000.0f) freq = 1000.0f;
        if (freq > 20000.0f) freq = 20000.0f;
        highCutFreq = freq;
        highCutFilter.SetFreq(highCutFreq);
    }
    
    // Set freeze state
    void SetFreeze(bool state) {
        freezeActive = state;
    }
    
    // Process a single audio sample
    float Process(float in) {
        // Store in predelay buffer
        predelayBuffer[predelayBufferPos] = in;
        predelayBufferPos = (predelayBufferPos + 1) % predelayBufferSize;
        
        // Get sample from predelay buffer
        size_t readPos = (predelayBufferPos + predelayBufferSize - predelaySamples) % predelayBufferSize;
        float predelayedSample = predelayBuffer[readPos];
        
        // If freeze is active, don't add new input
        if (!freezeActive) {
            // Add to input buffer
            inputBuffer[inputBufferPos] = predelayedSample;
        } else {
            // In freeze mode, don't add new input
            inputBuffer[inputBufferPos] = 0.0f;
        }
        
        // Get output sample
        float out = outputBuffer[outputBufferPos];
        
        // Reset output buffer position for next time
        outputBuffer[outputBufferPos] = 0.0f;
        
        // Update buffer positions
        inputBufferPos = (inputBufferPos + 1) % FFT_SIZE;
        outputBufferPos = (outputBufferPos + 1) % (FFT_SIZE * 2);
        
        // If we've filled the input buffer, process an FFT block
        if (inputBufferPos == 0) {
            ProcessFFTBlock();
        }
        
        // Apply filters
        out = lowCutFilter.Process(out);
        out = highCutFilter.Process(out);
        
        return out;
    }
    
private:
    // Process a block using FFT convolution
    void ProcessFFTBlock() {
        // Clear fftBuffer
        memset(fftBuffer, 0, FFT_SIZE_X2 * sizeof(float));
        
        // Copy input buffer to fftBuffer (real parts only)
        for (size_t i = 0; i < FFT_SIZE; i++) {
            fftBuffer[i * 2] = inputBuffer[i];
            fftBuffer[i * 2 + 1] = 0.0f;  // Imaginary part is zero
        }
        
        // Forward FFT
        arm_cfft_f32(&fftInstance, fftBuffer, 0, 1);
        
        // Complex multiplication (convolution in frequency domain)
        for (size_t i = 0; i < FFT_SIZE; i++) {
            float realA = fftBuffer[i * 2];
            float imagA = fftBuffer[i * 2 + 1];
            float realB = irFreqReal[i];
            float imagB = irFreqImag[i];
            
            // Complex multiplication
            fftBuffer[i * 2] = realA * realB - imagA * imagB;
            fftBuffer[i * 2 + 1] = realA * imagB + imagA * realB;
        }
        
        // Inverse FFT
        arm_cfft_f32(&fftInstance, fftBuffer, 1, 1);
        
        // Overlap-add to output buffer
        for (size_t i = 0; i < FFT_SIZE; i++) {
            // Scale by FFT size
            float sample = fftBuffer[i * 2] / FFT_SIZE;
            
            // Add to output buffer with overlap
            outputBuffer[(outputBufferPos + i) % (FFT_SIZE * 2)] += sample;
        }
    }
};

// Create one instance of the convolution reverb
ConvolutionReverb convReverb;

// Example Echo Bridge IR (placeholder - replace with your actual IR)
// This is just a placeholder decaying impulse for testing
const size_t echoBridgeIR_SIZE = 48000; // 1 second at 48kHz
float echoBridgeIR[48000];

// Generate a test IR (to be replaced with your recorded IR)
void GenerateTestIR() {
    for (size_t i = 0; i < echoBridgeIR_SIZE; i++) {
        float decay = exp(-5.0f * i / echoBridgeIR_SIZE); // Exponential decay
        
        // Add some "echo" character with spaced reflections
        if (i % 4800 < 100) { // Every 100ms
            decay *= 1.5f;
        }
        
        echoBridgeIR[i] = decay * ((i == 0) ? 1.0f : 0.7f);
    }
}

// Control variables
float dryWetMix = 0.5f;  // 0.0 = dry, 1.0 = wet
float predelayMs = 0.0f; // 0-500ms
float irLength = 1.0f;   // 0.1-1.0
float lowCutFreq = 20.0f; // 20-2000Hz
float highCutFreq = 20000.0f; // 1000-20000Hz
float outputLevel = 1.0f; // 0.0-1.0
int irMode = 0;          // 0=Full, 1=Short, 2=Long

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
    hw.ProcessAllControls();

    // Update parameters from knobs
    dryWetMix = hw.knobs[Hothouse::KNOB_1].Value();
    predelayMs = hw.knobs[Hothouse::KNOB_2].Value() * 500.0f; // 0-500ms
    irLength = 0.1f + hw.knobs[Hothouse::KNOB_3].Value() * 0.9f; // 10-100%
    lowCutFreq = 20.0f + hw.knobs[Hothouse::KNOB_4].Value() * 1980.0f; // 20-2000Hz
    highCutFreq = 1000.0f + hw.knobs[Hothouse::KNOB_5].Value() * 19000.0f; // 1000-20000Hz
    outputLevel = hw.knobs[Hothouse::KNOB_6].Value(); // 0-1.0
    
    // Get IR mode from switch 1
    switch (hw.switches[Hothouse::SWITCH_1].Position()) {
        case 0: irMode = 0; break; // UP - Full bridge
        case 1: irMode = 1; break; // MIDDLE - Short echo
        case 2: irMode = 2; break; // DOWN - Long decay
    }
    
    // Check for freezing from switch 2 and footswitch 1
    bool freeze_switch = hw.switches[Hothouse::SWITCH_2].Position() == 2; // DOWN position
    bool freeze_pedal = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
    freeze = freeze_switch || freeze_pedal;
    
    // Toggle bypass when FOOTSWITCH_2 is pressed
    bypass ^= hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();
    
    // Update reverb parameters
    convReverb.SetPredelay(predelayMs);
    convReverb.SetIRLength(irLength);
    convReverb.SetLowCut(lowCutFreq);
    convReverb.SetHighCut(highCutFreq);
    convReverb.SetFreeze(freeze);
    
    // Process audio
    for (size_t i = 0; i < size; ++i) {
        // Get input sample (just use left channel for mono in)
        float in_sample = in[0][i];
        
        if (bypass) {
            // Bypass mode - just pass through
            out[0][i] = out[1][i] = in_sample;
        } else {
            // Process through reverb
            float wet_sample = convReverb.Process(in_sample);
            
            // Mix dry and wet signals
            float mixed = (1.0f - dryWetMix) * in_sample + dryWetMix * wet_sample;
            
            // Apply output level
            mixed *= outputLevel;
            
            // Write to both outputs
            out[0][i] = out[1][i] = mixed;
        }
    }
}

int main() {
    hw.Init();
    hw.SetAudioBlockSize(48);  // Number of samples handled per callback
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // Initialize LEDs
    led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);
    led_freeze.Init(hw.seed.GetPin(Hothouse::LED_1), false);

    // Generate test IR
    GenerateTestIR();
    
    // Initialize convolution reverb
    float sampleRate = hw.AudioSampleRate();
    convReverb.Init(sampleRate, echoBridgeIR_SIZE);
    convReverb.LoadIR(echoBridgeIR, echoBridgeIR_SIZE);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while (true) {
        hw.DelayMs(10);

        // Update LEDs
        led_bypass.Set(bypass ? 0.0f : 1.0f);
        led_freeze.Set(freeze ? 1.0f : 0.0f);
        led_bypass.Update();
        led_freeze.Update();

        // Call System::ResetToBootloader() if FOOTSWITCH_1 is pressed for 2 seconds
        hw.CheckResetToBootloader();
    }
    return 0;
}