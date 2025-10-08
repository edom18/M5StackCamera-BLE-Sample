# BLE JPEG Central Usage

This guide shows how to consume the `BLEJpegSample` helper from a SwiftUI view to receive the JPEG stream emitted by the M5CoreS3 sketch in this repository.

## Prerequisites
- Add `BLEJpegSample.swift` to your iOS project and link against `CoreBluetooth`.
- Update `Info.plist` with `NSBluetoothAlwaysUsageDescription` (and `NSBluetoothPeripheralUsageDescription` if supporting iOS < 13).
- Ensure the M5CoreS3 is running the updated firmware and advertising the `FE55` service.

## SwiftUI Integration Example
```swift
import SwiftUI

struct ContentView: View {
    @StateObject private var ble = BLEJpegSample()

    var body: some View {
        VStack(spacing: 16) {
            Text("BLE Status: \(ble.connectionState.rawValue)")
            Text(ble.statusMessage)
                .font(.footnote)
                .foregroundStyle(.secondary)

            if let image = ble.lastImage {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(maxHeight: 240)
                    .border(.gray)
            } else {
                Rectangle()
                    .fill(.gray.opacity(0.1))
                    .frame(height: 240)
                    .overlay(Text("No image yet").foregroundStyle(.secondary))
            }

            HStack {
                Button("Start Scan") { ble.start() }
                Button("Stop") { ble.stop() }
                Button("Request JPEG") { ble.requestJpegTransfer() }
                    .disabled(ble.connectionState != .subscribed)
            }
        }
        .padding()
        .onAppear { ble.start() }
        .onDisappear { ble.stop() }
    }
}
```

The view privately owns the helper (`@StateObject private var ble = BLEJpegSample()`) and drives scanning on appear. The helper keeps track of connection state, status messages, and the latest JPEG. The `Request JPEG` button writes `"SEND_JPEG"` to the control characteristic; once the peripheral finishes streaming notifications, `lastImage` updates automatically.

## Notes
- `BLEJpegSample` automatically resubscribes if the peripheral disconnects and scanning remains enabled.
- The helper buffers JPEG chunks until notifications stop for ~350 ms, then exposes the assembled data and `UIImage`.
- If you need platform-neutral image handling (e.g., macOS Catalyst), observe `lastJpegData` instead of `lastImage`.
