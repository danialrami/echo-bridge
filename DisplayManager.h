#pragma once

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"

using namespace daisy;

// Class for abstracting OLED display functionality
class DisplayManager 
{
public:
    DisplayManager(OledDisplay<SSD130x4WireSpi128x64Driver>& disp) : display_(disp) {}
    
    void Init() 
    {
        display_.Fill(false);
        display_.Update();
    }
    
    void ShowWelcomeScreen() 
    {
        display_.Fill(false);
        display_.SetCursor(0, 0);
        display_.WriteString("Echo Bridge", Font_7x10, true);
        display_.SetCursor(0, 12);
        display_.WriteString("Stereo Mode", Font_7x10, true);
        display_.Update();
    }
    
    void ShowUSBStatus(bool connected) 
    {
        display_.Fill(false);
        display_.SetCursor(0, 0);
        display_.WriteString("Echo Bridge", Font_7x10, true);
        display_.SetCursor(0, 12);
        display_.WriteString(connected ? "USB Connected" : "USB Disconnected", Font_7x10, true);
        display_.Update();
    }
    
    void ShowIRLoaded(bool loaded) 
    {
        display_.SetCursor(0, 24);
        snprintf(buffer_, sizeof(buffer_), "IR %s", loaded ? "Loaded" : "Failed");
        display_.WriteString(buffer_, Font_7x10, true);
        display_.Update();
    }
    
    void ShowBypass(bool bypassed) 
    {
        display_.Fill(false);
        display_.SetCursor(0, 0);
        display_.WriteString("Echo Bridge", Font_7x10, true);
        display_.SetCursor(0, 12);
        display_.WriteString(bypassed ? "BYPASSED" : "ACTIVE", Font_7x10, true);
        display_.Update();
    }
    
    void ShowParameters(float dryWet, float predelay, float irLength, float lowCut, float highCut) 
    {
        display_.Fill(false);
        display_.SetCursor(0, 0);
        display_.WriteString("Parameters:", Font_7x10, true);
        
        snprintf(buffer_, sizeof(buffer_), "Mix:%d%% Pre:%dms", (int)(dryWet * 100), (int)predelay);
        display_.SetCursor(0, 12);
        display_.WriteString(buffer_, Font_7x10, true);
        
        snprintf(buffer_, sizeof(buffer_), "IR:%d%% LC:%dHz", (int)(irLength * 100), (int)lowCut);
        display_.SetCursor(0, 24);
        display_.WriteString(buffer_, Font_7x10, true);
        
        snprintf(buffer_, sizeof(buffer_), "HC:%dHz", (int)highCut);
        display_.SetCursor(0, 36);
        display_.WriteString(buffer_, Font_7x10, true);
        
        display_.Update();
    }
    
    void ShowErrorMessage(const char* message) 
    {
        display_.Fill(false);
        display_.SetCursor(0, 0);
        display_.WriteString("Error:", Font_7x10, true);
        display_.SetCursor(0, 12);
        display_.WriteString(message, Font_7x10, true);
        display_.Update();
    }
    
private:
    OledDisplay<SSD130x4WireSpi128x64Driver>& display_;
    char buffer_[64];
};
