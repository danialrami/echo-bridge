#include "daisysp.h"
#include "hothouse.h"
#include "IRLoader.h"
#include "DisplayManager.h"
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

// Display manager
DisplayManager displayMgr(hw.display);

// LEDs
Led led_bypass;
Led led_freeze;
Led led_usb;     // LED to indicate USB status
Led led_stereo;  // LED to indicate stereo mode

// State variables
bool bypass = true;
bool freeze = false;
bool usbMounted = false;
bool irLoaded = false;
bool isStereoInput = false; // Flag for stereo detection

// Convolution implementation (now with stereo support and using ShyFFT)
class ConvolutionReverb {
private:
    // FFT size - adjust based on available memory and desired quality
    static const size_t FFT_SIZE = 1024;
    static const size_t FFT_SIZE_X2 = FFT_SIZE * 2;
    
    // IR parameters
    float* irBuffer;        // Left/mono IR
    float* irBufferRight;   // Right IR for true stereo
    size_t irLength;
    float* irFreqReal;
    float* irFreqImag;
    float* irFreqRealRight;
    float* irFreqImagRight;
    
    // Input buffers
    float* inputBufferL;
    float* inputBufferR;
    size_t inputBufferPos;
    
    // Output buffers
    float* outputBufferL;
    float* outputBufferR;
    size_t outputBufferPos;
    
    // Processing buffers
    float* fftBuffer;
    float* fftBufferR;
    
    // ShyFFT instance
    ShyFFT<float, FFT_SIZE> fft;
    
    // Parameters
    float irLengthMultiplier;  // 0.0-1.0
    float predelayMs;
    size_t predelaySamples;
    float sampleRate;
    float lowCutFreq;
    float highCutFreq;
    bool freezeActive;
    float stereoWidth;      // Stereo width control (0.0-2.0)
    bool trueStereoIR;      // Whether we have a true stereo IR pair
    
    // Predelay buffers
    float* predelayBufferL;
    float* predelayBufferR;
    size_t predelayBufferSize;
    size_t predelayBufferPos;
    
    // Filters (separate for L and R)
    daisysp::Svf lowCutFilterL;
    daisysp::Svf highCutFilterL;
    daisysp::Svf lowCutFilterR;
    daisysp::Svf highCutFilterR;
    
public:
    ConvolutionReverb() {
        // Initialize to nullptr
        irBuffer = nullptr;
        irBufferRight = nullptr;
        irFreqReal = nullptr;
        irFreqImag = nullptr;
        irFreqRealRight = nullptr;
        irFreqImagRight = nullptr;
        inputBufferL = nullptr;
        inputBufferR = nullptr;
        outputBufferL = nullptr;
        outputBufferR = nullptr;
        fftBuffer = nullptr;
        fftBufferR = nullptr;
        predelayBufferL = nullptr;
        predelayBufferR = nullptr;
        
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
        stereoWidth = 1.0f;
        trueStereoIR = false;
    }
    
    ~ConvolutionReverb() {
        // Free allocated memory
        if (irBuffer != nullptr) delete[] irBuffer;
        if (irBufferRight != nullptr) delete[] irBufferRight;
        if (irFreqReal != nullptr) delete[] irFreqReal;
        if (irFreqImag != nullptr) delete[] irFreqImag;
        if (irFreqRealRight != nullptr) delete[] irFreqRealRight;
        if (irFreqImagRight != nullptr) delete[] irFreqImagRight;
        if (inputBufferL != nullptr) delete[] inputBufferL;
        if (inputBufferR != nullptr) delete[] inputBufferR;
        if (outputBufferL != nullptr) delete[] outputBufferL;
        if (outputBufferR != nullptr) delete[] outputBufferR;
        if (fftBuffer != nullptr) delete[] fftBuffer;
        if (fftBufferR != nullptr) delete[] fftBufferR;
        if (predelayBufferL != nullptr) delete[] predelayBufferL;
        if (predelayBufferR != nullptr) delete[] predelayBufferR;
    }
    
