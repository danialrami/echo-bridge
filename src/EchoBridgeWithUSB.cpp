#include "daisysp.h"
#include "hothouse.h"
#include "IRLoader.h"
#include "shy_fft.h"  // Include the ShyFFT library
#include <string.h>

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::System;

// Hothouse hardware
Hothouse hw;

// USB/IR loading
IRLoader irLoader;

// LEDs - Hothouse pedal only has two LEDs
Led led1;  // LED 1 - Used for freeze status and temporarily for IR loading
Led led2;  // LED 2 - Used for bypass status

// State variables
bool bypass = true;
bool freeze = false;
bool usbMounted = false;
bool irLoaded = false;
bool isStereoInput = false; // Flag for stereo detection

// Define FFT sizes
static const size_t EARLY_FFT_SIZE = 64;     // Small FFT for early reflections (low latency)
static const size_t LATE_FFT_SIZE = 1024;    // Larger FFT for late reflections (efficiency)
static const size_t MAX_PREDELAY_SAMPLES = 24000; // 500ms at 48kHz

// Global SDRAM buffers for late partition
DSY_SDRAM_BSS float g_lateInputBuffer[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateInputBufferRight[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateOutputBuffer[LATE_FFT_SIZE * 2];
DSY_SDRAM_BSS float g_lateOutputBufferRight[LATE_FFT_SIZE * 2];
DSY_SDRAM_BSS float g_lateTimeIn[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateTimeOut[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateFreqReal[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateFreqImag[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateIrFreqReal[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateIrFreqImag[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateIrFreqRealRight[LATE_FFT_SIZE];
DSY_SDRAM_BSS float g_lateIrFreqImagRight[LATE_FFT_SIZE];

// Global SDRAM buffers for predelay
DSY_SDRAM_BSS float g_predelayBuffer[MAX_PREDELAY_SAMPLES];
DSY_SDRAM_BSS float g_predelayBufferRight[MAX_PREDELAY_SAMPLES];

// Partitioned Convolution implementation
class PartitionedConvolutionReverb {
private:
    // IR parameters
    float* irBuffer;        // Left/mono IR
    float* irBufferRight;   // Right IR for true stereo
    size_t irLength;
    
    // FFT objects for early and late partitions
    ShyFFT<float, EARLY_FFT_SIZE> earlyFft;
    ShyFFT<float, LATE_FFT_SIZE> lateFft;
    
    // Early partition buffers - small enough to keep in internal memory
    float earlyTimeIn[EARLY_FFT_SIZE];
    float earlyTimeOut[EARLY_FFT_SIZE];
    float earlyFreqReal[EARLY_FFT_SIZE];
    float earlyFreqImag[EARLY_FFT_SIZE];
    float earlyFreqRealRight[EARLY_FFT_SIZE];
    float earlyFreqImagRight[EARLY_FFT_SIZE];
    
    // IR frequency domain for early partition
    float earlyIrFreqReal[EARLY_FFT_SIZE];
    float earlyIrFreqImag[EARLY_FFT_SIZE];
    float earlyIrFreqRealRight[EARLY_FFT_SIZE];
    float earlyIrFreqImagRight[EARLY_FFT_SIZE];
    
    // Input/output buffers for early partition
    float earlyInputBuffer[EARLY_FFT_SIZE];
    float earlyInputBufferRight[EARLY_FFT_SIZE];
    float earlyOutputBuffer[EARLY_FFT_SIZE * 2];
    float earlyOutputBufferRight[EARLY_FFT_SIZE * 2];
    size_t earlyInputBufferPos;
    
    // Late partition buffers - using global SDRAM buffers
    size_t lateInputBufferPos;
    
    // Predelay buffer - using global SDRAM buffers
    size_t predelayBufferPos;
    size_t predelayInSamples;
    
    // Parameters
    float dryWet;           // Dry/wet mix (0.0 - 1.0)
    float predelayMs;       // Predelay in milliseconds
    float irLengthFactor;   // IR length factor (0.0 - 1.0)
    float lowCutFreq;       // Low cut frequency
    float highCutFreq;      // High cut frequency
    float stereoWidth;      // Stereo width (0.0 - 2.0)
    float sampleRate;       // Sample rate
    bool trueStereoIR;      // True if using separate L/R IRs
    
    // Filters
    daisysp::Svf lowCutFilterL;
    daisysp::Svf highCutFilterL;
    daisysp::Svf lowCutFilterR;
    daisysp::Svf highCutFilterR;
    
    // Helper function to update IR in frequency domain
    bool UpdateIRFrequencyDomain() {
        // Clear the frequency domain buffers
        memset(earlyIrFreqReal, 0, EARLY_FFT_SIZE * sizeof(float));
        memset(earlyIrFreqImag, 0, EARLY_FFT_SIZE * sizeof(float));
        memset(earlyIrFreqRealRight, 0, EARLY_FFT_SIZE * sizeof(float));
        memset(earlyIrFreqImagRight, 0, EARLY_FFT_SIZE * sizeof(float));
        
        memset(g_lateIrFreqReal, 0, LATE_FFT_SIZE * sizeof(float));
        memset(g_lateIrFreqImag, 0, LATE_FFT_SIZE * sizeof(float));
        memset(g_lateIrFreqRealRight, 0, LATE_FFT_SIZE * sizeof(float));
        memset(g_lateIrFreqImagRight, 0, LATE_FFT_SIZE * sizeof(float));
        
        // Apply IR length factor
        size_t effectiveIrLength = (size_t)(irLength * irLengthFactor);
        if (effectiveIrLength > irLength) effectiveIrLength = irLength;
        
        // Prepare early partition (first EARLY_FFT_SIZE samples of IR)
        float earlyIrPadded[EARLY_FFT_SIZE];
        float earlyIrPaddedRight[EARLY_FFT_SIZE];
        
        memset(earlyIrPadded, 0, EARLY_FFT_SIZE * sizeof(float));
        memset(earlyIrPaddedRight, 0, EARLY_FFT_SIZE * sizeof(float));
        
        // Copy early part of IR (up to EARLY_FFT_SIZE samples)
        size_t earlyLength = (effectiveIrLength < EARLY_FFT_SIZE) ? effectiveIrLength : EARLY_FFT_SIZE;
        memcpy(earlyIrPadded, irBuffer, earlyLength * sizeof(float));
        
        if (trueStereoIR) {
            memcpy(earlyIrPaddedRight, irBufferRight, earlyLength * sizeof(float));
        } else {
            memcpy(earlyIrPaddedRight, irBuffer, earlyLength * sizeof(float));
        }
        
        // Transform early IR to frequency domain
        earlyFft.Direct(earlyIrPadded, earlyTimeOut);
        for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
            earlyIrFreqReal[i] = earlyTimeOut[i];
            earlyIrFreqImag[i] = 0.0f;
        }
        
        earlyFft.Direct(earlyIrPaddedRight, earlyTimeOut);
        for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
            earlyIrFreqRealRight[i] = earlyTimeOut[i];
            earlyIrFreqImagRight[i] = 0.0f;
        }
        
        // Prepare late partition (remaining IR samples)
        if (effectiveIrLength > EARLY_FFT_SIZE) {
            float lateIrPadded[LATE_FFT_SIZE];
            float lateIrPaddedRight[LATE_FFT_SIZE];
            
            memset(lateIrPadded, 0, LATE_FFT_SIZE * sizeof(float));
            memset(lateIrPaddedRight, 0, LATE_FFT_SIZE * sizeof(float));
            
            // Copy late part of IR (from EARLY_FFT_SIZE to end)
            size_t lateLength = effectiveIrLength - EARLY_FFT_SIZE;
            if (lateLength > LATE_FFT_SIZE) lateLength = LATE_FFT_SIZE;
            
            memcpy(lateIrPadded, irBuffer + EARLY_FFT_SIZE, lateLength * sizeof(float));
            
            if (trueStereoIR) {
                memcpy(lateIrPaddedRight, irBufferRight + EARLY_FFT_SIZE, lateLength * sizeof(float));
            } else {
                memcpy(lateIrPaddedRight, irBuffer + EARLY_FFT_SIZE, lateLength * sizeof(float));
            }
            
            // Transform late IR to frequency domain
            lateFft.Direct(lateIrPadded, g_lateTimeOut);
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                g_lateIrFreqReal[i] = g_lateTimeOut[i];
                g_lateIrFreqImag[i] = 0.0f;
            }
            
            lateFft.Direct(lateIrPaddedRight, g_lateTimeOut);
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                g_lateIrFreqRealRight[i] = g_lateTimeOut[i];
                g_lateIrFreqImagRight[i] = 0.0f;
            }
        }
        
        return true;
    }
    
public:
    PartitionedConvolutionReverb() : 
        irBuffer(nullptr),
        irBufferRight(nullptr),
        irLength(0),
        earlyInputBufferPos(0),
        lateInputBufferPos(0),
        predelayBufferPos(0),
        predelayInSamples(0),
        dryWet(0.5f),
        predelayMs(0.0f),
        irLengthFactor(1.0f),
        lowCutFreq(100.0f),
        highCutFreq(10000.0f),
        stereoWidth(1.0f),
        sampleRate(48000.0f),
        trueStereoIR(false)
    {
        // Initialize FFTs
        earlyFft.Init();
        lateFft.Init();
    }
    
    ~PartitionedConvolutionReverb() {
        // Free IR buffers
        if (irBuffer) {
            delete[] irBuffer;
            irBuffer = nullptr;
        }
        
        if (irBufferRight) {
            delete[] irBufferRight;
            irBufferRight = nullptr;
        }
    }
    
    // Initialize with sample rate
    void Init(float sample_rate) {
        sampleRate = sample_rate;
        
        // Initialize FFTs
        earlyFft.Init();
        lateFft.Init();
        
        // Clear buffers - must be done after hardware initialization for SDRAM buffers
        memset(earlyInputBuffer, 0, sizeof(earlyInputBuffer));
        memset(earlyInputBufferRight, 0, sizeof(earlyInputBufferRight));
        memset(earlyOutputBuffer, 0, sizeof(earlyOutputBuffer));
        memset(earlyOutputBufferRight, 0, sizeof(earlyOutputBufferRight));
        
        memset(g_lateInputBuffer, 0, sizeof(g_lateInputBuffer));
        memset(g_lateInputBufferRight, 0, sizeof(g_lateInputBufferRight));
        memset(g_lateOutputBuffer, 0, sizeof(g_lateOutputBuffer));
        memset(g_lateOutputBufferRight, 0, sizeof(g_lateOutputBufferRight));
        
        memset(g_predelayBuffer, 0, sizeof(g_predelayBuffer));
        memset(g_predelayBufferRight, 0, sizeof(g_predelayBufferRight));
        
        // Initialize filters with sample rate
        lowCutFilterL.Init(sampleRate);
        highCutFilterL.Init(sampleRate);
        lowCutFilterR.Init(sampleRate);
        highCutFilterR.Init(sampleRate);
        
        // Update filter parameters
        UpdateFilters();
    }
    
    // Load IR from buffer - now using external SDRAM for IR storage
    bool LoadIR(float* buffer, size_t length) {
        // Check if length is valid
        if (length == 0 || length > MAX_IR_LENGTH) {
            return false;
        }
        
        // Free old buffer if it exists
        if (irBuffer) {
            delete[] irBuffer;
        }
        
        // Allocate new buffer in SDRAM
        irBuffer = new float[length];
        if (!irBuffer) {
            return false;
        }
        
        // Copy data
        memcpy(irBuffer, buffer, length * sizeof(float));
        irLength = length;
        
        // Not true stereo
        trueStereoIR = false;
        
        // Update frequency domain representation
        return UpdateIRFrequencyDomain();
    }
    
    // Load stereo IR from buffers - now using external SDRAM for IR storage
    bool LoadStereoIR(float* bufferL, float* bufferR, size_t length) {
        // Check if length is valid
        if (length == 0 || length > MAX_IR_LENGTH) {
            return false;
        }
        
        // Free old buffers if they exist
        if (irBuffer) {
            delete[] irBuffer;
        }
        
        if (irBufferRight) {
            delete[] irBufferRight;
        }
        
        // Allocate new buffers in SDRAM
        irBuffer = new float[length];
        irBufferRight = new float[length];
        
        if (!irBuffer || !irBufferRight) {
            if (irBuffer) {
                delete[] irBuffer;
                irBuffer = nullptr;
            }
            
            if (irBufferRight) {
                delete[] irBufferRight;
                irBufferRight = nullptr;
            }
            
            return false;
        }
        
        // Copy data
        memcpy(irBuffer, bufferL, length * sizeof(float));
        memcpy(irBufferRight, bufferR, length * sizeof(float));
        irLength = length;
        
        // True stereo
        trueStereoIR = true;
        
        // Update frequency domain representation
        return UpdateIRFrequencyDomain();
    }
    
    // Set dry/wet mix
    void SetDryWet(float value) {
        dryWet = value;
    }
    
    // Set predelay in milliseconds
    void SetPredelay(float ms) {
        predelayMs = ms;
        predelayInSamples = (size_t)(predelayMs * sampleRate / 1000.0f);
        if (predelayInSamples >= MAX_PREDELAY_SAMPLES) {
            predelayInSamples = MAX_PREDELAY_SAMPLES - 1;
        }
    }
    
    // Set IR length factor
    void SetIRLengthFactor(float factor) {
        if (factor != irLengthFactor) {
            irLengthFactor = factor;
            UpdateIRFrequencyDomain();
        }
    }
    
    // Set low cut frequency
    void SetLowCut(float freq) {
        lowCutFreq = freq;
        UpdateFilters();
    }
    
    // Set high cut frequency
    void SetHighCut(float freq) {
        highCutFreq = freq;
        UpdateFilters();
    }
    
    // Set stereo width
    void SetStereoWidth(float width) {
        stereoWidth = width;
    }
    
    // Update filter parameters
    void UpdateFilters() {
        // Low cut filter (high pass)
        lowCutFilterL.SetFreq(lowCutFreq);
        lowCutFilterL.SetRes(0.707f);
        lowCutFilterL.SetDrive(1.0f);
        
        highCutFilterL.SetFreq(highCutFreq);
        highCutFilterL.SetRes(0.707f);
        highCutFilterL.SetDrive(1.0f);
        
        lowCutFilterR.SetFreq(lowCutFreq);
        lowCutFilterR.SetRes(0.707f);
        lowCutFilterR.SetDrive(1.0f);
        
        highCutFilterR.SetFreq(highCutFreq);
        highCutFilterR.SetRes(0.707f);
        highCutFilterR.SetDrive(1.0f);
    }
    
    // Process a single sample
    void Process(float inL, float inR, float* outL, float* outR) {
        // If no IR is loaded, pass through
        if (!irBuffer) {
            *outL = inL;
            *outR = inR;
            return;
        }
        
        // Store input in predelay buffer
        g_predelayBuffer[predelayBufferPos] = inL;
        g_predelayBufferRight[predelayBufferPos] = inR;
        
        // Get delayed input
        size_t delayedPos = (predelayBufferPos + MAX_PREDELAY_SAMPLES - predelayInSamples) % MAX_PREDELAY_SAMPLES;
        float delayedL = g_predelayBuffer[delayedPos];
        float delayedR = g_predelayBufferRight[delayedPos];
        
        // Advance predelay buffer position
        predelayBufferPos = (predelayBufferPos + 1) % MAX_PREDELAY_SAMPLES;
        
        // Store input in early and late buffers
        earlyInputBuffer[earlyInputBufferPos] = delayedL;
        earlyInputBufferRight[earlyInputBufferPos] = delayedR;
        
        g_lateInputBuffer[lateInputBufferPos] = delayedL;
        g_lateInputBufferRight[lateInputBufferPos] = delayedR;
        
        // Process early partition (low latency)
        earlyInputBufferPos++;
        if (earlyInputBufferPos >= EARLY_FFT_SIZE) {
            // Reset input buffer position
            earlyInputBufferPos = 0;
            
            // Process left channel
            memcpy(earlyTimeIn, earlyInputBuffer, EARLY_FFT_SIZE * sizeof(float));
            earlyFft.Direct(earlyTimeIn, earlyTimeOut);
            
            for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
                earlyFreqReal[i] = earlyTimeOut[i];
                earlyFreqImag[i] = 0.0f;
            }
            
            // Complex multiplication with IR
            for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
                float a = earlyFreqReal[i];
                float b = earlyFreqImag[i];
                float c = earlyIrFreqReal[i];
                float d = earlyIrFreqImag[i];
                
                earlyFreqReal[i] = a*c - b*d;
                earlyFreqImag[i] = a*d + b*c;
            }
            
            // Transform back to time domain
            earlyFft.Inverse(earlyFreqReal, earlyFreqImag, EARLY_FFT_SIZE);
            
            // Process right channel
            memcpy(earlyTimeIn, earlyInputBufferRight, EARLY_FFT_SIZE * sizeof(float));
            earlyFft.Direct(earlyTimeIn, earlyTimeOut);
            
            for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
                earlyFreqReal[i] = earlyTimeOut[i];
                earlyFreqImag[i] = 0.0f;
            }
            
            // Complex multiplication with IR
            for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
                float a = earlyFreqReal[i];
                float b = earlyFreqImag[i];
                float c = earlyIrFreqRealRight[i];
                float d = earlyIrFreqImagRight[i];
                
                earlyFreqReal[i] = a*c - b*d;
                earlyFreqImag[i] = a*d + b*c;
            }
            
            // Transform back to time domain
            earlyFft.Inverse(earlyFreqReal, earlyFreqImag, EARLY_FFT_SIZE);
            
            // Overlap-add
            for (size_t i = 0; i < EARLY_FFT_SIZE; i++) {
                earlyOutputBuffer[i] = earlyOutputBuffer[i + EARLY_FFT_SIZE] + earlyFreqReal[i];
                earlyOutputBufferRight[i] = earlyOutputBufferRight[i + EARLY_FFT_SIZE] + earlyFreqReal[i];
            }
            
            // Clear the second half of the output buffer
            memset(earlyOutputBuffer + EARLY_FFT_SIZE, 0, EARLY_FFT_SIZE * sizeof(float));
            memset(earlyOutputBufferRight + EARLY_FFT_SIZE, 0, EARLY_FFT_SIZE * sizeof(float));
        }
        
        // Process late partition (efficiency)
        lateInputBufferPos++;
        if (lateInputBufferPos >= LATE_FFT_SIZE) {
            // Reset input buffer position
            lateInputBufferPos = 0;
            
            // Process left channel
            memcpy(g_lateTimeIn, g_lateInputBuffer, LATE_FFT_SIZE * sizeof(float));
            lateFft.Direct(g_lateTimeIn, g_lateTimeOut);
            
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                g_lateFreqReal[i] = g_lateTimeOut[i];
                g_lateFreqImag[i] = 0.0f;
            }
            
            // Complex multiplication with IR
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                float a = g_lateFreqReal[i];
                float b = g_lateFreqImag[i];
                float c = g_lateIrFreqReal[i];
                float d = g_lateIrFreqImag[i];
                
                g_lateFreqReal[i] = a*c - b*d;
                g_lateFreqImag[i] = a*d + b*c;
            }
            
            // Transform back to time domain
            lateFft.Inverse(g_lateFreqReal, g_lateFreqImag, LATE_FFT_SIZE);
            
            // Process right channel
            memcpy(g_lateTimeIn, g_lateInputBufferRight, LATE_FFT_SIZE * sizeof(float));
            lateFft.Direct(g_lateTimeIn, g_lateTimeOut);
            
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                g_lateFreqReal[i] = g_lateTimeOut[i];
                g_lateFreqImag[i] = 0.0f;
            }
            
            // Complex multiplication with IR
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                float a = g_lateFreqReal[i];
                float b = g_lateFreqImag[i];
                float c = g_lateIrFreqRealRight[i];
                float d = g_lateIrFreqImagRight[i];
                
                g_lateFreqReal[i] = a*c - b*d;
                g_lateFreqImag[i] = a*d + b*c;
            }
            
            // Transform back to time domain
            lateFft.Inverse(g_lateFreqReal, g_lateFreqImag, LATE_FFT_SIZE);
            
            // Overlap-add
            for (size_t i = 0; i < LATE_FFT_SIZE; i++) {
                g_lateOutputBuffer[i] = g_lateOutputBuffer[i + LATE_FFT_SIZE] + g_lateFreqReal[i];
                g_lateOutputBufferRight[i] = g_lateOutputBufferRight[i + LATE_FFT_SIZE] + g_lateFreqReal[i];
            }
            
            // Clear the second half of the output buffer
            memset(g_lateOutputBuffer + LATE_FFT_SIZE, 0, LATE_FFT_SIZE * sizeof(float));
            memset(g_lateOutputBufferRight + LATE_FFT_SIZE, 0, LATE_FFT_SIZE * sizeof(float));
        }
        
        // Get output from early and late partitions
        float wetL = earlyOutputBuffer[0] + g_lateOutputBuffer[0];
        float wetR = earlyOutputBufferRight[0] + g_lateOutputBufferRight[0];
        
        // Apply filters
        lowCutFilterL.Process(wetL);
        float filteredL = lowCutFilterL.High();
        highCutFilterL.Process(filteredL);
        filteredL = highCutFilterL.Low();
        
        lowCutFilterR.Process(wetR);
        float filteredR = lowCutFilterR.High();
        highCutFilterR.Process(filteredR);
        filteredR = highCutFilterR.Low();
        
        // Apply stereo width
        if (stereoWidth != 1.0f) {
            float mid = (filteredL + filteredR) * 0.5f;
            float side = (filteredL - filteredR) * 0.5f * stereoWidth;
            filteredL = mid + side;
            filteredR = mid - side;
        }
        
        // Mix dry and wet signals
        *outL = inL * (1.0f - dryWet) + filteredL * dryWet;
        *outR = inR * (1.0f - dryWet) + filteredR * dryWet;
        
        // Shift output buffers
        for (size_t i = 0; i < EARLY_FFT_SIZE - 1; i++) {
            earlyOutputBuffer[i] = earlyOutputBuffer[i + 1];
            earlyOutputBufferRight[i] = earlyOutputBufferRight[i + 1];
        }
        
        for (size_t i = 0; i < LATE_FFT_SIZE - 1; i++) {
            g_lateOutputBuffer[i] = g_lateOutputBuffer[i + 1];
            g_lateOutputBufferRight[i] = g_lateOutputBufferRight[i + 1];
        }
    }
};

