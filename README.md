# Echo Bridge Convolution Reverb

Echo Bridge is a high-quality convolution reverb effect for the Daisy Seed platform, designed to provide studio-quality reverb in a compact guitar pedal form factor.

## Features

- **Partitioned Convolution Algorithm**: Low-latency processing with high audio quality
  - Early reflections processed with small FFT (64 points) for minimal latency
  - Late reverb tail processed with larger FFT (1024 points) for computational efficiency
  
- **USB Host Support**: Load custom impulse responses from USB drive
  - Supports mono and stereo WAV files
  - Automatically normalizes impulse responses
  
- **Comprehensive Controls**:
  - Dry/Wet mix
  - Predelay (0-500ms)
  - IR length control
  - Low/High cut filters
  - Stereo width control
  
- **Visual Feedback**:
  - LED indicators for bypass and freeze status
  
- **Memory Optimized**:
  - Uses external SDRAM for large buffers
  - Efficient memory usage for long impulse responses

## Hardware Requirements

- Daisy Seed board (65MB version recommended)
- Cleveland Music Co. Hothouse pedal enclosure or compatible hardware
- USB host port for loading impulse responses

## Usage

### Basic Operation

1. **Freeze Mode**: Press footswitch 1 to toggle freeze mode (only outputs reverb tail)
2. **Bypass Mode**: Press footswitch 2 to toggle bypass mode
3. **Load IR from USB**: Long press footswitch 1 to load impulse response from USB drive
   - LED 1 will blink during loading and indicate success/failure
4. **Reset Parameters**: Long press footswitch 2 to reset all parameters to default values

### Knob Controls

- **Knob 1**: Dry/Wet mix (0-100%)
- **Knob 2**: Predelay (0-500ms)
- **Knob 3**: IR length (0-100%)
- **Knob 4**: Filter control
  - First half (0-50%): Low cut frequency (20Hz-1000Hz)
  - Second half (50-100%): High cut frequency (20kHz-1000Hz)
- **Knob 5**: Stereo width (0-200%)
- **Knob 6**: Reserved for future use

### LED Indicators

- **LED 1**: Freeze status (on when freeze is active)
  - Temporarily blinks when loading IR files
- **LED 2**: Bypass status (off when bypassed, on when active)

### Loading Impulse Responses

Place WAV files on a USB drive and connect it to the pedal:

- For mono reverb: Save as `ir_mono.wav`
- For stereo reverb: Save left and right channels as `ir_left.wav` and `ir_right.wav`

Supported formats:
- 16-bit, 24-bit, or 32-bit float WAV files
- Mono or stereo files
- Any sample rate (will be resampled as needed)

## Building from Source

### Prerequisites

- Daisy toolchain (arm-none-eabi-gcc)
- libDaisy and DaisySP libraries

### Build Instructions

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

3. Build Echo Bridge:
   ```
   cd echo-bridge_v0.99/src
   make
   ```

4. Flash to Daisy Seed:
   ```
   make program-dfu
   ```

## Technical Details

Echo Bridge uses a partitioned convolution algorithm to achieve low latency while maintaining high audio quality. The impulse response is divided into two parts:

1. **Early reflections**: Processed with a small FFT size (64 samples) for minimal latency
2. **Late reverb tail**: Processed with a larger FFT size (1024 samples) for computational efficiency

This approach provides the immediate response needed for musical performance while still allowing for rich, detailed reverb tails.

Memory usage is optimized by storing large buffers in the external SDRAM, allowing for longer impulse responses without running out of internal memory. This implementation is specifically optimized for the 65MB version of the Daisy Seed which provides additional SDRAM.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- Electrosmith for the Daisy platform and libraries
- Cleveland Music Co. for the Hothouse pedal design