    void Init(float sampleRate, size_t maxIrLength) {
        this->sampleRate = sampleRate;
        
        // Allocate memory for buffers
        irBuffer = new float[maxIrLength]();
        irBufferRight = new float[maxIrLength]();
        irFreqReal = new float[FFT_SIZE]();
        irFreqImag = new float[FFT_SIZE]();
        irFreqRealRight = new float[FFT_SIZE]();
        irFreqImagRight = new float[FFT_SIZE]();
        inputBufferL = new float[FFT_SIZE]();
        inputBufferR = new float[FFT_SIZE]();
        outputBufferL = new float[FFT_SIZE * 2](); // Double size for overlap-add
        outputBufferR = new float[FFT_SIZE * 2]();
        fftBuffer = new float[FFT_SIZE * 2]();    // Complex buffer (real, imag pairs)
        fftBufferR = new float[FFT_SIZE * 2]();   // Complex buffer for right channel
        
        // Maximum predelay of 500ms
        predelayBufferSize = static_cast<size_t>(sampleRate * 0.5f);
        predelayBufferL = new float[predelayBufferSize]();
        predelayBufferR = new float[predelayBufferSize]();
        
        // Initialize positions
        inputBufferPos = 0;
        outputBufferPos = 0;
        predelayBufferPos = 0;
        
        // Initialize FFT
        fft.Init();
        
        // Initialize filters
        lowCutFilterL.Init(sampleRate);
        highCutFilterL.Init(sampleRate);
        lowCutFilterR.Init(sampleRate);
        highCutFilterR.Init(sampleRate);
        
        // Set up filter parameters
        lowCutFilterL.SetFreq(lowCutFreq);
        highCutFilterL.SetFreq(highCutFreq);
        lowCutFilterL.SetRes(0.7f);
        highCutFilterL.SetRes(0.7f);
        lowCutFilterL.SetDrive(1.0f);
        highCutFilterL.SetDrive(1.0f);
        lowCutFilterL.SetMode(daisysp::Svf::Mode::HPF);
        highCutFilterL.SetMode(daisysp::Svf::Mode::LPF);
        
        // Same settings for right channel
        lowCutFilterR.SetFreq(lowCutFreq);
        highCutFilterR.SetFreq(highCutFreq);
        lowCutFilterR.SetRes(0.7f);
        highCutFilterR.SetRes(0.7f);
        lowCutFilterR.SetDrive(1.0f);
        highCutFilterR.SetDrive(1.0f);
        lowCutFilterR.SetMode(daisysp::Svf::Mode::HPF);
        highCutFilterR.SetMode(daisysp::Svf::Mode::LPF);
    }
    
    // Load a mono IR (will be used for both channels, or with stereo widening)
    bool LoadIR(const float* newIR, size_t newIrLength) {
        // Store the original IR
        irLength = newIrLength;
        memcpy(irBuffer, newIR, newIrLength * sizeof(float));
        
        // Copy the same IR to right channel (will be used for mono->stereo expansion)
        memcpy(irBufferRight, newIR, newIrLength * sizeof(float));
        
        trueStereoIR = false;
        
        // Calculate IR in frequency domain
        return UpdateIRFrequencyDomain();
    }
    
    // Load a stereo IR pair (true stereo processing)
    bool LoadStereoIR(const float* newIrLeft, const float* newIrRight, size_t newIrLength) {
        // Store the original IRs
        irLength = newIrLength;
        memcpy(irBuffer, newIrLeft, newIrLength * sizeof(float));
        memcpy(irBufferRight, newIrRight, newIrLength * sizeof(float));
        
        trueStereoIR = true;
        
        // Calculate IR in frequency domain
        return UpdateIRFrequencyDomain();
    }
    
