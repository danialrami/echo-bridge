# Technical Implementation Details

## Partitioned Convolution Algorithm

Echo Bridge uses a partitioned convolution algorithm to achieve low latency while maintaining high audio quality. This approach divides the impulse response into two parts:

1. **Early reflections**: Processed with a small FFT size (64 samples) for minimal latency
   - At 48kHz, this introduces only 1.33ms of latency
   - Captures the immediate character of the reverb

2. **Late reverb tail**: Processed with a larger FFT size (1024 samples) for computational efficiency
   - Provides the rich, detailed reverb tail
   - More efficient processing for longer impulse responses

### How Partitioned Convolution Works

Traditional convolution reverb processes the entire impulse response with a single FFT size, which creates a tradeoff between latency and computational efficiency. Larger FFT sizes are more efficient but introduce more latency.

Partitioned convolution solves this problem by:

1. Dividing the impulse response into segments
2. Processing the early part with small FFTs for low latency
3. Processing the later parts with larger FFTs for efficiency
4. Combining the results for the final output

This provides the best of both worlds: the immediate response needed for musical performance and the rich, detailed reverb tails that make convolution reverb sound natural.

## Memory Optimization

The Daisy Seed has limited internal memory (128KB FLASH, 512KB SRAM), but includes 64MB of external SDRAM. Echo Bridge uses this memory architecture efficiently:

1. **SDRAM Usage**: Large buffers are stored in external SDRAM using the `DSY_SDRAM_BSS` attribute
   - Late partition FFT buffers
   - Predelay buffers
   - This allows for longer impulse responses without running out of internal memory

2. **Memory Footprint**:
   - FLASH usage: ~92% (118KB of 128KB)
   - SRAM usage: ~54% (280KB of 512KB)
   - SDRAM usage: <1% (250KB of 64MB)

3. **Buffer Size Optimization**:
   - Early partition: 64-point FFT (small enough for internal memory)
   - Late partition: 1024-point FFT (stored in SDRAM)
   - Maximum IR length: 4096 samples (can be extended if needed)

## USB Host Implementation

Echo Bridge includes a USB host implementation that allows loading impulse responses from a USB drive:

1. **File System**: Uses FatFs to read WAV files from USB drives
2. **Format Support**:
   - 16-bit, 24-bit, and 32-bit float WAV files
   - Mono and stereo files
   - Automatic normalization of impulse responses

3. **File Naming Convention**:
   - Mono reverb: `ir_mono.wav`
   - Stereo reverb: `ir_left.wav` and `ir_right.wav`

## Audio Processing Pipeline

The audio processing pipeline includes:

1. **Input Stage**:
   - Stereo detection
   - Predelay buffer (up to 500ms)

2. **Convolution Stage**:
   - Early partition processing (64-point FFT)
   - Late partition processing (1024-point FFT)
   - Overlap-add for continuous output

3. **Output Stage**:
   - Low/high cut filtering
   - Stereo width control
   - Dry/wet mixing

## Hardware Interface

The code is designed for the Cleveland Music Co. Hothouse pedal form factor:

1. **Controls**:
   - 6 knobs for parameter adjustment
   - 2 footswitches with press and long-press functionality
   - 3 toggle switches for additional options
   - 4 LEDs for status indication

2. **Display**:
   - SSD1309 OLED display via SPI
   - Shows parameter values and status information

3. **Audio**:
   - 48kHz sample rate
   - 24-bit audio processing
   - Stereo input/output

## Build System

The project uses a Makefile-based build system compatible with the Daisy ecosystem:

1. **Dependencies**:
   - libDaisy: Hardware abstraction layer for Daisy Seed
   - DaisySP: Signal processing library

2. **Output Files**:
   - EchoBridge.bin: Binary file for flashing to Daisy Seed
   - EchoBridge.elf: ELF file with debug information
   - EchoBridge.map: Memory map file

3. **Flash Commands**:
   - `make program-dfu`: Flash using DFU mode
   - `make program`: Flash using ST-Link
