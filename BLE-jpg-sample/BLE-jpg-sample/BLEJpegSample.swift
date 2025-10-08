//
//  BLEJpegSample.swift
//  BLE JPEG central helper for M5CoreS3 sample
//

import Foundation
import CoreBluetooth
import Combine
#if canImport(UIKit)
import UIKit
#endif

/// Handles discovery of the M5CoreS3 BLE peripheral and downloads JPEG payloads.
public final class BLEJpegSample: NSObject, ObservableObject {

  public enum ConnectionState: String {
    case idle
    case bluetoothUnavailable
    case scanning
    case connecting
    case connected
    case discovering
    case subscribed
    case error
  }

  private let serviceUUID = CBUUID(string: "FE55")
  private let controlCharacteristicUUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a8")
  private let jpegCharacteristicUUID = CBUUID(string: "c9d1cba2-1f32-4fb0-b6bc-9b73c7d8b4e2")

  private lazy var central: CBCentralManager = {
    CBCentralManager(delegate: self, queue: centralQueue)
  }()

  @Published public private(set) var connectionState: ConnectionState = .idle
  @Published public private(set) var statusMessage: String = "Waiting"
  @Published public private(set) var lastJpegData: Data?
#if canImport(UIKit)
  @Published public private(set) var lastImage: UIImage?
#endif

  private let centralQueue: DispatchQueue
  private var targetPeripheral: CBPeripheral?
  private var controlCharacteristic: CBCharacteristic?
  private var jpegCharacteristic: CBCharacteristic?

  private var shouldScan = false
  private var transferBuffer = Data()
  private var headerBuffer = Data()
  private var expectedImageLength: Int?
  private var chunkTimeoutWorkItem: DispatchWorkItem?
  private let chunkTimeout: TimeInterval = 0.35

  public init(queue: DispatchQueue = DispatchQueue(label: "ble.jpeg.central")) {
    self.centralQueue = queue
    super.init()
    _ = central  // Force lazy init.
  }

  deinit {
    stop()
  }

  // MARK: - Public API

  public func start() {
    centralQueue.async {
      self.shouldScan = true
      self.statusMessage = "Starting"
      self.updateState(.idle)
      if self.central.state == .poweredOn {
        self.beginScanning()
      }
    }
  }

  public func stop() {
    centralQueue.async {
      self.shouldScan = false
      self.cancelCurrentConnection()
      if self.central.isScanning {
        self.central.stopScan()
      }
      self.updateState(.idle)
      self.statusMessage = "Stopped"
    }
  }

  /// Requests the peripheral to send the JPEG via notifications.
  public func requestJpegTransfer() {
    centralQueue.async {
      guard let peripheral = self.targetPeripheral,
            let control = self.controlCharacteristic,
            peripheral.state == .connected else {
        self.updateStatus("Peripheral unavailable for JPEG request")
        return
      }
      self.resetTransferState()
      let payload = Data("SEND_JPEG".utf8)
      peripheral.writeValue(payload, for: control, type: .withResponse)
      self.updateStatus("Requested SEND_JPEG")
    }
  }

  // MARK: - Private helpers

