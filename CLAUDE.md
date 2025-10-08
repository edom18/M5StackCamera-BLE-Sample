# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a BLE (Bluetooth Low Energy) sample project featuring camera image transfer between an M5Stack CoreS3 device (peripheral) and iOS devices (central). The Arduino sketch runs on the M5CoreS3 and captures camera frames, encoding them as JPEG and transmitting via BLE notifications. The Swift helper class manages the iOS central role for receiving these images.

## Architecture

- **BLE-sample2.ino**: Arduino sketch for M5Stack CoreS3 that:
  - Sets up BLE server with service UUID `FE55`
  - Captures camera frames and converts to JPEG
  - Streams JPEG data via BLE notifications when iOS client sends "SEND_JPEG" command
  - Handles chunked data transfer with MTU negotiation

- **BLEJpegSample.swift**: iOS/SwiftUI helper class that:
  - Discovers and connects to M5CoreS3 peripheral
  - Subscribes to JPEG notifications
  - Reassembles chunked JPEG data
  - Provides `@Published` properties for SwiftUI integration

- **python-demo/**: Utility scripts for Base64 encoding/decoding test images

## Build Commands

### Arduino (M5Stack CoreS3)
```bash
# Detect board and port
arduino-cli board list

# Compile sketch
arduino-cli compile --fqbn m5stack:esp32:m5stack_coreS3 BLE-sample2.ino

# Upload firmware (replace COM port as needed)
arduino-cli upload -p /dev/ttyACM0 --fqbn m5stack:esp32:m5stack_coreS3 BLE-sample2.ino

# Monitor serial output
arduino-cli monitor -p /dev/ttyACM0 --config 115200
```

### iOS Integration
- Add `BLEJpegSample.swift` to Xcode project
- Update `Info.plist` with `NSBluetoothAlwaysUsageDescription`
- See README.md for SwiftUI usage example

## Key Implementation Details

### BLE Communication Protocol
- Service UUID: `FE55`
- Control Characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8` (read/write/notify)
- JPEG Characteristic: `c9d1cba2-1f32-4fb0-b6bc-9b73c7d8b4e2` (notify/indicate)
- JPEG transfer initiated by writing "SEND_JPEG" to control characteristic
- Data sent with 8-byte header: "JPEG" + 4-byte size (big-endian)
- Chunked transfer with MTU-based payload sizing (max 240 bytes)

### Testing Workflow
1. Upload firmware to M5CoreS3 and monitor serial output
2. Run iOS app with `BLEJpegSample` helper
3. Press power button on M5CoreS3 to trigger notifications
4. Send "SEND_JPEG" command from iOS to receive camera image
5. Verify LCD messages on device and serial logs for transfer status