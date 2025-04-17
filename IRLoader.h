#pragma once

#include "daisy_seed.h"
#include <string>

using namespace daisy;

// Class for handling USB drive mounting and IR file loading
class IRLoader {
public:
    IRLoader() : usbh_(nullptr), usbMounted_(false) {}
    
    void Init() {
        // Initialize USB Host
        usbh_ = new USBHostHandle();
        USBHostHandle::Config usbConfig;
        usbh_->Init(usbConfig);
    }
    
    void StartUSB() {
        if (usbh_) {
            // Process USB events to start
            usbh_->Process();
        }
    }
    
    void Process() {
        if (usbh_) {
            // Process USB events
            usbh_->Process();
            
            // Check if USB drive is mounted
            bool newMounted = usbh_->GetReady();
            if (newMounted != usbMounted_) {
                usbMounted_ = newMounted;
                // Handle mount/unmount events here if needed
            }
        }
    }
    
    bool IsUSBMounted() const {
        return usbMounted_;
    }
    
    // Load IR file from USB drive
    bool LoadIRFile(const char* filename, float* buffer, size_t* length, size_t maxLength) {
        if (!usbMounted_ || !usbh_) {
            return false;
        }
        
        // Open file
        FIL file;
        FRESULT result = f_open(&file, filename, FA_READ);
        if (result != FR_OK) {
            return false;
        }
        
        // Get file size
        FSIZE_t fileSize = f_size(&file);
        size_t numSamples = fileSize / sizeof(float);
        
        // Check if buffer is large enough
        if (numSamples > maxLength) {
            numSamples = maxLength;
        }
        
        // Read file
        UINT bytesRead;
        result = f_read(&file, buffer, numSamples * sizeof(float), &bytesRead);
        f_close(&file);
        
        if (result != FR_OK) {
            return false;
        }
        
        // Update length
        *length = bytesRead / sizeof(float);
        
        return true;
    }
    
    // Load stereo IR files from USB drive
    bool LoadStereoIRFiles(const char* filenameL, const char* filenameR, 
                          float* bufferL, float* bufferR, 
                          size_t* length, size_t maxLength) {
        if (!usbMounted_ || !usbh_) {
            return false;
        }
        
        // Load left channel
        bool leftOk = LoadIRFile(filenameL, bufferL, length, maxLength);
        if (!leftOk) {
            return false;
        }
        
        // Load right channel
        size_t rightLength;
        bool rightOk = LoadIRFile(filenameR, bufferR, &rightLength, maxLength);
        if (!rightOk) {
            return false;
        }
        
        // Check if lengths match
        if (*length != rightLength) {
            // Truncate to shorter length
            *length = (*length < rightLength) ? *length : rightLength;
        }
        
        return true;
    }
    
private:
    USBHostHandle* usbh_;
    bool usbMounted_;
};