// Create the reverb processor
PartitionedConvolutionReverb reverb;

// Audio callback
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Process each sample
    for (size_t i = 0; i < size; i++) {
        // Get input samples
        float inL = in[0][i];
        float inR = in[1][i];
        
        // Detect stereo input
        if (!isStereoInput && fabs(inL - inR) > 0.01f) {
            isStereoInput = true;
            // No LED indicator for stereo mode in Hothouse pedal (only 2 LEDs available)
        }
        
        // Process
        float outL, outR;
        
        if (bypass) {
            // Bypass mode
            outL = inL;
            outR = inR;
        } else if (freeze) {
            // Freeze mode - only output reverb tail
            reverb.Process(0.0f, 0.0f, &outL, &outR);
        } else {
            // Normal mode
            reverb.Process(inL, inR, &outL, &outR);
        }
        
        // Output
        out[0][i] = outL;
        out[1][i] = outR;
    }
}

// Handle footswitch 1 (freeze) - SWAPPED from original implementation
void HandleFootswitch1(bool pressed) {
    if (pressed) {
        freeze = !freeze;
        led1.Set(freeze ? 1.0f : 0.0f);
    }
}

// Handle footswitch 2 (bypass) - SWAPPED from original implementation
void HandleFootswitch2(bool pressed) {
    if (pressed) {
        bypass = !bypass;
        led2.Set(bypass ? 0.0f : 1.0f);
    }
}

