#pragma once

#include "daisy_seed.h"
#include "hid/usb_host.h"
#include <string.h>

using namespace daisy;

// Define max IR length as a global constant so it can be accessed from IRLoader
static const size_t MAX_IR_LENGTH = 4096;

// Class for loading impulse response files from USB
class IRLoader {
public:
    IRLoader() : usbh_(nullptr), mounted_(false) {}
    
    void Init() {
        // Initialize USB host
        usbh_ = new USBHostHandle();
        usbh_->Init(USBHostHandle::Config());
    }
    
    void Process() {
        // Process USB host events
        if (usbh_) {
            usbh_->Process();
            
            // Check if USB drive is mounted
            // In newer API, we need to check if the filesystem is ready
            bool mounted = f_mount(nullptr, "", 0) == FR_OK;
            
            // If mount state changed
            if (mounted != mounted_) {
                mounted_ = mounted;
                
                // If newly mounted, try to load IR
                if (mounted_) {
                    LoadIR();
                }
            }
        }
    }
    
    bool LoadIR() {
        if (!usbh_ || !mounted_) {
            return false;
        }
        
        // Try to load mono IR first
        if (LoadMonoIR()) {
            return true;
        }
        
        // If mono IR not found, try to load stereo IR
        return LoadStereoIR();
    }
    
    bool LoadMonoIR() {
        // Open file
        FIL file;
        FRESULT result = f_open(&file, "ir_mono.wav", FA_READ);
        
        if (result != FR_OK) {
            return false;
        }
        
        // Read WAV header
        WAVHeader header;
        UINT bytesRead;
        result = f_read(&file, &header, sizeof(WAVHeader), &bytesRead);
        
        if (result != FR_OK || bytesRead != sizeof(WAVHeader)) {
            f_close(&file);
            return false;
        }
        
        // Check if valid WAV file
        if (strncmp(header.riff, "RIFF", 4) != 0 || 
            strncmp(header.wave, "WAVE", 4) != 0 || 
            strncmp(header.fmt, "fmt ", 4) != 0 || 
            strncmp(header.data, "data", 4) != 0) {
            f_close(&file);
            return false;
        }
        
        // Check if supported format (PCM)
        if (header.audioFormat != 1) {
            f_close(&file);
            return false;
        }
        
        // Calculate number of samples
        uint32_t numSamples = header.dataSize / (header.numChannels * (header.bitsPerSample / 8));
        
        // Limit to MAX_IR_LENGTH
        if (numSamples > MAX_IR_LENGTH) {
            numSamples = MAX_IR_LENGTH;
        }
        
        // Allocate buffer
        float* irBuffer = new float[numSamples];
        
        if (!irBuffer) {
            f_close(&file);
            return false;
        }
        
        // Read samples
        if (header.bitsPerSample == 16) {
            // 16-bit samples
            int16_t* tempBuffer = new int16_t[numSamples * header.numChannels];
            
            if (!tempBuffer) {
                delete[] irBuffer;
                f_close(&file);
                return false;
            }
            
            result = f_read(&file, tempBuffer, numSamples * header.numChannels * sizeof(int16_t), &bytesRead);
            
            if (result != FR_OK) {
                delete[] tempBuffer;
                delete[] irBuffer;
                f_close(&file);
                return false;
            }
            
            // Convert to float
            for (uint32_t i = 0; i < numSamples; i++) {
                // If stereo, average channels
                if (header.numChannels == 2) {
                    irBuffer[i] = ((float)tempBuffer[i*2] + (float)tempBuffer[i*2+1]) / 65536.0f;
                } else {
                    irBuffer[i] = (float)tempBuffer[i] / 32768.0f;
                }
            }
            
            delete[] tempBuffer;
        } else if (header.bitsPerSample == 24) {
            // 24-bit samples
            uint8_t* tempBuffer = new uint8_t[numSamples * header.numChannels * 3];
            
            if (!tempBuffer) {
                delete[] irBuffer;
                f_close(&file);
                return false;
            }
            
            result = f_read(&file, tempBuffer, numSamples * header.numChannels * 3, &bytesRead);
            
            if (result != FR_OK) {
                delete[] tempBuffer;
                delete[] irBuffer;
                f_close(&file);
                return false;
            }
            
            // Convert to float
            for (uint32_t i = 0; i < numSamples; i++) {
                // If stereo, average channels
                if (header.numChannels == 2) {
                    int32_t sampleL = (tempBuffer[i*6] << 8) | (tempBuffer[i*6+1] << 16) | (tempBuffer[i*6+2] << 24);
                    int32_t sampleR = (tempBuffer[i*6+3] << 8) | (tempBuffer[i*6+4] << 16) | (tempBuffer[i*6+5] << 24);
                    sampleL >>= 8;
                    sampleR >>= 8;
                    irBuffer[i] = ((float)sampleL + (float)sampleR) / 16777216.0f;
                } else {
                    int32_t sample = (tempBuffer[i*3] << 8) | (tempBuffer[i*3+1] << 16) | (tempBuffer[i*3+2] << 24);
                    sample >>= 8;
                    irBuffer[i] = (float)sample / 8388608.0f;
                }
            }
            
            delete[] tempBuffer;
        } else if (header.bitsPerSample == 32) {
            // 32-bit samples (assume float)
            result = f_read(&file, irBuffer, numSamples * sizeof(float), &bytesRead);
            
            if (result != FR_OK) {
                delete[] irBuffer;
                f_close(&file);
                return false;
            }
        } else {
            // Unsupported bit depth
            delete[] irBuffer;
            f_close(&file);
            return false;
        }
        
        // Close file
        f_close(&file);
        
        // Normalize IR
        float maxAbs = 0.0f;
        for (uint32_t i = 0; i < numSamples; i++) {
            float absVal = fabsf(irBuffer[i]);
            if (absVal > maxAbs) {
                maxAbs = absVal;
            }
        }
        
        if (maxAbs > 0.0f) {
            for (uint32_t i = 0; i < numSamples; i++) {
                irBuffer[i] /= maxAbs;
            }
        }
        
        // Load IR into reverb
        bool success = LoadIRCallback(irBuffer, nullptr, numSamples);
        
        // Free buffer
        delete[] irBuffer;
        
        return success;
    }
    
