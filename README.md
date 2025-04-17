# Echo Bridge - Convolution Reverb for Daisy Seed

This is a convolution reverb implementation for the Daisy Seed board, designed to be used in a guitar pedal format with the Cleveland Music Co. Hothouse form factor.

## Features

- Convolution reverb using FFT-based processing
- USB support for loading impulse response files
- OLED display for parameter visualization
- Stereo input/output support
- Adjustable parameters:
  - Dry/wet mix
  - Predelay
  - IR length
  - Low cut filter
  - High cut filter
  - Stereo width

## Hardware Requirements

- Daisy Seed board
- Cleveland Music Co. Hothouse pedal enclosure
- USB port for loading impulse response files
- Audio input/output jacks
- Potentiometers for parameter control
- Footswitches for bypass and other functions

## Installation

1. Connect your Daisy Seed board to your computer via USB
2. Use the Daisy Web Programmer (https://electro-smith.github.io/Programmer/) or the Daisy Seed CLI tools to flash the `EchoBridge.bin` file to your board
3. Alternatively, you can build the project from source using the provided Makefile

## Building from Source

1. Clone the libDaisy and DaisySP repositories:
   ```
   git clone https://github.com/electro-smith/libDaisy.git
   git clone https://github.com/electro-smith/DaisySP.git
   ```

2. Build the libraries:
   ```
   cd libDaisy
   make
   cd ../DaisySP
   make
   ```

3. Update the Makefile to point to your libDaisy and DaisySP directories if needed

4. Build the project:
   ```
   make
   ```

5. Flash the resulting `build/EchoBridge.bin` file to your Daisy Seed board

## Usage

1. Power on the pedal
2. Connect a USB drive with impulse response files (WAV format)
3. Use the footswitches to control:
   - Footswitch 1: Toggle bypass
   - Footswitch 2: Toggle freeze mode
   - Long press Footswitch 1: Reset parameters to default
   - Long press Footswitch 2: Load IR files from USB

4. Use the knobs to adjust parameters:
   - Knob 1: Dry/wet mix
   - Knob 2: Predelay (0-500ms)
   - Knob 3: IR length factor
   - Knob 4: Filter control (first half controls low cut, second half controls high cut)

## Impulse Response Files

The pedal looks for the following files on the USB drive:
- `/ir_mono.wav` - Used for mono processing
- `/ir_left.wav` and `/ir_right.wav` - Used for stereo processing when stereo input is detected

## Changes Made to Fix Implementation

The following issues were fixed in this implementation:

1. **ShyFFT API Usage**: 
   - Fixed template instantiation with proper parameters
   - Updated method calls from Forward() to Direct()
   - Fixed Inverse() method parameter types

2. **OLED Display Driver**: 
   - Updated to use the correct SSD130x4WireSpi128x64Driver class
   - Fixed constructor parameters and initialization

3. **Pin Access Methods**: 
   - Changed hw.GetPin() to hw.seed.GetPin() for proper pin access

4. **SVF Filter API**: 
   - Updated filter processing to use the correct High() and Low() methods
   - Removed SetMode() calls which don't exist in the current API

5. **Memory Optimization**: 
   - Reduced FFT_SIZE from 1024 to 512
   - Reduced MAX_IR_LENGTH to 4096
   - Optimized buffer allocations to fit within memory constraints

6. **USB Host API**: 
   - Updated the IRLoader class to use the correct USBHostHandle API
   - Fixed file system access methods

7. **FatFs Integration**: 
   - Added missing unicode.c file with required functions
   - Fixed character conversion functions for file system access

## Memory Usage

The implementation uses:
- 89.83% of FLASH (117736 bytes of 128 KB)
- 46.28% of SRAM (242636 bytes of 512 KB)

This is within the constraints of the Daisy Seed board.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Electrosmith for the Daisy platform
- Cleveland Music Co. for the Hothouse pedal format
