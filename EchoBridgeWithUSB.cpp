#include "daisysp.h"
#include "hothouse.h"
#include "IRLoader.h"
#include "DisplayManager.h"
#include <arm_math.h>
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

// Convolution implementation (now with stereo support)
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
        
        // Maximum predelay of 500ms
        predelayBufferSize = static_cast<size_t>(sampleRate * 0.5f);
        predelayBufferL = new float[predelayBufferSize]();
        predelayBufferR = new float[predelayBufferSize]();
        
        // Initialize positions
        inputBufferPos = 0;
        outputBufferPos = 0;
        predelayBufferPos = 0;
        
        // Initialize FFT
        arm_cfft_init_f32(&fftInstance, FFT_SIZE);
        
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
        
        // Create a temporary buffer for FFT processing
        float temp[FFT_SIZE * 2] = {0};
        
        // Calculate effective IR length based on multiplier
        size_t effectiveIrLength = static_cast<size_t>(irLength * irLengthMultiplier);
        if (effectiveIrLength > FFT_SIZE) effectiveIrLength = FFT_SIZE;
        
        // Process left channel IR
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
        
        // Process right channel IR
        // Clear temp buffer
        memset(temp, 0, FFT_SIZE * 2 * sizeof(float));
        
        // Copy IR to temporary buffer (real parts only)
        for (size_t i = 0; i < effectiveIrLength; i++) {
            temp[i * 2] = irBufferRight[i];
            temp[i * 2 + 1] = 0.0f;  // Imaginary part is zero
        }
        
        // Perform FFT
        arm_cfft_f32(&fftInstance, temp, 0, 1);
        
        // Store results
        for (size_t i = 0; i < FFT_SIZE; i++) {
            irFreqRealRight[i] = temp[i * 2];
            irFreqImagRight[i] = temp[i * 2 + 1];
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