    // Update IR frequency domain representation based on current settings
    bool UpdateIRFrequencyDomain() {
        // Clear frequency domain buffers
        memset(irFreqReal, 0, FFT_SIZE * sizeof(float));
        memset(irFreqImag, 0, FFT_SIZE * sizeof(float));
        memset(irFreqRealRight, 0, FFT_SIZE * sizeof(float));
        memset(irFreqImagRight, 0, FFT_SIZE * sizeof(float));
        
        // Create temporary buffers for FFT processing
        float tempL[FFT_SIZE] = {0};
        float tempR[FFT_SIZE] = {0};
        float resultL[FFT_SIZE] = {0};
        float resultR[FFT_SIZE] = {0};
        
        // Calculate effective IR length based on multiplier
        size_t effectiveIrLength = static_cast<size_t>(irLength * irLengthMultiplier);
        if (effectiveIrLength > FFT_SIZE) effectiveIrLength = FFT_SIZE;
        
        // Copy truncated IRs to temp buffers
        memcpy(tempL, irBuffer, effectiveIrLength * sizeof(float));
        memcpy(tempR, irBufferRight, effectiveIrLength * sizeof(float));
        
        // Perform FFT on left IR
        fft.Direct(tempL, resultL);
        
        // Convert to real/imag format for later convolution
        for (size_t i = 0; i < FFT_SIZE/2; ++i) {
            irFreqReal[i] = resultL[i * 2];
            irFreqImag[i] = resultL[i * 2 + 1];
        }
        
        // Perform FFT on right IR
        fft.Direct(tempR, resultR);
        
        // Convert to real/imag format for later convolution
        for (size_t i = 0; i < FFT_SIZE/2; ++i) {
            irFreqRealRight[i] = resultR[i * 2];
            irFreqImagRight[i] = resultR[i * 2 + 1];
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
        lowCutFilterL.SetFreq(lowCutFreq);
        lowCutFilterR.SetFreq(lowCutFreq);
    }
    
    // Set high cut frequency
    void SetHighCut(float freq) {
        if (freq < 1000.0f) freq = 1000.0f;
        if (freq > 20000.0f) freq = 20000.0f;
        highCutFreq = freq;
        highCutFilterL.SetFreq(highCutFreq);
        highCutFilterR.SetFreq(highCutFreq);
    }
    
    // Set freeze state
    void SetFreeze(bool state) {
        freezeActive = state;
    }
    
    // Set stereo width (0.0 = mono, 1.0 = normal stereo, 2.0 = exaggerated stereo)
    void SetStereoWidth(float width) {
        if (width < 0.0f) width = 0.0f;
        if (width > 2.0f) width = 2.0f;
        stereoWidth = width;
        
        // If we don't have a true stereo IR, we can adjust the right channel IR
        // to create a pseudo-stereo effect based on the width parameter
        if (!trueStereoIR) {
            // Only update if we have IRs loaded
            if (irLength > 0) {
                if (stereoWidth != 1.0f) {
                    // Create a modified right channel IR with slight time/phase differences
                    for (size_t i = 0; i < irLength; i++) {
                        // Add slight variations based on stereo width
                        // If width < 1.0, make channels more similar
                        // If width > 1.0, make channels more different
                        size_t offset = size_t(i * 0.01f * (stereoWidth - 1.0f));
                        float factor = 1.0f + 0.2f * (stereoWidth - 1.0f) * (static_cast<float>(rand()) / RAND_MAX - 0.5f);
                        
                        // Ensure offset is within range
                        size_t idx = (i + offset) % irLength;
                        
                        // Create a modified right channel IR
                        irBufferRight[i] = irBuffer[idx] * factor;
                    }
                } else {
                    // If width is 1.0, just copy the left channel
                    memcpy(irBufferRight, irBuffer, irLength * sizeof(float));
                }
                
                // Update IR frequency domain representation
                UpdateIRFrequencyDomain();
            }
        }
    }
    
    // Process a stereo pair of audio samples
    void Process(float inL, float inR, float* outL, float* outR) {
        // Store in predelay buffer
        predelayBufferL[predelayBufferPos] = inL;
        predelayBufferR[predelayBufferPos] = inR;
        predelayBufferPos = (predelayBufferPos + 1) % predelayBufferSize;
        
        // Get samples from predelay buffer
        size_t readPos = (predelayBufferPos + predelayBufferSize - predelaySamples) % predelayBufferSize;
        float predelayedSampleL = predelayBufferL[readPos];
        float predelayedSampleR = predelayBufferR[readPos];
        
        // Apply stereo width to the input (for mono->stereo expansion if needed)
        if (!trueStereoIR && stereoWidth != 1.0f) {
            // Calculate mid and side
            float mid = (predelayedSampleL + predelayedSampleR) * 0.5f;
            float side = (predelayedSampleL - predelayedSampleR) * 0.5f;
            
            // Apply width factor to side component
            side *= stereoWidth;
            
            // Recombine to L/R
            predelayedSampleL = mid + side;
            predelayedSampleR = mid - side;
        }
        
        // If freeze is active, don't add new input
        if (!freezeActive) {
            // Add to input buffer
            inputBufferL[inputBufferPos] = predelayedSampleL;
            inputBufferR[inputBufferPos] = predelayedSampleR;
        } else {
            // In freeze mode, don't add new input
            inputBufferL[inputBufferPos] = 0.0f;
            inputBufferR[inputBufferPos] = 0.0f;
        }
        
        // Get output samples
        float outSampleL = outputBufferL[outputBufferPos];
        float outSampleR = outputBufferR[outputBufferPos];
        
        // Reset output buffer position for next time
        outputBufferL[outputBufferPos] = 0.0f;
        outputBufferR[outputBufferPos] = 0.0f;
        
        // Update buffer positions
        inputBufferPos = (inputBufferPos + 1) % FFT_SIZE;
        outputBufferPos = (outputBufferPos + 1) % (FFT_SIZE * 2);
        
        // If we've filled the input buffer, process an FFT block
        if (inputBufferPos == 0) {
            ProcessFFTBlock();
        }
        
        // Apply filters
        outSampleL = lowCutFilterL.Process(outSampleL);
        outSampleL = highCutFilterL.Process(outSampleL);
        
        outSampleR = lowCutFilterR.Process(outSampleR);
        outSampleR = highCutFilterR.Process(outSampleR);
        
        // Apply stereo width to the output
        if (stereoWidth != 1.0f) {
            // Calculate mid and side
            float mid = (outSampleL + outSampleR) * 0.5f;
            float side = (outSampleL - outSampleR) * 0.5f;
            
            // Apply width factor to side component
            side *= stereoWidth;
            
            // Recombine to L/R
            outSampleL = mid + side;
            outSampleR = mid - side;
        }
        
        // Return the processed samples
        *outL = outSampleL;
        *outR = outSampleR;
    }
    
private:
    // Process a block using FFT convolution with ShyFFT
    void ProcessFFTBlock() {
        // Temporary buffers for FFT processing
        float tempInputL[FFT_SIZE] = {0};
        float tempInputR[FFT_SIZE] = {0};
        float tempOutputL[FFT_SIZE] = {0};
        float tempOutputR[FFT_SIZE] = {0};
        float fftResultL[FFT_SIZE] = {0}; 
        float fftResultR[FFT_SIZE] = {0};
        
        // Copy input buffers to temp buffers
        memcpy(tempInputL, inputBufferL, FFT_SIZE * sizeof(float));
        memcpy(tempInputR, inputBufferR, FFT_SIZE * sizeof(float));
        
        // Forward FFT - ShyFFT manages the real/complex conversion internally
        fft.Direct(tempInputL, fftResultL);
        fft.Direct(tempInputR, fftResultR);
        
        // ShyFFT returns results in a special format where:
        // - Real and imaginary parts are interleaved
        // - fftResult[0] = DC component (real)
        // - fftResult[1] = Nyquist component (real)
        // - fftResult[2*i] = real part of bin i
        // - fftResult[2*i+1] = imaginary part of bin i
        
        // Perform complex multiplication (convolution in frequency domain)
        // We'll use fftBuffer as temporary storage
        
        // DC and Nyquist components (real only)
        fftBuffer[0] = fftResultL[0] * irFreqReal[0]; // DC is real * real
        fftBuffer[1] = fftResultL[1] * irFreqReal[1]; // Nyquist is real * real
        
        fftBufferR[0] = fftResultR[0] * irFreqRealRight[0];
        fftBufferR[1] = fftResultR[1] * irFreqRealRight[1];
        
        // Remaining frequency bins - complex multiplication
        for (size_t i = 1; i < FFT_SIZE/2; ++i) {
            // Get input components
            float inRealL = fftResultL[i*2];
            float inImagL = fftResultL[i*2+1];
            float inRealR = fftResultR[i*2];
            float inImagR = fftResultR[i*2+1];
            
            // Get IR components
            float irRealL = irFreqReal[i];
            float irImagL = irFreqImag[i];
            float irRealR = irFreqRealRight[i];
            float irImagR = irFreqImagRight[i];
            
            // Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i
            fftBuffer[i*2] = inRealL * irRealL - inImagL * irImagL;     // Real part
            fftBuffer[i*2+1] = inRealL * irImagL + inImagL * irRealL;   // Imaginary part
            
            fftBufferR[i*2] = inRealR * irRealR - inImagR * irImagR;    // Real part
            fftBufferR[i*2+1] = inRealR * irImagR + inImagR * irRealR;  // Imaginary part
        }
        
        // Inverse FFT
        fft.Inverse(fftBuffer, tempOutputL);
        fft.Inverse(fftBufferR, tempOutputR);
        
        // Overlap-add to output buffer
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            // Add to output buffer with overlap
            // Note: Scaling is handled by ShyFFT internally, but we'll apply a small scale factor for safety
            outputBufferL[(outputBufferPos + i) % (FFT_SIZE * 2)] += tempOutputL[i] * 0.5f;
            outputBufferR[(outputBufferPos + i) % (FFT_SIZE * 2)] += tempOutputR[i] * 0.5f;
        }
    }
};

