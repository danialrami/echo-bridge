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

// OLED display
using MyOledDisplay = daisy::OledDisplay<daisy::SSD130x4WireSpi128x64Driver>;
MyOledDisplay display;

// Display manager
DisplayManager displayMgr(display);

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

// Define max IR length as a global constant so it can be accessed from anywhere
static const size_t MAX_IR_LENGTH = 4096;

// Convolution implementation (now with stereo support and using ShyFFT)
class ConvolutionReverb {
private:
    // FFT size - reduced to save memory
    static const size_t FFT_SIZE = 512;
    static const size_t FFT_SIZE_X2 = FFT_SIZE * 2;
    
    // IR parameters - reduced max length to save memory
    float* irBuffer;        // Left/mono IR
    float* irBufferRight;   // Right IR for true stereo
    size_t irLength;
    
    // FFT buffers
    ShyFFT<float, FFT_SIZE> fft;  // Properly instantiate the template with float type and FFT_SIZE
    float fftTimeIn[FFT_SIZE];
    float fftTimeOut[FFT_SIZE];
    float fftFreqReal[FFT_SIZE];
    float fftFreqImag[FFT_SIZE];
    float fftFreqRealRight[FFT_SIZE];
    float fftFreqImagRight[FFT_SIZE];
    
    // IR frequency domain
    float irFreqReal[FFT_SIZE];
    float irFreqImag[FFT_SIZE];
    float irFreqRealRight[FFT_SIZE];
    float irFreqImagRight[FFT_SIZE];
    
    // Input/output buffers
    float inputBuffer[FFT_SIZE];
    float inputBufferRight[FFT_SIZE];
    float outputBuffer[FFT_SIZE_X2];
    float outputBufferRight[FFT_SIZE_X2];
    size_t inputBufferPos;
    size_t outputBufferPos;
    
    // Predelay buffer - reduced size to save memory
    static const size_t MAX_PREDELAY_SAMPLES = 24000; // 0.5s at 48kHz
    float predelayBuffer[MAX_PREDELAY_SAMPLES];
    float predelayBufferRight[MAX_PREDELAY_SAMPLES];
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
        // Use stack-allocated buffers instead of heap to avoid fragmentation
        float paddedIR[FFT_SIZE];
        float paddedIRRight[FFT_SIZE];
        
        // Clear the buffers
        memset(paddedIR, 0, FFT_SIZE * sizeof(float));
        memset(paddedIRRight, 0, FFT_SIZE * sizeof(float));
        
        // Apply IR length factor
        size_t effectiveIrLength = (size_t)(irLength * irLengthFactor);
        if (effectiveIrLength > irLength) effectiveIrLength = irLength;
        if (effectiveIrLength > FFT_SIZE) effectiveIrLength = FFT_SIZE;
        
        // Copy the IR data
        memcpy(paddedIR, irBuffer, effectiveIrLength * sizeof(float));
        if (trueStereoIR) {
            memcpy(paddedIRRight, irBufferRight, effectiveIrLength * sizeof(float));
        } else {
            memcpy(paddedIRRight, irBuffer, effectiveIrLength * sizeof(float));
        }
        
        // Transform IR to frequency domain
        fft.Direct(paddedIR, fftTimeOut);
        for (size_t i = 0; i < FFT_SIZE; i++) {
            irFreqReal[i] = fftTimeOut[i];
            irFreqImag[i] = 0.0f;
        }
        
        fft.Direct(paddedIRRight, fftTimeOut);
        for (size_t i = 0; i < FFT_SIZE; i++) {
            irFreqRealRight[i] = fftTimeOut[i];
            irFreqImagRight[i] = 0.0f;
        }
        
        return true;
    }
    