    bool LoadStereoIR() {
        // Open left channel file
        FIL fileL;
        FRESULT result = f_open(&fileL, "ir_left.wav", FA_READ);
        
        if (result != FR_OK) {
            return false;
        }
        
        // Open right channel file
        FIL fileR;
        result = f_open(&fileR, "ir_right.wav", FA_READ);
        
        if (result != FR_OK) {
            f_close(&fileL);
            return false;
        }
        
        // Read WAV headers
        WAVHeader headerL, headerR;
        UINT bytesReadL, bytesReadR;
        
        result = f_read(&fileL, &headerL, sizeof(WAVHeader), &bytesReadL);
        if (result != FR_OK || bytesReadL != sizeof(WAVHeader)) {
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        result = f_read(&fileR, &headerR, sizeof(WAVHeader), &bytesReadR);
        if (result != FR_OK || bytesReadR != sizeof(WAVHeader)) {
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        // Check if valid WAV files
        if (strncmp(headerL.riff, "RIFF", 4) != 0 || 
            strncmp(headerL.wave, "WAVE", 4) != 0 || 
            strncmp(headerL.fmt, "fmt ", 4) != 0 || 
            strncmp(headerL.data, "data", 4) != 0 ||
            strncmp(headerR.riff, "RIFF", 4) != 0 || 
            strncmp(headerR.wave, "WAVE", 4) != 0 || 
            strncmp(headerR.fmt, "fmt ", 4) != 0 || 
            strncmp(headerR.data, "data", 4) != 0) {
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        // Check if supported format (PCM)
        if (headerL.audioFormat != 1 || headerR.audioFormat != 1) {
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        // Calculate number of samples
        uint32_t numSamplesL = headerL.dataSize / (headerL.numChannels * (headerL.bitsPerSample / 8));
        uint32_t numSamplesR = headerR.dataSize / (headerR.numChannels * (headerR.bitsPerSample / 8));
        
        // Use the smaller of the two
        uint32_t numSamples = (numSamplesL < numSamplesR) ? numSamplesL : numSamplesR;
        
        // Limit to MAX_IR_LENGTH
        if (numSamples > MAX_IR_LENGTH) {
            numSamples = MAX_IR_LENGTH;
        }
        
        // Allocate buffers
        float* irBufferL = new float[numSamples];
        float* irBufferR = new float[numSamples];
        
        if (!irBufferL || !irBufferR) {
            if (irBufferL) delete[] irBufferL;
            if (irBufferR) delete[] irBufferR;
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        // Read samples from left channel
        if (headerL.bitsPerSample == 16) {
            // 16-bit samples
            int16_t* tempBuffer = new int16_t[numSamples * headerL.numChannels];
            
            if (!tempBuffer) {
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            result = f_read(&fileL, tempBuffer, numSamples * headerL.numChannels * sizeof(int16_t), &bytesReadL);
            
            if (result != FR_OK) {
                delete[] tempBuffer;
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            // Convert to float
            for (uint32_t i = 0; i < numSamples; i++) {
                // If stereo, use left channel
                if (headerL.numChannels == 2) {
                    irBufferL[i] = (float)tempBuffer[i*2] / 32768.0f;
                } else {
                    irBufferL[i] = (float)tempBuffer[i] / 32768.0f;
                }
            }
            
            delete[] tempBuffer;
        } else if (headerL.bitsPerSample == 24) {
            // 24-bit samples
            uint8_t* tempBuffer = new uint8_t[numSamples * headerL.numChannels * 3];
            
            if (!tempBuffer) {
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            result = f_read(&fileL, tempBuffer, numSamples * headerL.numChannels * 3, &bytesReadL);
            
            if (result != FR_OK) {
                delete[] tempBuffer;
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            // Convert to float
            for (uint32_t i = 0; i < numSamples; i++) {
                // If stereo, use left channel
                if (headerL.numChannels == 2) {
                    int32_t sample = (tempBuffer[i*6] << 8) | (tempBuffer[i*6+1] << 16) | (tempBuffer[i*6+2] << 24);
                    sample >>= 8;
                    irBufferL[i] = (float)sample / 8388608.0f;
                } else {
                    int32_t sample = (tempBuffer[i*3] << 8) | (tempBuffer[i*3+1] << 16) | (tempBuffer[i*3+2] << 24);
                    sample >>= 8;
                    irBufferL[i] = (float)sample / 8388608.0f;
                }
            }
            
            delete[] tempBuffer;
        } else if (headerL.bitsPerSample == 32) {
            // 32-bit samples (assume float)
            result = f_read(&fileL, irBufferL, numSamples * sizeof(float), &bytesReadL);
            
            if (result != FR_OK) {
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
        } else {
            // Unsupported bit depth
            delete[] irBufferL;
            delete[] irBufferR;
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        // Read samples from right channel
        if (headerR.bitsPerSample == 16) {
            // 16-bit samples
            int16_t* tempBuffer = new int16_t[numSamples * headerR.numChannels];
            
            if (!tempBuffer) {
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            result = f_read(&fileR, tempBuffer, numSamples * headerR.numChannels * sizeof(int16_t), &bytesReadR);
            
            if (result != FR_OK) {
                delete[] tempBuffer;
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            // Convert to float
            for (uint32_t i = 0; i < numSamples; i++) {
                // If stereo, use right channel
                if (headerR.numChannels == 2) {
                    irBufferR[i] = (float)tempBuffer[i*2+1] / 32768.0f;
                } else {
                    irBufferR[i] = (float)tempBuffer[i] / 32768.0f;
                }
            }
            
            delete[] tempBuffer;
        } else if (headerR.bitsPerSample == 24) {
            // 24-bit samples
            uint8_t* tempBuffer = new uint8_t[numSamples * headerR.numChannels * 3];
            
            if (!tempBuffer) {
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            result = f_read(&fileR, tempBuffer, numSamples * headerR.numChannels * 3, &bytesReadR);
            
            if (result != FR_OK) {
                delete[] tempBuffer;
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
            
            // Convert to float
            for (uint32_t i = 0; i < numSamples; i++) {
                // If stereo, use right channel
                if (headerR.numChannels == 2) {
                    int32_t sample = (tempBuffer[i*6+3] << 8) | (tempBuffer[i*6+4] << 16) | (tempBuffer[i*6+5] << 24);
                    sample >>= 8;
                    irBufferR[i] = (float)sample / 8388608.0f;
                } else {
                    int32_t sample = (tempBuffer[i*3] << 8) | (tempBuffer[i*3+1] << 16) | (tempBuffer[i*3+2] << 24);
                    sample >>= 8;
                    irBufferR[i] = (float)sample / 8388608.0f;
                }
            }
            
            delete[] tempBuffer;
        } else if (headerR.bitsPerSample == 32) {
            // 32-bit samples (assume float)
            result = f_read(&fileR, irBufferR, numSamples * sizeof(float), &bytesReadR);
            
            if (result != FR_OK) {
                delete[] irBufferL;
                delete[] irBufferR;
                f_close(&fileL);
                f_close(&fileR);
                return false;
            }
        } else {
            // Unsupported bit depth
            delete[] irBufferL;
            delete[] irBufferR;
            f_close(&fileL);
            f_close(&fileR);
            return false;
        }
        
        // Close files
        f_close(&fileL);
        f_close(&fileR);
        
        // Normalize IRs
        float maxAbsL = 0.0f;
        float maxAbsR = 0.0f;
        
        for (uint32_t i = 0; i < numSamples; i++) {
            float absValL = fabsf(irBufferL[i]);
            float absValR = fabsf(irBufferR[i]);
            
            if (absValL > maxAbsL) {
                maxAbsL = absValL;
            }
            
            if (absValR > maxAbsR) {
                maxAbsR = absValR;
            }
        }
        
        // Use the larger of the two for normalization
        float maxAbs = (maxAbsL > maxAbsR) ? maxAbsL : maxAbsR;
        
        if (maxAbs > 0.0f) {
            for (uint32_t i = 0; i < numSamples; i++) {
                irBufferL[i] /= maxAbs;
                irBufferR[i] /= maxAbs;
            }
        }
        
        // Load IR into reverb
        bool success = LoadIRCallback(irBufferL, irBufferR, numSamples);
        
        // Free buffers
        delete[] irBufferL;
        delete[] irBufferR;
        
        return success;
    }
    
    // Set callback for loading IR
    typedef bool (*LoadIRCallbackFn)(float* bufferL, float* bufferR, size_t length);
    static LoadIRCallbackFn LoadIRCallback;
    
private:
    USBHostHandle* usbh_;
    bool mounted_;
    
    // WAV file header structure
    struct WAVHeader {
        char riff[4];           // "RIFF"
        uint32_t chunkSize;     // File size - 8
        char wave[4];           // "WAVE"
        char fmt[4];            // "fmt "
        uint32_t fmtSize;       // Format chunk size
        uint16_t audioFormat;   // Audio format (1 = PCM)
        uint16_t numChannels;   // Number of channels
        uint32_t sampleRate;    // Sample rate
        uint32_t byteRate;      // Byte rate
        uint16_t blockAlign;    // Block align
        uint16_t bitsPerSample; // Bits per sample
        char data[4];           // "data"
        uint32_t dataSize;      // Data size
    };
};

// Initialize static member
IRLoader::LoadIRCallbackFn IRLoader::LoadIRCallback = nullptr;