// Create one instance of the convolution reverb
ConvolutionReverb convReverb;

// Define Echo Bridge IR array sizes based on buffer requirements
// Maximum IR length (3 seconds at 48kHz)
const size_t MAX_IR_LENGTH = 144000;

// IR mode selection
enum IRMode {
    IR_FULL_BRIDGE = 0,
    IR_SHORT_ECHO = 1,
    IR_LONG_DECAY = 2
};

// Different IRs for different modes
float echoBridgeIR_full[MAX_IR_LENGTH];
float echoBridgeIR_short[MAX_IR_LENGTH];
float echoBridgeIR_long[MAX_IR_LENGTH];

// For stereo IRs (if available)
float echoBridgeIR_full_R[MAX_IR_LENGTH];
float echoBridgeIR_short_R[MAX_IR_LENGTH];
float echoBridgeIR_long_R[MAX_IR_LENGTH];

// IR lengths for each mode
size_t irLength_full = 0;
size_t irLength_short = 0;
size_t irLength_long = 0;

// Current IR mode
int irMode = IR_FULL_BRIDGE;
bool isStereoIR[3] = {false, false, false}; // Track if each IR mode has stereo IRs

// Control variables
float dryWetMix = 0.5f;  // 0.0 = dry, 1.0 = wet
float predelayMs = 0.0f; // 0-500ms
float irLength = 1.0f;   // 0.1-1.0
float lowCutFreq = 20.0f; // 20-2000Hz
float highCutFreq = 20000.0f; // 1000-20000Hz
float outputLevel = 1.0f; // 0.0-1.0
float stereoWidth = 1.0f; // 0.5-1.5 (set by switch 3)