public:
    ConvolutionReverb() : 
        irBuffer(nullptr),
        irBufferRight(nullptr),
        irLength(0),
        inputBufferPos(0),
        outputBufferPos(0),
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
        // Initialize FFT
        fft.Init();
        
        // Clear buffers
        memset(inputBuffer, 0, sizeof(inputBuffer));
        memset(inputBufferRight, 0, sizeof(inputBufferRight));
        memset(outputBuffer, 0, sizeof(outputBuffer));
        memset(outputBufferRight, 0, sizeof(outputBufferRight));
        memset(predelayBuffer, 0, sizeof(predelayBuffer));
        memset(predelayBufferRight, 0, sizeof(predelayBufferRight));
        
        // Initialize filters
        lowCutFilterL.Init(sampleRate);
        highCutFilterL.Init(sampleRate);
        lowCutFilterR.Init(sampleRate);
        highCutFilterR.Init(sampleRate);
        
        // Set filter parameters
        UpdateFilters();
    }
    
    ~ConvolutionReverb() {
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
        
        // Initialize filters with sample rate
        lowCutFilterL.Init(sampleRate);
        highCutFilterL.Init(sampleRate);
        lowCutFilterR.Init(sampleRate);
        highCutFilterR.Init(sampleRate);
        
        // Update filter parameters
        UpdateFilters();
    }
    
    // Load IR from buffer
    bool LoadIR(float* buffer, size_t length) {
        // Check if length is valid
        if (length == 0 || length > MAX_IR_LENGTH) {
            return false;
        }
        
        // Free old buffer if it exists
        if (irBuffer) {
            delete[] irBuffer;
        }
        
        // Allocate new buffer
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
    
    // Load stereo IR from buffers
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
        
        // Allocate new buffers
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
        // Low cut filter
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
        predelayBuffer[predelayBufferPos] = inL;
        predelayBufferRight[predelayBufferPos] = inR;
        
        // Get delayed input
        size_t delayedPos = (predelayBufferPos + MAX_PREDELAY_SAMPLES - predelayInSamples) % MAX_PREDELAY_SAMPLES;
        float delayedL = predelayBuffer[delayedPos];
        float delayedR = predelayBufferRight[delayedPos];
        
        // Advance predelay buffer position
        predelayBufferPos = (predelayBufferPos + 1) % MAX_PREDELAY_SAMPLES;
        
        // Store input in buffer
        inputBuffer[inputBufferPos] = delayedL;
        inputBufferRight[inputBufferPos] = delayedR;
        inputBufferPos++;
        
        // If input buffer is full, process FFT
        if (inputBufferPos >= FFT_SIZE) {
            // Reset input buffer position
            inputBufferPos = 0;
            
            // Process left channel
            // Copy input buffer to FFT input
            memcpy(fftTimeIn, inputBuffer, FFT_SIZE * sizeof(float));
            
            // Transform to frequency domain
            fft.Direct(fftTimeIn, fftTimeOut);
            
            // Copy to frequency domain buffers
            for (size_t i = 0; i < FFT_SIZE; i++) {
                fftFreqReal[i] = fftTimeOut[i];
                fftFreqImag[i] = 0.0f;
            }
            
            // Complex multiplication with IR
            for (size_t i = 0; i < FFT_SIZE; i++) {
                // Real = (a*c - b*d), Imag = (a*d + b*c)
                float a = fftFreqReal[i];
                float b = fftFreqImag[i];
                float c = irFreqReal[i];
                float d = irFreqImag[i];
                
                fftFreqReal[i] = a*c - b*d;
                fftFreqImag[i] = a*d + b*c;
            }
            
            // Transform back to time domain - pass FFT_SIZE as the third parameter
            fft.Inverse(fftFreqReal, fftFreqImag, FFT_SIZE);
            
            // Process right channel
            // Copy input buffer to FFT input
            memcpy(fftTimeIn, inputBufferRight, FFT_SIZE * sizeof(float));
            
            // Transform to frequency domain
            fft.Direct(fftTimeIn, fftTimeOut);
            
            // Copy to frequency domain buffers
            for (size_t i = 0; i < FFT_SIZE; i++) {
                fftFreqRealRight[i] = fftTimeOut[i];
                fftFreqImagRight[i] = 0.0f;
            }
            
            // Complex multiplication with IR
            for (size_t i = 0; i < FFT_SIZE; i++) {
                // Real = (a*c - b*d), Imag = (a*d + b*c)
                float a = fftFreqRealRight[i];
                float b = fftFreqImagRight[i];
                float c = irFreqRealRight[i];
                float d = irFreqImagRight[i];
                
                fftFreqRealRight[i] = a*c - b*d;
                fftFreqImagRight[i] = a*d + b*c;
            }
            
            // Transform back to time domain - pass FFT_SIZE as the third parameter
            fft.Inverse(fftFreqRealRight, fftFreqImagRight, FFT_SIZE);
            
            // Overlap-add
            for (size_t i = 0; i < FFT_SIZE; i++) {
                outputBuffer[i] += fftFreqReal[i];
                outputBufferRight[i] += fftFreqRealRight[i];
            }
            
            // Shift output buffer
            memmove(outputBuffer, outputBuffer + FFT_SIZE, FFT_SIZE * sizeof(float));
            memmove(outputBufferRight, outputBufferRight + FFT_SIZE, FFT_SIZE * sizeof(float));
            
            // Clear second half of output buffer
            memset(outputBuffer + FFT_SIZE, 0, FFT_SIZE * sizeof(float));
            memset(outputBufferRight + FFT_SIZE, 0, FFT_SIZE * sizeof(float));
        }
        
        // Get output from buffer
        float wetL = outputBuffer[0];
        float wetR = outputBufferRight[0];
        
        // Apply filters - SVF Process method doesn't return a value, it modifies the input
        // Use temporary variables for filter processing
        float filteredL = wetL;
        float filteredR = wetR;
        
        // Process through low cut filter (high pass)
        lowCutFilterL.Process(filteredL);
        filteredL = lowCutFilterL.High();
        
        // Process through high cut filter (low pass)
        highCutFilterL.Process(filteredL);
        filteredL = highCutFilterL.Low();
        
        // Process right channel
        lowCutFilterR.Process(filteredR);
        filteredR = lowCutFilterR.High();
        
        highCutFilterR.Process(filteredR);
        filteredR = highCutFilterR.Low();
        
        // Apply stereo width
        if (stereoWidth != 1.0f) {
            float mid = (filteredL + filteredR) * 0.5f;
            float side = (filteredL - filteredR) * 0.5f * stereoWidth;
            filteredL = mid + side;
            filteredR = mid - side;
        }
        
        // Mix dry/wet
        *outL = inL * (1.0f - dryWet) + filteredL * dryWet;
        *outR = inR * (1.0f - dryWet) + filteredR * dryWet;
    }
};