// Handle footswitch 2 long press (reset parameters) - SWAPPED from original implementation
void HandleFootswitch2LongPress() {
    // Reset parameters
    reverb.SetDryWet(0.5f);
    reverb.SetPredelay(0.0f);
    reverb.SetIRLengthFactor(1.0f);
    reverb.SetLowCut(100.0f);
    reverb.SetHighCut(10000.0f);
    reverb.SetStereoWidth(1.0f);
}

// Handle footswitch 1 long press (load IR from USB) - SWAPPED from original implementation
void HandleFootswitch1LongPress() {
    // Set LED1 to blink during IR loading (temporary indicator)
    led1.Set(1.0f);
    
    // Try to load IR from USB
    if (irLoader.LoadIR()) {
        irLoaded = true;
        
        // Blink LED1 rapidly a few times to indicate success
        for (int i = 0; i < 5; i++) {
            led1.Set(0.0f);
            System::Delay(50);
            led1.Set(1.0f);
            System::Delay(50);
        }
    } else {
        irLoaded = false;
        
        // Blink LED1 slowly a few times to indicate failure
        for (int i = 0; i < 3; i++) {
            led1.Set(0.0f);
            System::Delay(200);
            led1.Set(1.0f);
            System::Delay(200);
        }
    }
    
    // Return LED1 to showing freeze status
    led1.Set(freeze ? 1.0f : 0.0f);
}