// Generate placeholder test IRs for demo/startup
void GenerateTestIRs() {
    // Generate basic impulse responses for testing
    // Full bridge IR (longer decay)
    for (size_t i = 0; i < MAX_IR_LENGTH; i++) {
        float decay = exp(-3.0f * i / MAX_IR_LENGTH); // Exponential decay
        
        // Add some "echo" character with spaced reflections
        if (i % 4800 < 100) { // Every 100ms
            decay *= 1.5f;
        }
        
        echoBridgeIR_full[i] = decay * ((i == 0) ? 1.0f : 0.7f);
        // Create slightly different right channel for stereo effect
        echoBridgeIR_full_R[i] = decay * ((i == 0) ? 1.0f : 0.7f) * (1.0f + 0.1f * sin(i * 0.01f));
    }
    irLength_full = 48000; // 1 second at 48kHz
    
    // Short echo IR (quick initial reflections)
    for (size_t i = 0; i < MAX_IR_LENGTH; i++) {
        float decay = exp(-6.0f * i / MAX_IR_LENGTH); // Faster decay
        
        // Add more defined early reflections
        if (i % 2400 < 200 && i < 24000) { // More defined early reflections
            decay *= 2.0f;
        }
        
        echoBridgeIR_short[i] = decay * ((i == 0) ? 1.0f : 0.8f);
        // Create slightly different right channel
        echoBridgeIR_short_R[i] = decay * ((i == 0) ? 1.0f : 0.8f) * (1.0f + 0.1f * sin(i * 0.015f));
    }
    irLength_short = 24000; // 0.5 seconds at 48kHz
    
    // Long decay IR (more diffuse, longer tail)
    for (size_t i = 0; i < MAX_IR_LENGTH; i++) {
        float decay = exp(-2.0f * i / MAX_IR_LENGTH); // Slower decay
        
        // Add more diffuse late reflections
        float diffusion = 0.3f * sin(i * 0.003f) * sin(i * 0.005f);
        
        echoBridgeIR_long[i] = (decay + diffusion * decay) * ((i == 0) ? 0.9f : 0.6f);
        // Create slightly different right channel
        echoBridgeIR_long_R[i] = (decay + diffusion * decay) * ((i == 0) ? 0.9f : 0.6f) * (1.0f + 0.12f * sin(i * 0.008f));
    }
    irLength_long = 72000; // 1.5 seconds at 48kHz
    
    // Mark all generated IRs as stereo for demo purposes
    isStereoIR[0] = true;
    isStereoIR[1] = true;
    isStereoIR[2] = true;
}

// Detect if input is stereo by analyzing L/R differences
bool DetectStereoInput(float* bufferL, float* bufferR, size_t size) {
    float sumDiff = 0.0f;
    float sumMag = 0.0f;
    
    for (size_t i = 0; i < size; i++) {
        float diff = fabsf(bufferL[i] - bufferR[i]);
        float mag = (fabsf(bufferL[i]) + fabsf(bufferR[i])) * 0.5f;
        
        sumDiff += diff;
        sumMag += mag;
    }
    
    // If the average difference is above a threshold, consider it stereo
    // This might need tuning for your specific use case
    if (sumMag > 0.001f) {
        float diffRatio = sumDiff / sumMag;
        return diffRatio > 0.1f; // Arbitrary threshold (10% difference)
    }
    
    return false;
}