// Global parameters
float dryWet = 0.5f;     // Dry/wet mix (0.0 - 1.0)
float predelay = 0.0f;   // Predelay in milliseconds
float irLength = 1.0f;   // IR length factor (0.0 - 1.0)
float lowCut = 100.0f;   // Low cut frequency
float highCut = 10000.0f; // High cut frequency
float stereoWidth = 1.0f; // Stereo width (0.0 - 2.0)

// Convolution reverb instance
ConvolutionReverb reverb;

// Function to detect if input is stereo
bool DetectStereoInput(const float* left, const float* right, size_t numSamples) {
    float sumDiff = 0.0f;
    float sumTotal = 0.0f;
    
    for (size_t i = 0; i < numSamples; i++) {
        sumDiff += fabsf(left[i] - right[i]);
        sumTotal += fabsf(left[i]) + fabsf(right[i]);
    }
    
    // If there's no signal, return previous state
    if (sumTotal < 0.001f) {
        return false;
    }
    
    // If difference is significant, it's stereo
    return (sumDiff / sumTotal) > 0.1f;
}

// Forward declarations for switch handlers
void HandlePress(Hothouse::Switches sw);
void HandleLongPress(Hothouse::Switches sw);

// Audio callback
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Process audio
    for (size_t i = 0; i < size; i++) {
        float inL = in[0][i];
        float inR = in[1][i];
        float outL, outR;
        
        if (bypass) {
            // Bypass mode
            outL = inL;
            outR = inR;
        } else {
            // Process through reverb
            reverb.Process(inL, inR, &outL, &outR);
        }
        
        // Output
        out[0][i] = outL;
        out[1][i] = outR;
    }
    
    // Check USB status
    bool newUsbMounted = irLoader.IsUSBMounted();
    if (newUsbMounted != usbMounted) {
        usbMounted = newUsbMounted;
        led_usb.Set(usbMounted ? 1.0f : 0.0f);
        displayMgr.ShowUSBStatus(usbMounted);
    }
    
    // Detect stereo input
    bool newIsStereoInput = DetectStereoInput(in[0], in[1], 16);
    if (newIsStereoInput != isStereoInput) {
        isStereoInput = newIsStereoInput;
        led_stereo.Set(isStereoInput ? 1.0f : 0.0f);
    }
    
    // Process knobs
    float knob1 = hw.GetKnobValue(Hothouse::KNOB_1);
    float knob2 = hw.GetKnobValue(Hothouse::KNOB_2);
    float knob3 = hw.GetKnobValue(Hothouse::KNOB_3);
    float knob4 = hw.GetKnobValue(Hothouse::KNOB_4);
    
    // Map knobs to parameters
    dryWet = knob1;
    predelay = knob2 * 500.0f; // 0-500ms
    irLength = knob3;
    
    // Knob 4 controls both filters
    if (knob4 < 0.5f) {
        // First half controls low cut (20Hz - 1000Hz)
        lowCut = 20.0f + knob4 * 2.0f * 980.0f;
        highCut = 10000.0f;
    } else {
        // Second half controls high cut (1000Hz - 20000Hz)
        lowCut = 20.0f;
        highCut = 1000.0f + (knob4 - 0.5f) * 2.0f * 19000.0f;
    }
    
    // Update reverb parameters
    reverb.SetDryWet(dryWet);
    reverb.SetPredelay(predelay);
    reverb.SetIRLengthFactor(irLength);
    reverb.SetLowCut(lowCut);
    reverb.SetHighCut(highCut);
    reverb.SetStereoWidth(stereoWidth);
}

