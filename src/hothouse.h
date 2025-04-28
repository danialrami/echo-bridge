#pragma once

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "hid/encoder.h"
#include "hid/switch.h"

namespace clevelandmusicco {

class Hothouse {
public:
    // Pin definitions as integers
    enum Pin {
        // Knobs (ADC inputs)
        KNOB_1 = 15,  // A0
        KNOB_2 = 16,  // A1
        KNOB_3 = 17,  // A2
        KNOB_4 = 18,  // A3
        KNOB_5 = 19,  // A4
        KNOB_6 = 20,  // A5
        
        // Footswitches (digital inputs)
        FOOTSWITCH_1 = 1,  // D1
        FOOTSWITCH_2 = 2,  // D2
        
        // Toggles (digital inputs)
        TOGGLE_1 = 7,  // D7
        TOGGLE_2 = 8,  // D8
        TOGGLE_3 = 9,  // D9
        
        // LEDs (digital outputs)
        LED_1 = 10,  // D10
        LED_2 = 11,  // D11
        LED_3 = 12,  // D12
        LED_4 = 13,  // D13
        
        // OLED display (SPI)
        OLED_DC = 3,  // D3
        OLED_RESET = 4  // D4
    };
    
    Hothouse() {}
    
    void Init() {
        // Initialize seed hardware
        seed.Configure();
        seed.Init();
        
        // Initialize audio - use public API
        daisy::AudioHandle::Config audio_config;
        audio_config.samplerate = daisy::SaiHandle::Config::SampleRate::SAI_48KHZ;
        audio_config.blocksize = 48;
        seed.SetAudioBlockSize(audio_config.blocksize);
        seed.SetAudioSampleRate(audio_config.samplerate);
        
        // Don't start audio yet - will be started later with actual callback
        // Removed the ambiguous nullptr call
        
        // Initialize ADC for knobs
        daisy::AdcChannelConfig adcConfig[6];
        for (int i = 0; i < 6; i++) {
            adcConfig[i].InitSingle(seed.GetPin(GetKnobPin(i)));
            adcValues_[i] = 0.0f;
        }
        seed.adc.Init(adcConfig, 6);
        seed.adc.Start();
        
        // Initialize footswitches
        for (int i = 0; i < 2; i++) {
            footswitches_[i].Init(seed.GetPin(GetFootswitchPin(i)), 1000);
            footswitchStates_[i] = false;
            footswitchPressTime_[i] = 0;
            footswitchCallbacks_[i] = nullptr;
            footswitchLongPressCallbacks_[i] = nullptr;
        }
        
        // Initialize toggles
        for (int i = 0; i < 3; i++) {
            toggles_[i].Init(seed.GetPin(GetTogglePin(i)), 1000);
            toggleStates_[i] = 0;
            toggleCallbacks_[i] = nullptr;
        }
    }
    
    void ProcessAllControls() {
        // Process ADC readings
        for (int i = 0; i < 6; i++) {
            float newValue = seed.adc.GetFloat(i);
            
            // Apply some smoothing
            adcValues_[i] = adcValues_[i] * 0.9f + newValue * 0.1f;
            
            // Call callback if value changed significantly
            if (knobCallbacks_[i] != nullptr && fabsf(adcValues_[i] - lastKnobValues_[i]) > 0.01f) {
                knobCallbacks_[i](adcValues_[i]);
                lastKnobValues_[i] = adcValues_[i];
            }
        }
        
        // Process footswitches
        for (int i = 0; i < 2; i++) {
            footswitches_[i].Debounce();
            
            // Check for press
            if (footswitches_[i].RisingEdge()) {
                footswitchStates_[i] = true;
                footswitchPressTime_[i] = daisy::System::GetNow();
                
                // Call callback
                if (footswitchCallbacks_[i] != nullptr) {
                    footswitchCallbacks_[i](true);
                }
            }
            
            // Check for release
            if (footswitches_[i].FallingEdge()) {
                footswitchStates_[i] = false;
                
                // Check for long press
                uint32_t pressDuration = daisy::System::GetNow() - footswitchPressTime_[i];
                if (pressDuration > 1000 && footswitchLongPressCallbacks_[i] != nullptr) {
                    footswitchLongPressCallbacks_[i]();
                }
                
                // Call callback
                if (footswitchCallbacks_[i] != nullptr) {
                    footswitchCallbacks_[i](false);
                }
            }
        }
        
        // Process toggles
        for (int i = 0; i < 3; i++) {
            toggles_[i].Debounce();
            
            // Check for state change
            int newState = toggles_[i].Pressed() ? 1 : -1;
            
            if (newState != toggleStates_[i]) {
                toggleStates_[i] = newState;
                
                // Call callback
                if (toggleCallbacks_[i] != nullptr) {
                    toggleCallbacks_[i](newState > 0);
                }
            }
        }
    }
    
    // Start audio processing
    void StartAudio(daisy::AudioHandle::AudioCallback cb) {
        seed.StartAudio(cb);
    }
    
    // Get audio sample rate
    float AudioSampleRate() {
        return seed.AudioSampleRate();
    }
    
    // Set callbacks
    void SetKnobCallback(int knob, void (*callback)(float)) {
        if (knob >= 0 && knob < 6) {
            knobCallbacks_[knob] = callback;
        }
    }
    
    void SetFootswitchCallback(int footswitch, void (*callback)(bool)) {
        if (footswitch >= 0 && footswitch < 2) {
            footswitchCallbacks_[footswitch] = callback;
        }
    }
    
    void SetFootswitchLongPressCallback(int footswitch, void (*callback)()) {
        if (footswitch >= 0 && footswitch < 2) {
            footswitchLongPressCallbacks_[footswitch] = callback;
        }
    }
    
    void SetToggleCallback(int toggle, void (*callback)(bool)) {
        if (toggle >= 0 && toggle < 3) {
            toggleCallbacks_[toggle] = callback;
        }
    }
    
    // Helper functions to get pin numbers
    Pin GetKnobPin(int knob) {
        switch (knob) {
            case 0: return KNOB_1;
            case 1: return KNOB_2;
            case 2: return KNOB_3;
            case 3: return KNOB_4;
            case 4: return KNOB_5;
            case 5: return KNOB_6;
            default: return KNOB_1;
        }
    }
    
    Pin GetFootswitchPin(int footswitch) {
        switch (footswitch) {
            case 0: return FOOTSWITCH_1;
            case 1: return FOOTSWITCH_2;
            default: return FOOTSWITCH_1;
        }
    }
    
    Pin GetTogglePin(int toggle) {
        switch (toggle) {
            case 0: return TOGGLE_1;
            case 1: return TOGGLE_2;
            case 2: return TOGGLE_3;
            default: return TOGGLE_1;
        }
    }
    
    // Public access to seed hardware
    daisy::DaisySeed seed;
    
private:
    // ADC values for knobs
    float adcValues_[6];
    float lastKnobValues_[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    
    // Footswitches
    daisy::Switch footswitches_[2];
    bool footswitchStates_[2];
    uint32_t footswitchPressTime_[2];
    
    // Toggles
    daisy::Switch toggles_[3];
    int toggleStates_[3];
    
    // Callbacks
    void (*knobCallbacks_[6])(float);
    void (*footswitchCallbacks_[2])(bool);
    void (*footswitchLongPressCallbacks_[2])();
    void (*toggleCallbacks_[3])(bool);
};

} // namespace clevelandmusicco