// Load IR from USB
bool LoadIRFromUSB(int irMode) {
    char filename[64];
    bool success = false;
    
    // Try to load stereo IRs first
    switch (irMode) {
        case IR_FULL_BRIDGE:
            {
                // Try to load stereo pair first
                sprintf(filename, "ECHOBR_FULL_L.WAV");
                float* tempIR_L = echoBridgeIR_full;
                size_t irSize = MAX_IR_LENGTH;
                
                if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                    // Left channel loaded, try right
                    sprintf(filename, "ECHOBR_FULL_R.WAV");
                    float* tempIR_R = echoBridgeIR_full_R;
                    size_t irSizeR = MAX_IR_LENGTH;
                    
                    if (irLoader.LoadWavToIR(filename, tempIR_R, &irSizeR, MAX_IR_LENGTH)) {
                        // Both channels loaded - we have a stereo IR
                        irLength_full = irSize;
                        isStereoIR[IR_FULL_BRIDGE] = true;
                        if (irMode == IR_FULL_BRIDGE) {
                            convReverb.LoadStereoIR(tempIR_L, tempIR_R, irSize);
                        }
                        success = true;
                    } else {
                        // Failed to load right channel, revert to mono
                        sprintf(filename, "ECHOBR_FULL.WAV");
                        if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                            irLength_full = irSize;
                            isStereoIR[IR_FULL_BRIDGE] = false;
                            if (irMode == IR_FULL_BRIDGE) {
                                convReverb.LoadIR(tempIR_L, irSize);
                            }
                            success = true;
                        }
                    }
                } else {
                    // No stereo L channel found, try mono
                    sprintf(filename, "ECHOBR_FULL.WAV");
                    if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                        irLength_full = irSize;
                        isStereoIR[IR_FULL_BRIDGE] = false;
                        if (irMode == IR_FULL_BRIDGE) {
                            convReverb.LoadIR(tempIR_L, irSize);
                        }
                        success = true;
                    }
                }
            }
            break;
            
        case IR_SHORT_ECHO:
            {
                // Try to load stereo pair first
                sprintf(filename, "ECHOBR_SHORT_L.WAV");
                float* tempIR_L = echoBridgeIR_short;
                size_t irSize = MAX_IR_LENGTH;
                
                if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                    // Left channel loaded, try right
                    sprintf(filename, "ECHOBR_SHORT_R.WAV");
                    float* tempIR_R = echoBridgeIR_short_R;
                    size_t irSizeR = MAX_IR_LENGTH;
                    
                    if (irLoader.LoadWavToIR(filename, tempIR_R, &irSizeR, MAX_IR_LENGTH)) {
                        // Both channels loaded - we have a stereo IR
                        irLength_short = irSize;
                        isStereoIR[IR_SHORT_ECHO] = true;
                        if (irMode == IR_SHORT_ECHO) {
                            convReverb.LoadStereoIR(tempIR_L, tempIR_R, irSize);
                        }
                        success = true;
                    } else {
                        // Failed to load right channel, revert to mono
                        sprintf(filename, "ECHOBR_SHORT.WAV");
                        if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                            irLength_short = irSize;
                            isStereoIR[IR_SHORT_ECHO] = false;
                            if (irMode == IR_SHORT_ECHO) {
                                convReverb.LoadIR(tempIR_L, irSize);
                            }
                            success = true;
                        }
                    }
                } else {
                    // No stereo L channel found, try mono
                    sprintf(filename, "ECHOBR_SHORT.WAV");
                    if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                        irLength_short = irSize;
                        isStereoIR[IR_SHORT_ECHO] = false;
                        if (irMode == IR_SHORT_ECHO) {
                            convReverb.LoadIR(tempIR_L, irSize);
                        }
                        success = true;
                    }
                }
            }
            break;
            
        case IR_LONG_DECAY:
            {
                // Try to load stereo pair first
                sprintf(filename, "ECHOBR_LONG_L.WAV");
                float* tempIR_L = echoBridgeIR_long;
                size_t irSize = MAX_IR_LENGTH;
                
                if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                    // Left channel loaded, try right
                    sprintf(filename, "ECHOBR_LONG_R.WAV");
                    float* tempIR_R = echoBridgeIR_long_R;
                    size_t irSizeR = MAX_IR_LENGTH;
                    
                    if (irLoader.LoadWavToIR(filename, tempIR_R, &irSizeR, MAX_IR_LENGTH)) {
                        // Both channels loaded - we have a stereo IR
                        irLength_long = irSize;
                        isStereoIR[IR_LONG_DECAY] = true;
                        if (irMode == IR_LONG_DECAY) {
                            convReverb.LoadStereoIR(tempIR_L, tempIR_R, irSize);
                        }
                        success = true;
                    } else {
                        // Failed to load right channel, revert to mono
                        sprintf(filename, "ECHOBR_LONG.WAV");
                        if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                            irLength_long = irSize;
                            isStereoIR[IR_LONG_DECAY] = false;
                            if (irMode == IR_LONG_DECAY) {
                                convReverb.LoadIR(tempIR_L, irSize);
                            }
                            success = true;
                        }
                    }
                } else {
                    // No stereo L channel found, try mono
                    sprintf(filename, "ECHOBR_LONG.WAV");
                    if (irLoader.LoadWavToIR(filename, tempIR_L, &irSize, MAX_IR_LENGTH)) {
                        irLength_long = irSize;
                        isStereoIR[IR_LONG_DECAY] = false;
                        if (irMode == IR_LONG_DECAY) {
                            convReverb.LoadIR(tempIR_L, irSize);
                        }
                        success = true;
                    }
                }
            }
            break;
    }
    
    return success;
}

// Footswitch callbacks
struct FsCallbacks : public Hothouse::FootswitchCallbacks {
    static void HandleNormalPress(Hothouse::Switches footswitch) {
        if (footswitch == Hothouse::FOOTSWITCH_2) {
            // Toggle bypass
            bypass = !bypass;
        } else if (footswitch == Hothouse::FOOTSWITCH_1) {
            // Normal press of FOOTSWITCH_1 triggers momentary freeze
            // Don't do anything here - handled in main loop by debouncing
        }
    }
    
