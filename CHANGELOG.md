# Changelog

All notable changes to the Echo Bridge firmware will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Partitioned convolution algorithm (64-point early FFT, 1024-point late FFT)
- USB host support for loading custom impulse responses
- Mono and stereo WAV file support
- Freeze mode, bypass mode, parameter reset controls
- Dry/wet mixing, predelay (0-500ms), IR length control
- Low/high cut filters with knob control
- Stereo width control (0-200%)
- OLED display via SSD1309 SPI
- SDRAM-optimized large buffer storage
- GitHub Actions CI workflow for firmware build verification
- CHANGELOG.md for version tracking

### Changed
- Initial public release versioning

## [0.9.9] - 2025-05-20

### Added
- Initial working implementation with partitioned convolution
- USB impulse response loading
- Hothouse pedal hardware support
- Comprehensive README and TECHNICAL.md documentation
- GPL-3.0 license