// Switch callback
void SwitchCallback(Hothouse::Switches sw, bool state) {
    if (state) {
        // Switch pressed
        HandlePress(sw);
    }
}

// Handle switch press
void HandlePress(Hothouse::Switches sw) {
    switch (sw) {
        case Hothouse::FOOTSWITCH_1:
            // Toggle bypass
            bypass = !bypass;
            led_bypass.Set(bypass ? 0.0f : 1.0f);
            displayMgr.ShowBypass(bypass);
            break;
            
        case Hothouse::FOOTSWITCH_2:
            // Toggle freeze
            freeze = !freeze;
            led_freeze.Set(freeze ? 1.0f : 0.0f);
            
            // Update display
            displayMgr.ShowParameters(dryWet, predelay, irLength, lowCut, highCut);
            break;
            
        default:
            // Handle other switches if needed
            break;
    }
}

// Handle long press
void HandleLongPress(Hothouse::Switches sw) {
    switch (sw) {
        case Hothouse::FOOTSWITCH_1:
            // Reset all parameters to default
            dryWet = 0.5f;
            predelay = 0.0f;
            irLength = 1.0f;
            lowCut = 100.0f;
            highCut = 10000.0f;
            stereoWidth = 1.0f;
            
            // Update reverb parameters
            reverb.SetDryWet(dryWet);
            reverb.SetPredelay(predelay);
            reverb.SetIRLengthFactor(irLength);
            reverb.SetLowCut(lowCut);
            reverb.SetHighCut(highCut);
            reverb.SetStereoWidth(stereoWidth);
            
            // Update display
            displayMgr.ShowParameters(dryWet, predelay, irLength, lowCut, highCut);
            break;
            
        case Hothouse::FOOTSWITCH_2:
            // Load IR from USB
            if (usbMounted) {
                // Allocate buffers for IR data
                float* irBuffer = new float[MAX_IR_LENGTH];
                float* irBufferRight = new float[MAX_IR_LENGTH];
                
                if (!irBuffer || !irBufferRight) {
                    delete[] irBuffer;
                    delete[] irBufferRight;
                    return;
                }
                
                // Load IR files
                size_t irLength = 0;
                bool loaded = false;
                
                if (isStereoInput) {
                    // Load stereo IR
                    loaded = irLoader.LoadStereoIRFiles("/ir_left.wav", "/ir_right.wav", 
                                                       irBuffer, irBufferRight, 
                                                       &irLength, MAX_IR_LENGTH);
                    
                    if (loaded) {
                        // Load into reverb
                        reverb.LoadStereoIR(irBuffer, irBufferRight, irLength);
                    }
                } else {
                    // Load mono IR
                    loaded = irLoader.LoadIRFile("/ir_mono.wav", irBuffer, &irLength, 
                                               MAX_IR_LENGTH);
                    
                    if (loaded) {
                        // Load into reverb
                        reverb.LoadIR(irBuffer, irLength);
                    }
                }
                
                // Free buffers
                delete[] irBuffer;
                delete[] irBufferRight;
                
                // Update state
                irLoaded = loaded;
                
                // Update display
                displayMgr.ShowIRLoaded(loaded);
            }
            break;
            
        default:
            // Handle other switches if needed
            break;
    }
}