  private func beginScanning() {
    guard shouldScan else { return }
    updateState(.scanning)
    updateStatus("Scanning for FE55")
    central.scanForPeripherals(withServices: [serviceUUID],
                               options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
  }

  private func cancelCurrentConnection() {
    if let peripheral = targetPeripheral {
      central.cancelPeripheralConnection(peripheral)
    }
    targetPeripheral = nil
    controlCharacteristic = nil
    jpegCharacteristic = nil
    resetTransferState()
  }

  private func updateState(_ newState: ConnectionState) {
    DispatchQueue.main.async {
      self.connectionState = newState
    }
  }

  private func updateStatus(_ message: String) {
    print(message)
    DispatchQueue.main.async {
      self.statusMessage = message
    }
  }

  private func resetTransferState() {
    headerBuffer.removeAll(keepingCapacity: false)
    transferBuffer.removeAll(keepingCapacity: false)
    expectedImageLength = nil
    chunkTimeoutWorkItem?.cancel()
    chunkTimeoutWorkItem = nil
  }

  private func finalizeTransfer() {
    guard let expected = expectedImageLength,
          transferBuffer.count >= expected else {
      print("[BLEJpegSample] finalizeTransfer skipped; buffer=\(transferBuffer.count) expected=\(expectedImageLength ?? -1)")
      return
    }
    let jpegSlice = transferBuffer.prefix(expected)
    let jpegData = Data(jpegSlice)
    let hasValidHeader = jpegData.starts(with: [0xFF, 0xD8])
    let hasValidFooter = jpegData.suffix(2) == Data([0xFF, 0xD9])

    chunkTimeoutWorkItem?.cancel()
    chunkTimeoutWorkItem = nil
    headerBuffer.removeAll(keepingCapacity: false)
    transferBuffer.removeAll(keepingCapacity: false)
    expectedImageLength = nil

    guard hasValidHeader, hasValidFooter else {
      print("[BLEJpegSample] JPEG markers invalid header=\(hasValidHeader) footer=\(hasValidFooter)")
      updateStatus("JPEG invalid (missing markers)")
      DispatchQueue.main.async {
        self.lastJpegData = nil
#if canImport(UIKit)
        self.lastImage = nil
#endif
      }
      return
    }

    DispatchQueue.main.async {
      self.lastJpegData = jpegData
#if canImport(UIKit)
      self.lastImage = UIImage(data: jpegData)
#endif
      self.statusMessage = "JPEG received (\(jpegData.count) bytes)"
    }
    print("[BLEJpegSample] JPEG transfer complete (\(jpegData.count) bytes)")
  }

  private func scheduleTransferCompletion() {
    chunkTimeoutWorkItem?.cancel()
    let workItem = DispatchWorkItem { [weak self] in
      guard let self = self else { return }
      print("[BLEJpegSample] Chunk timeout fired, finalizing transfer")
      self.finalizeTransfer()
    }
    chunkTimeoutWorkItem = workItem
    centralQueue.asyncAfter(deadline: .now() + chunkTimeout, execute: workItem)
  }

  private func handleJpegNotification(_ data: Data) {
    print("[BLEJpegSample] Received notification with \(data.count) bytes")
    if expectedImageLength == nil {
      headerBuffer.append(data)
      let requiredHeaderBytes = 8
      if headerBuffer.count < requiredHeaderBytes {
        updateStatus("Receiving header (\(headerBuffer.count)/\(requiredHeaderBytes))")
        return
      }

      let header = headerBuffer.prefix(requiredHeaderBytes)
      let signature = Data(header.prefix(4))
      guard signature == Data("JPEG".utf8) else {
        updateStatus("Unexpected JPEG header")
        resetTransferState()
        return
      }

      let lengthBytes = header.suffix(4)
      var length: UInt32 = 0
      for byte in lengthBytes {
        length = (length << 8) | UInt32(byte)
      }
      let expectedCount = Int(length)
      expectedImageLength = expectedCount
      let signatureString = String(data: signature, encoding: .ascii) ?? "JPEG"
      print("[BLEJpegSample] Header signature=\(signatureString) expected=\(expectedCount) bytes")
      updateStatus("Header received: \(expectedCount) bytes")

      let remainder = headerBuffer.dropFirst(requiredHeaderBytes)
      headerBuffer.removeAll(keepingCapacity: false)
      if !remainder.isEmpty {
        transferBuffer.append(contentsOf: remainder)
        print("[BLEJpegSample] Appended remainder \(remainder.count) bytes")
      }

      if let expected = expectedImageLength {
        if transferBuffer.count >= expected {
          finalizeTransfer()
        } else {
          let received = transferBuffer.count
          print("[BLEJpegSample] After header remainder -> \(received)/\(expected) bytes")
          updateStatus("Receiving JPEG (\(received)/\(expected))")
          scheduleTransferCompletion()
        }
      }
      return
    }

    transferBuffer.append(data)
    guard let expected = expectedImageLength else {
      scheduleTransferCompletion()
      return
    }

    let received = transferBuffer.count
    print("[BLEJpegSample] Chunk received, total \(received)/\(expected)")
    if received >= expected {
      finalizeTransfer()
    } else {
      updateStatus("Receiving JPEG (\(received)/\(expected))")
      scheduleTransferCompletion()
    }
  }
}

// MARK: - CBCentralManagerDelegate

extension BLEJpegSample: CBCentralManagerDelegate {
  public func centralManagerDidUpdateState(_ central: CBCentralManager) {
    switch central.state {
    case .unknown, .resetting:
      updateState(.idle)
      updateStatus("Bluetooth resetting")
    case .unsupported, .unauthorized:
      updateState(.bluetoothUnavailable)
      updateStatus("Bluetooth unavailable")
    case .poweredOff:
      updateState(.bluetoothUnavailable)
      updateStatus("Bluetooth powered off")
    case .poweredOn:
      updateState(.idle)
      updateStatus("Bluetooth ready")
      beginScanning()
    @unknown default:
      updateState(.error)
      updateStatus("Unknown BT state")
    }
  }