    static void HandleDoublePress(Hothouse::Switches footswitch) {
        if (footswitch == Hothouse::FOOTSWITCH_1) {
            // Double press of FOOTSWITCH_1 cycles through IR modes
            irMode = (irMode + 1) % 3;
            
            // Load the appropriate IR
            if (irMode == IR_FULL_BRIDGE) {
                if (isStereoIR[IR_FULL_BRIDGE]) {
                    convReverb.LoadStereoIR(echoBridgeIR_full, echoBridgeIR_full_R, irLength_full);
                } else {
                    convReverb.LoadIR(echoBridgeIR_full, irLength_full);
                }
            } else if (irMode == IR_SHORT_ECHO) {
                if (isStereoIR[IR_SHORT_ECHO]) {
                    convReverb.LoadStereoIR(echoBridgeIR_short, echoBridgeIR_short_R, irLength_short);
                } else {
                    convReverb.LoadIR(echoBridgeIR_short, irLength_short);
                }
            } else if (irMode == IR_LONG_DECAY) {
                if (isStereoIR[IR_LONG_DECAY]) {
                    convReverb.LoadStereoIR(echoBridgeIR_long, echoBridgeIR_long_R, irLength_long);
                } else {
                    convReverb.LoadIR(echoBridgeIR_long, irLength_long);
                }
            }
            
            // Update display
            displayMgr.ShowIRLoaded(isStereoIR[irMode], irMode);
        }
    }
    
    static void HandleLongPress(Hothouse::Switches footswitch) {
        if (footswitch == Hothouse::FOOTSWITCH_1) {
            // Long press of FOOTSWITCH_1 triggers bootloader mode (handled in hw.CheckResetToBootloader())
        }
    }
};

// Audio callback function
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Process hardware controls
    hw.ProcessAllControls();
    
    // Process USB events
    irLoader.Process();
    
    // Check if USB status changed
    bool isUSBMounted = irLoader.IsUSBMounted();
    if (isUSBMounted != usbMounted) {
        usbMounted = isUSBMounted;
        displayMgr.ShowUSBStatus(usbMounted);
        
        // Try to load IRs from USB if newly mounted
        if (usbMounted) {
            LoadIRFromUSB(IR_FULL_BRIDGE);
            LoadIRFromUSB(IR_SHORT_ECHO);
            LoadIRFromUSB(IR_LONG_DECAY);
            
            // Show loaded IR status
            displayMgr.ShowIRLoaded(isStereoIR[irMode], irMode);
        }
    }
    
    // Update parameters from knobs
    dryWetMix = hw.GetKnobValue(Hothouse::KNOB_1);
    predelayMs = hw.GetKnobValue(Hothouse::KNOB_2) * 500.0f; // 0-500ms
    irLength = 0.1f + hw.GetKnobValue(Hothouse::KNOB_3) * 0.9f; // 10-100%
    lowCutFreq = 20.0f + hw.GetKnobValue(Hothouse::KNOB_4) * 1980.0f; // 20-2000Hz
    highCutFreq = 1000.0f + hw.GetKnobValue(Hothouse::KNOB_5) * 19000.0f; // 1000-20000Hz
    outputLevel = hw.GetKnobValue(Hothouse::KNOB_6); // 0-1.0
    
    // Get IR mode from switch 1
    int switchPos = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1);
    if (switchPos != irMode) {
        irMode = switchPos;
        
        // Load the appropriate IR
        if (irMode == IR_FULL_BRIDGE) {
            if (isStereoIR[IR_FULL_BRIDGE]) {
                convReverb.LoadStereoIR(echoBridgeIR_full, echoBridgeIR_full_R, irLength_full);
            } else {
                convReverb.LoadIR(echoBridgeIR_full, irLength_full);
            }
        } else if (irMode == IR_SHORT_ECHO) {
            if (isStereoIR[IR_SHORT_ECHO]) {
                convReverb.LoadStereoIR(echoBridgeIR_short, echoBridgeIR_short_R, irLength_short);
            } else {
                convReverb.LoadIR(echoBridgeIR_short, irLength_short);
            }
        } else if (irMode == IR_LONG_DECAY) {
            if (isStereoIR[IR_LONG_DECAY]) {
                convReverb.LoadStereoIR(echoBridgeIR_long, echoBridgeIR_long_R, irLength_long);
            } else {
                convReverb.LoadIR(echoBridgeIR_long, irLength_long);
            }
        }
        
        // Update display
        displayMgr.ShowIRLoaded(isStereoIR[irMode], irMode);
    }
    
    // Check for freezing from switch 2 and footswitch 1
    bool freeze_switch = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2) == Hothouse::TOGGLESWITCH_DOWN; // DOWN position
    bool freeze_pedal = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
    freeze = freeze_switch || freeze_pedal;
    
    // Get stereo width from switch 3
    int widthPos = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3);
    switch (widthPos) {
        case Hothouse::TOGGLESWITCH_UP:    stereoWidth = 0.5f; break; // Narrow
        case Hothouse::TOGGLESWITCH_MIDDLE: stereoWidth = 1.0f; break; // Normal
        case Hothouse::TOGGLESWITCH_DOWN:  stereoWidth = 1.5f; break; // Wide
    }
    
    // Update reverb parameters
    convReverb.SetPredelay(predelayMs);
    convReverb.SetIRLength(irLength);
    convReverb.SetLowCut(lowCutFreq);
    convReverb.SetHighCut(highCutFreq);
    convReverb.SetFreeze(freeze);
    convReverb.SetStereoWidth(stereoWidth);
    
    // Check if input is stereo
    if (size > 16) { // Need enough samples to detect
        bool newIsStereoInput = DetectStereoInput(in[0], in[1], 16);
        if (newIsStereoInput != isStereoInput) {
            isStereoInput = newIsStereoInput;
        }
    }
    
    // Update parameter display occasionally
    static size_t displayCounter = 0;
    if (++displayCounter >= 2000) { // Update display about every 2000 blocks
        displayCounter = 0;
        displayMgr.ShowParameters(dryWetMix, predelayMs, irLength, lowCutFreq, highCutFreq);
    }
    
    // Process audio
    for (size_t i = 0; i < size; ++i) {
        // Get input samples
        float in_sample_L = in[0][i];
        float in_sample_R = in[1][i];
        float wet_sample_L = 0.0f;
        float wet_sample_R = 0.0f;
        
        if (bypass) {
            // Bypass mode - just pass through
            out[0][i] = in_sample_L;
            out[1][i] = in_sample_R;
        } else {
            // Process through reverb
            convReverb.Process(in_sample_L, in_sample_R, &wet_sample_L, &wet_sample_R);
            
            // Mix dry and wet signals
            float mixed_L = (1.0f - dryWetMix) * in_sample_L + dryWetMix * wet_sample_L;
            float mixed_R = (1.0f - dryWetMix) * in_sample_R + dryWetMix * wet_sample_R;
            
            // Apply output level
            mixed_L *= outputLevel;
            mixed_R *= outputLevel;
            
            // Write to outputs
            out[0][i] = mixed_L;
            out[1][i] = mixed_R;
        }
    }
}

