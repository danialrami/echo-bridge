#pragma once

#include "daisy.h"
#include <string.h>

using namespace daisy;

// Enhanced error handling
enum class IRLoadError
{
    NONE,
    NOT_MOUNTED,
    FILE_NOT_FOUND,
    INVALID_FORMAT,
    READ_ERROR,
    UNSUPPORTED_FORMAT
};

// Class for loading IRs from USB drive
class IRLoader
{
public:
    IRLoader() : mounted_(false), fileLoaded_(false), lastError_(IRLoadError::NONE) {}
    ~IRLoader() {}

    void Init()
    {
        // Initialize the USB Host for Mass Storage
        usbh_.Init(UsbHandle::FS_INTERNAL);
    }

    // Starts the USB processing
    void StartUSB()
    {
        usbh_.Start();
    }

    // Call this regularly to process USB events
    void Process()
    {
        usbh_.Process();

        // If filesystem isn't mounted and a device is connected, try to mount
        if(!mounted_ && usbh_.GetState() == UsbHandle::HostState::CONNECTED)
        {
            fatfs_.Init(FatFSInterface::Config::MEDIA_USB);
            
            // Try to mount the file system
            if(f_mount(&fatfs_.GetSDFileSystem(), "/", 1) == FR_OK)
            {
                mounted_ = true;
            }
        }

        // If we disconnected, mark filesystem as unmounted
        if(usbh_.GetState() != UsbHandle::HostState::CONNECTED)
        {
            mounted_ = false;
        }
    }

