# Echo Bridge Stereo Convolution Reverb

## Description

A stereo-compatible convolution reverb effect that recreates the unique acoustic characteristics of Echo Bridge in San Antonio. This pedal allows sound designers to capture the sound of the iconic Echo Bridge with full stereo processing, while remaining compatible with mono inputs.

### Key Features

- **True Stereo Processing**: Properly handles stereo inputs and creates a rich stereo field
- **Automatic Mono/Stereo Detection**: Detects if the input is mono or stereo and processes accordingly
- **Stereo Field Control**: Adjust the stereo width from mono to exaggerated stereo
- **USB IR Loading**: Load your own Impulse Responses via USB, including stereo IR pairs
- **Freeze Function**: Capture and sustain the reverb tail indefinitely
- **Enhanced Error Handling**: Robust USB/file operations with informative error messages
- **OLED Parameter Display**: Real-time visualization of all control parameters
- **Memory-Efficient FFT**: Uses ShyFFT for optimized convolution on embedded hardware

### Controls

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Dry/Wet Mix | Controls the balance between dry and wet signal |
| KNOB 2 | Pre-delay | Adds space before the reverb (0-500ms) |
| KNOB 3 | IR Length | Trims the impulse response length (10-100%) |
| KNOB 4 | Low Cut | Removes low frequencies from the reverb (20-2000Hz) |
| KNOB 5 | High Cut | Removes high frequencies from the reverb (1000-20000Hz) |
| KNOB 6 | Output Level | Controls the overall output level |
| SWITCH 1 | IR Mode | **UP** - Full Bridge<br/>**MIDDLE** - Short Echo<br/>**DOWN** - Long Decay |
| SWITCH 2 | Freeze | **UP** - Normal<br/>**MIDDLE** - Normal<br/>**DOWN** - Freeze reverb |
| SWITCH 3 | Stereo Width | **UP** - Narrow stereo (0.5)<br/>**MIDDLE** - Normal stereo (1.0)<br/>**DOWN** - Wide stereo (1.5) |
| FOOTSWITCH 1 | Freeze/Hold | Momentary freeze of the reverb tail |
| FOOTSWITCH 2 | Bypass | The bypassed signal is buffered |

## Features

| FEATURE | DESCRIPTION |
|-|-|
| Double Press FOOTSWITCH_1 | Cycles through IR modes |
| Long Press FOOTSWITCH_1 | Enters bootloader mode (after LED flashing sequence) |

## LED Indicators

| LED | FUNCTION |
|-|-|
| LED 1 | Freeze status |
| LED 2 | Bypass status |
| LED 3 | USB status |
| LED 4 | Stereo input detection |

## USB IR Loading

The pedal supports loading custom impulse responses via USB drive. You can load:

1. **Mono IRs**: Single `.WAV` files that will be processed to create a stereo field
2. **Stereo IR Pairs**: Left and right channel `.WAV` files for true stereo processing

### Supported Filename Conventions

| IR Type | Filenames |
|-|-|
| Mono IRs | `ECHOBR_FULL.WAV`, `ECHOBR_SHORT.WAV`, `ECHOBR_LONG.WAV` |
| Stereo Pairs | `ECHOBR_FULL_L.WAV`/`ECHOBR_FULL_R.WAV`, <br/>`ECHOBR_SHORT_L.WAV`/`ECHOBR_SHORT_R.WAV`, <br/>`ECHOBR_LONG_L.WAV`/`ECHOBR_LONG_R.WAV` |

### WAV File Requirements

- Sample rates: 44.1kHz or 48kHz
- Bit depths: 16-bit, 24-bit, or 32-bit float
- Length: Up to 3 seconds (144,000 samples at 48kHz)

### Error Handling

The pedal provides feedback on the OLED display when IR loading operations fail, with specific error messages for:
- USB drive not mounted
- File not found
- Invalid or corrupted WAV format
- Read errors
- Unsupported bit depths or formats

## Installation

1. Copy the binary file to your Daisy Seed using the web programmer or Daisy Seed Tool.
2. Default IRs are embedded in the firmware, but you can enhance the experience by loading your own IRs via USB.

## Development Notes

- The IR was recorded at Echo Bridge in San Antonio using a matched pair of omnidirectional microphones for full stereo capture.
- Processing uses FFT-based convolution with separate processors for left and right channels.
- Mid-side processing is used for stereo width control.
- Automatic stereo detection analyzes channel differences to determine if the input is mono or stereo.
- Memory-efficient implementation using ShyFFT, an optimized FFT library specifically designed for embedded audio applications.
- 1024-point FFT with overlap-add processing for efficient real-time convolution.
- Full 48kHz fidelity with support for IRs up to 3 seconds in length.
- Enhanced stereo capabilities including true stereo IR pairs or mono-to-stereo expansion.
- Display abstraction layer for improved code maintainability.
- Enhanced error handling for robust operation.
- Footswitch callback system for detecting normal, double, and long presses.

## Sound Design Tips

- For sound design with DAWs, feed the reverb with different inputs on left and right channels to maximize the stereo field.
- Try freezing the reverb with one sound, then switching to a different sound while frozen to create layered textures.
- The stereo width control can be used to collapse a stereo reverb to mono or exaggerate stereo differences.
- For cinematic sound design, try using a short pre-delay (20-30ms) and a wide stereo setting.
- Use the low and high cut filters to shape the spectral characteristics of the reverb tail.
- For more experimental sounds, try extreme settings like very short IR length combined with freeze functionality.
- Double-press FOOTSWITCH_1 to quickly cycle through IR modes during performance.
- Hold the left footswitch (FOOTSWITCH_1) for 2 seconds to enter bootloader mode for firmware updates.

## Building From Source

This project requires the Daisy ecosystem libraries:
- libDaisy: Hardware abstraction layer for the Daisy platform
- DaisySP: DSP library with various audio processing modules

It also depends on the Hothouse form factor headers, which handle the hardware interface for the specific pedal layout.

The project also uses:
- ShyFFT: An efficient FFT implementation designed for embedded audio applications by Emilie Gillet.

Build with the standard Daisy toolchain:
```
make clean
make
```

To upload to a connected Daisy Seed in bootloader mode:
```
make program
```