// Main function
int main() {
    // Initialize hardware
    hw.Init();
    hw.SetAudioBlockSize(48);  // Number of samples handled per callback
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    
    // Initialize display
    displayMgr.Init();
    displayMgr.ShowWelcomeScreen();
    
    // Initialize LEDs
    led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);
    led_freeze.Init(hw.seed.GetPin(Hothouse::LED_1), false);
    led_usb.Init(hw.seed.GetPin(22), false);    // Using GPIO22 for USB status LED
    led_stereo.Init(hw.seed.GetPin(23), false); // Using GPIO23 for stereo input detection LED
    
    // Initialize IR Loader and USB
    irLoader.Init();
    irLoader.StartUSB();
    
    // Generate test IRs for startup
    GenerateTestIRs();
    
    // Initialize convolution reverb
    float sampleRate = hw.AudioSampleRate();
    convReverb.Init(sampleRate, MAX_IR_LENGTH);
    
    // Load initial IR
    if (isStereoIR[IR_FULL_BRIDGE]) {
        convReverb.LoadStereoIR(echoBridgeIR_full, echoBridgeIR_full_R, irLength_full);
    } else {
        convReverb.LoadIR(echoBridgeIR_full, irLength_full);
    }
    
    // Register footswitch callbacks
    static FsCallbacks callbacks;
    callbacks.HandleNormalPress = FsCallbacks::HandleNormalPress;
    callbacks.HandleDoublePress = FsCallbacks::HandleDoublePress;
    callbacks.HandleLongPress = FsCallbacks::HandleLongPress;
    hw.RegisterFootswitchCallbacks(&callbacks);
    
    // Start processing
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    
    while (true) {
        hw.DelayMs(10);
        
        // Update LEDs
        led_bypass.Set(bypass ? 0.0f : 1.0f);
        led_freeze.Set(freeze ? 1.0f : 0.0f);
        led_usb.Set(usbMounted ? 1.0f : 0.0f);
        led_stereo.Set(isStereoInput ? 1.0f : 0.0f);
        
        led_bypass.Update();
        led_freeze.Update();
        led_usb.Update();
        led_stereo.Update();
        
        // Call System::ResetToBootloader() if FOOTSWITCH_1 is pressed for 2 seconds
        hw.CheckResetToBootloader();
    }
    
    return 0;
}