    // Enhanced LoadWavToIR with better error reporting
    bool LoadWavToIR(const char* filename, float* buffer, size_t* pLength, size_t maxLength)
    {
        lastError_ = IRLoadError::NONE;
        
        if(!mounted_)
        {
            lastError_ = IRLoadError::NOT_MOUNTED;
            return false;
        }

        FIL file;
        FRESULT result = f_open(&file, filename, FA_READ);
        if(result != FR_OK)
        {
            lastError_ = IRLoadError::FILE_NOT_FOUND;
            return false;
        }

        // Read WAV header (44 bytes for standard WAV)
        uint8_t header[44];
        UINT bytesRead;
        result = f_read(&file, header, 44, &bytesRead);
        if(result != FR_OK || bytesRead != 44)
        {
            f_close(&file);
            lastError_ = IRLoadError::READ_ERROR;
            return false;
        }

        // Verify it's a WAV file (RIFF + WAVE)
        if(memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        {
            f_close(&file);
            lastError_ = IRLoadError::INVALID_FORMAT;
            return false;
        }

        // Get format information
        uint16_t numChannels = header[22] | (header[23] << 8);
        uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
        uint16_t bitsPerSample = header[34] | (header[35] << 8);

        // Find the data chunk
        uint8_t chunkHeader[8];
        while(true)
        {
            result = f_read(&file, chunkHeader, 8, &bytesRead);
            if(result != FR_OK || bytesRead != 8)
            {
                f_close(&file);
                lastError_ = IRLoadError::READ_ERROR;
                return false;
            }

            // Check if this is the data chunk
            if(memcmp(chunkHeader, "data", 4) == 0)
            {
                break;
            }

            // Skip this chunk
            uint32_t chunkSize = chunkHeader[4] | (chunkHeader[5] << 8) | (chunkHeader[6] << 16) | (chunkHeader[7] << 24);
            result = f_lseek(&file, f_tell(&file) + chunkSize);
            if(result != FR_OK)
            {
                f_close(&file);
                lastError_ = IRLoadError::READ_ERROR;
                return false;
            }
        }

        // Get data chunk size
        uint32_t dataSize = chunkHeader[4] | (chunkHeader[5] << 8) | (chunkHeader[6] << 16) | (chunkHeader[7] << 24);
        
        // Calculate samples in data chunk
        uint32_t numSamples = dataSize / (numChannels * (bitsPerSample / 8));
        
        // Limit to maxLength
        if(numSamples > maxLength)
        {
            numSamples = maxLength;
        }
        
        // Set the output length
        *pLength = numSamples;
        
        // Read samples based on bit depth
        if(bitsPerSample == 16)
        {
            int16_t sample;
            for(size_t i = 0; i < numSamples; i++)
            {
                // Read a 16-bit sample
                result = f_read(&file, &sample, 2, &bytesRead);
                if(result != FR_OK || bytesRead != 2)
                {
                    f_close(&file);
                    lastError_ = IRLoadError::READ_ERROR;
                    return false;
                }
                
                // Convert to float (-1.0 to 1.0)
                buffer[i] = sample / 32768.0f;
                
                // Skip other channels if stereo
                if(numChannels > 1)
                {
                    result = f_lseek(&file, f_tell(&file) + 2 * (numChannels - 1));
                    if(result != FR_OK)
                    {
                        f_close(&file);
                        lastError_ = IRLoadError::READ_ERROR;
                        return false;
                    }
                }
            }
        }
        else if(bitsPerSample == 24)
        {
            uint8_t sample[3];
            for(size_t i = 0; i < numSamples; i++)
            {
                // Read a 24-bit sample
                result = f_read(&file, sample, 3, &bytesRead);
                if(result != FR_OK || bytesRead != 3)
                {
                    f_close(&file);
                    lastError_ = IRLoadError::READ_ERROR;
                    return false;
                }
                
                // Convert 24-bit to signed int
                int32_t val = (sample[0] | (sample[1] << 8) | (sample[2] << 16));
                if(val & 0x800000) // Sign extend
                    val |= 0xFF000000;
                
                // Convert to float (-1.0 to 1.0)
                buffer[i] = val / 8388608.0f; // 2^23
                
                // Skip other channels if stereo
                if(numChannels > 1)
                {
                    result = f_lseek(&file, f_tell(&file) + 3 * (numChannels - 1));
                    if(result != FR_OK)
                    {
                        f_close(&file);
                        lastError_ = IRLoadError::READ_ERROR;
                        return false;
                    }
                }
            }
        }
        else if(bitsPerSample == 32)
        {
            // Assume 32-bit float
            for(size_t i = 0; i < numSamples; i++)
            {
                float sample;
                result = f_read(&file, &sample, 4, &bytesRead);
                if(result != FR_OK || bytesRead != 4)
                {
                    f_close(&file);
                    lastError_ = IRLoadError::READ_ERROR;
                    return false;
                }
                
                buffer[i] = sample;
                
                // Skip other channels if stereo
                if(numChannels > 1)
                {
                    result = f_lseek(&file, f_tell(&file) + 4 * (numChannels - 1));
                    if(result != FR_OK)
                    {
                        f_close(&file);
                        lastError_ = IRLoadError::READ_ERROR;
                        return false;
                    }
                }
            }
        }
        else
        {
            // Unsupported bit depth
            f_close(&file);
            lastError_ = IRLoadError::UNSUPPORTED_FORMAT;
            return false;
        }
        
        f_close(&file);
        fileLoaded_ = true;
        return true;
    }
    
    bool IsUSBMounted() { return mounted_; }
    bool IsFileLoaded() { return fileLoaded_; }
    IRLoadError GetLastError() { return lastError_; }
    
    const char* GetErrorMessage() 
    {
        switch(lastError_) 
        {
            case IRLoadError::NONE: return "No error";
            case IRLoadError::NOT_MOUNTED: return "USB not mounted";
            case IRLoadError::FILE_NOT_FOUND: return "File not found";
            case IRLoadError::INVALID_FORMAT: return "Invalid WAV format";
            case IRLoadError::READ_ERROR: return "File read error";
            case IRLoadError::UNSUPPORTED_FORMAT: return "Unsupported format";
            default: return "Unknown error";
        }
    }

private:
    UsbHandle usbh_;
    FatFSInterface fatfs_;
    bool mounted_;
    bool fileLoaded_;
    IRLoadError lastError_;
};