// Footswitch callback struct
Hothouse::FootswitchCallbacks footswitchCallbacks = {
    .HandleNormalPress = HandlePress,
    .HandleDoublePress = nullptr,
    .HandleLongPress = HandleLongPress
};

int main(void) {
    // Initialize hardware
    hw.Init();
    
    // Initialize LEDs
    led_bypass.Init(hw.seed.GetPin(Hothouse::LED_1), false);
    led_freeze.Init(hw.seed.GetPin(Hothouse::LED_2), false);
    
    // Use GPIO pins for additional LEDs
    // Note: Using pins 15 and 16 as examples - adjust based on available pins
    led_usb.Init(hw.seed.GetPin(15), false);
    led_stereo.Init(hw.seed.GetPin(16), false);
    
    // Initialize display
    daisy::SpiHandle::Config spiConfig;
    spiConfig.periph = daisy::SpiHandle::Config::Peripheral::SPI_1;
    
    // Use GPIO pins for SPI - adjust based on actual pin mapping
    spiConfig.pin_config.sclk = hw.seed.GetPin(9);  // Example pin
    spiConfig.pin_config.miso = hw.seed.GetPin(10); // Example pin
    spiConfig.pin_config.mosi = hw.seed.GetPin(11); // Example pin
    spiConfig.pin_config.nss = hw.seed.GetPin(8);   // Example pin
    
    daisy::SSD130x4WireSpi128x64Driver::Config oledConfig;
    oledConfig.transport_config.spi_config = spiConfig;
    oledConfig.transport_config.pin_config.dc = hw.seed.GetPin(12);    // Example pin
    oledConfig.transport_config.pin_config.reset = hw.seed.GetPin(13); // Example pin
    
    MyOledDisplay::Config displayConfig;
    displayConfig.driver_config = oledConfig;
    display.Init(displayConfig);
    
    // Initialize display manager
    displayMgr.Init();
    displayMgr.ShowWelcomeScreen();
    
    // Initialize USB
    irLoader.Init();
    irLoader.StartUSB();
    
    // Initialize reverb
    reverb.Init(hw.AudioSampleRate());
    
    // Register footswitch callbacks
    hw.RegisterFootswitchCallbacks(&footswitchCallbacks);
    
    // Set audio callback
    hw.StartAudio(AudioCallback);
    
    // Main loop
    while(1) {
        // Process USB
        irLoader.Process();
        
        // Update LEDs
        led_bypass.Update();
        led_freeze.Update();
        led_usb.Update();
        led_stereo.Update();
        
        // Process all controls
        hw.ProcessAllControls();
        
        // Sleep to save power
        System::Delay(1);
    }
}