// Handle knob 1 (dry/wet)
void HandleKnob1(float value) {
    reverb.SetDryWet(value);
}

// Handle knob 2 (predelay)
void HandleKnob2(float value) {
    // Map 0-1 to 0-500ms
    float predelayMs = value * 500.0f;
    reverb.SetPredelay(predelayMs);
}

// Handle knob 3 (IR length)
void HandleKnob3(float value) {
    reverb.SetIRLengthFactor(value);
}

// Handle knob 4 (filter)
void HandleKnob4(float value) {
    if (value < 0.5f) {
        // First half: low cut (20Hz - 1000Hz)
        float lowCutFreq = 20.0f + value * 2.0f * 980.0f;
        reverb.SetLowCut(lowCutFreq);
        reverb.SetHighCut(20000.0f);
    } else {
        // Second half: high cut (20000Hz - 1000Hz)
        float highCutFreq = 20000.0f - (value - 0.5f) * 2.0f * 19000.0f;
        reverb.SetLowCut(20.0f);
        reverb.SetHighCut(highCutFreq);
    }
}

// Handle knob 5 (stereo width)
void HandleKnob5(float value) {
    // Map 0-1 to 0-2
    float stereoWidth = value * 2.0f;
    reverb.SetStereoWidth(stereoWidth);
}