  public func centralManager(_ central: CBCentralManager,
                             didDiscover peripheral: CBPeripheral,
                             advertisementData: [String: Any],
                             rssi RSSI: NSNumber) {
    updateStatus("Found \(peripheral.name ?? "M5CoreS3")")
    shouldScan = false
    central.stopScan()
    updateState(.connecting)
    targetPeripheral = peripheral
    peripheral.delegate = self
    central.connect(peripheral, options: nil)
  }

  public func centralManager(_ central: CBCentralManager,
                             didConnect peripheral: CBPeripheral) {
    updateState(.connected)
    updateStatus("Discovering services")
    peripheral.delegate = self
    peripheral.discoverServices([serviceUUID])
  }

  public func centralManager(_ central: CBCentralManager,
                             didFailToConnect peripheral: CBPeripheral,
                             error: Error?) {
    updateState(.error)
    updateStatus("Connect failed: \(error?.localizedDescription ?? "unknown")")
    targetPeripheral = nil
    resetTransferState()
    if shouldScan {
      beginScanning()
    }
  }

  public func centralManager(_ central: CBCentralManager,
                             didDisconnectPeripheral peripheral: CBPeripheral,
                             error: Error?) {
    updateStatus("Disconnected")
    updateState(.idle)
    targetPeripheral = nil
    controlCharacteristic = nil
    jpegCharacteristic = nil
    resetTransferState()
    if shouldScan {
      beginScanning()
    }
  }
}

// MARK: - CBPeripheralDelegate

extension BLEJpegSample: CBPeripheralDelegate {
  public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
    if let error = error {
      updateState(.error)
      updateStatus("Service discovery failed: \(error.localizedDescription)")
      return
    }
    guard let services = peripheral.services else { return }
    for service in services where service.uuid == serviceUUID {
      updateState(.discovering)
      updateStatus("Discovering characteristics")
      peripheral.discoverCharacteristics([controlCharacteristicUUID, jpegCharacteristicUUID], for: service)
      return
    }
    updateState(.error)
    updateStatus("Service FE55 not found")
  }

  public func peripheral(_ peripheral: CBPeripheral,
                         didDiscoverCharacteristicsFor service: CBService,
                         error: Error?) {
    if let error = error {
      updateState(.error)
      updateStatus("Characteristic discovery failed: \(error.localizedDescription)")
      return
    }
    guard let chars = service.characteristics else { return }
    for characteristic in chars {
      switch characteristic.uuid {
      case controlCharacteristicUUID:
        controlCharacteristic = characteristic
      case jpegCharacteristicUUID:
        jpegCharacteristic = characteristic
        peripheral.setNotifyValue(true, for: characteristic)
        updateStatus("Subscribing to JPEG notifications")
        print("[BLEJpegSample] CCCD subscription state: notify=\(characteristic.properties.contains(.notify)) isNotifying=\(characteristic.isNotifying)")
      default:
        continue
      }
    }
    
    if controlCharacteristic != nil && jpegCharacteristic != nil {
      updateState(.subscribed)
      updateStatus("Ready for JPEG transfer")
  }

  public func peripheral(_ peripheral: CBPeripheral,
                         didUpdateNotificationStateFor characteristic: CBCharacteristic,
                         error: Error?) {
    if let error = error {
      updateStatus("Notify failed: \(error.localizedDescription)")
      print("[BLEJpegSample] CCCD update error: \(error.localizedDescription)")
      return
    }
    if characteristic.uuid == jpegCharacteristicUUID {
      if characteristic.isNotifying {
        updateState(.subscribed)
        updateStatus("JPEG notifications active")
        print("[BLEJpegSample] JPEG notifications are now active")
      } else {
        updateStatus("JPEG notifications stopped")
        print("[BLEJpegSample] JPEG notifications stopped")
      }
    } else if characteristic.isNotifying {
      updateStatus("Notify active for \(characteristic.uuid.uuidString)")
      print("[BLEJpegSample] Notify active for \(characteristic.uuid.uuidString)")
    } else {
      updateStatus("Notify stopped for \(characteristic.uuid.uuidString)")
      print("[BLEJpegSample] Notify stopped for \(characteristic.uuid.uuidString)")
    }
  }

  public func peripheral(_ peripheral: CBPeripheral,
                         didUpdateValueFor characteristic: CBCharacteristic,
                         error: Error?) {
    if let error = error {
      updateStatus("Update error: \(error.localizedDescription)")
      return
    }
    guard let data = characteristic.value else { return }
    if characteristic.uuid == jpegCharacteristicUUID {
      handleJpegNotification(data)
    }
  }
}