// Handle knob 6 (custom parameter)
void HandleKnob6(float value) {
    // Custom parameter - not used yet
}

// Set up IR loader callback
bool LoadIRCallback(float* bufferL, float* bufferR, size_t length) {
    if (bufferR) {
        return reverb.LoadStereoIR(bufferL, bufferR, length);
    } else {
        return reverb.LoadIR(bufferL, length);
    }
}

// Main function
int main(void) {
    // Initialize hardware
    hw.Init();
    
    // Initialize LEDs - Hothouse pedal only has two LEDs
    led1.Init(hw.seed.GetPin(hw.LED_1), false);  // LED 1 - Used for freeze status and temporarily for IR loading
    led2.Init(hw.seed.GetPin(hw.LED_2), false);  // LED 2 - Used for bypass status
    
    // Set initial LED states
    led2.Set(bypass ? 0.0f : 1.0f);  // LED 2 for bypass status
    led1.Set(freeze ? 1.0f : 0.0f);  // LED 1 for freeze status
    
    // Initialize USB host for IR loading
    irLoader.Init();
    IRLoader::LoadIRCallback = LoadIRCallback;
    
    // Initialize reverb
    reverb.Init(hw.AudioSampleRate());
    
    // Set up audio callback
    hw.StartAudio(AudioCallback);
    
    // Set up footswitch callbacks
    hw.SetFootswitchCallback(0, HandleFootswitch1);
    hw.SetFootswitchCallback(1, HandleFootswitch2);
    hw.SetFootswitchLongPressCallback(0, HandleFootswitch1LongPress);
    hw.SetFootswitchLongPressCallback(1, HandleFootswitch2LongPress);
    
    // Set up knob callbacks
    hw.SetKnobCallback(0, HandleKnob1);
    hw.SetKnobCallback(1, HandleKnob2);
    hw.SetKnobCallback(2, HandleKnob3);
    hw.SetKnobCallback(3, HandleKnob4);
    hw.SetKnobCallback(4, HandleKnob5);
    hw.SetKnobCallback(5, HandleKnob6);
    
    // Main loop
    while (true) {
        // Process hardware events
        hw.ProcessAllControls();
        
        // Process USB events
        irLoader.Process();
        
        // Update LEDs - Hothouse pedal only has two LEDs
        led1.Update();  // LED 1 for freeze status
        led2.Update();  // LED 2 for bypass status
    }
}
