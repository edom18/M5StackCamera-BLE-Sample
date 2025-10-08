#include <M5CoreS3.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <esp_gatt_defs.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#define SERVICE_UUID                 "FE55"
#define CONTROL_CHARACTERISTIC_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define JPEG_CHARACTERISTIC_UUID     "c9d1cba2-1f32-4fb0-b6bc-9b73c7d8b4e2"
#define SERVER_NAME                  "M5CoreS3"


/* ================ BLE State ================ */
BLEServer *pServer = nullptr;
BLECharacteristic *pControlCharacteristic = nullptr;
BLECharacteristic *pJpegCharacteristic = nullptr;
bool deviceConnected = false;
static std::vector<uint8_t> gJpegBuffer;

int counter = 0;

void sendJpegToCentral();

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("connect");
    deviceConnected = true;
  }

  void onDisconnect(BLEServer *server) override {
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextSize(2);
    M5.Lcd.println("disconnect");
    deviceConnected = false;
    BLEAdvertising *advertising = server->getAdvertising();
    if (advertising != nullptr) {
      advertising->start();
    }
  }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *characteristic) override {
    M5.Lcd.println("read");
    String value = characteristic->getValue();
    M5.Lcd.println(value.c_str());
  }

  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    M5.Lcd.setCursor(0, 160);
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("written");
    M5.Lcd.setTextSize(5);
    M5.Lcd.println(value.c_str());
    if (value == "SEND_JPEG") {
      M5.Lcd.setTextSize(2);
      M5.Lcd.println("JPEG start");
      sendJpegToCentral();
      M5.Lcd.println("JPEG done");
    }
  }
};

static bool notifyChunk(BLECharacteristic *characteristic,
                        const uint8_t *data,
                        size_t length,
                        uint32_t gapMs = 20) {
  if (!deviceConnected || characteristic == nullptr || data == nullptr || length == 0) {
    return false;
  }
  characteristic->setValue(const_cast<uint8_t *>(data), length);
  size_t storedLength = characteristic->getLength();
  Serial.printf("notify len=%u stored=%u first=%02X%02X\n",
                static_cast<unsigned>(length),
                static_cast<unsigned>(storedLength),
                length > 0 ? data[0] : 0,
                length > 1 ? data[1] : 0);
  characteristic->notify();
  delay(gapMs);
  return true;
}

/* ================ Arduino ================ */
void setupBle();

void setup() {
  Serial.begin(115200);

  M5.begin();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Start!");

  if (!CoreS3.Camera.begin()) {
    CoreS3.Display.drawString("Camera Init Fail", CoreS3.Display.width() / 2, CoreS3.Display.height() / 2);
    return;
  }

  setupBle();
}

void loop() {
  CoreS3.update();

  if (!deviceConnected) {
    return;
  }

  if (CoreS3.BtnPWR.wasClicked()) {
    pControlCharacteristic->setValue(counter);
    pControlCharacteristic->notify();
    M5.Lcd.setCursor(0, 60);
    M5.Lcd.setTextSize(3);
    M5.Lcd.println("notify");
    M5.Lcd.setTextSize(5);
    M5.Lcd.println(counter);
    counter += 1;
  }
}

bool captureFrameJPEG(std::vector<uint8_t>& outJpeg) {
  
  if (!CoreS3.Camera.get()) {
    Serial.println("Failed to get a camera frame.");
    return false;
  }

  uint8_t* out_jpg = NULL;
  size_t out_jpg_len = 0;
  frame2jpg(CoreS3.Camera.fb, 255, &out_jpg, &out_jpg_len);

  Serial.printf("captured: %d\n", out_jpg_len);

  outJpeg.assign(out_jpg, out_jpg + out_jpg_len);
  free(out_jpg);
  CoreS3.Camera.free();
  return true;
}

/* ================ BLE Helpers ================ */
void setupBle() {
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Init BLE");

  BLEDevice::init(SERVER_NAME);
  BLEDevice::setMTU(517);  // Request full L2CAP throughput.

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pControlCharacteristic = pService->createCharacteristic(
    CONTROL_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_WRITE_NR |
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE
  );
  pControlCharacteristic->setCallbacks(new ControlCallbacks());
  pControlCharacteristic->addDescriptor(new BLE2902());

  pJpegCharacteristic = pService->createCharacteristic(
    JPEG_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY |
    BLECharacteristic::PROPERTY_INDICATE |
    BLECharacteristic::PROPERTY_READ
  );
  BLEDescriptor *jpegDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
  jpegDescriptor->setValue("image/jpeg");
  pJpegCharacteristic->addDescriptor(jpegDescriptor);
  BLE2902 *jpegCccd = new BLE2902();
  jpegCccd->setNotifications(true);
  jpegCccd->setIndications(true);
  pJpegCharacteristic->addDescriptor(jpegCccd);

  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  M5.Lcd.println("BLE ready");
}

void sendJpegToCentral() {
  if (!deviceConnected || pJpegCharacteristic == nullptr) {
    M5.Lcd.println("No central");
    return;
  }

  BLE2902 *cccd = (BLE2902 *)pJpegCharacteristic->getDescriptorByUUID((uint16_t)0x2902);
  if (cccd == nullptr) {
    Serial.println("CCCD descriptor missing");
    M5.Lcd.println("CCCD missing");
  }

  std::vector<uint8_t> outJpeg;
  if (!captureFrameJPEG(outJpeg)) {
    Serial.println("Failed to capture camera image.");
    return;
  }

  const uint32_t totalSize = static_cast<uint32_t>(outJpeg.size());
  Serial.printf("JPEG size: %lu bytes\n", static_cast<unsigned long>(totalSize));

  uint8_t header[8];
  header[0] = 'J';
  header[1] = 'P';
  header[2] = 'E';
  header[3] = 'G';
  header[4] = (totalSize >> 24) & 0xFF;
  header[5] = (totalSize >> 16) & 0xFF;
  header[6] = (totalSize >> 8) & 0xFF;
  header[7] = totalSize & 0xFF;

  uint16_t negotiatedMtu = 23;
  if (pServer != nullptr) {
    const uint16_t connId = pServer->getConnId();
    const uint16_t peerMtu = pServer->getPeerMTU(connId);
    if (peerMtu > 0) {
      negotiatedMtu = peerMtu;
    }
  }

  size_t payloadSize = negotiatedMtu > 3 ? negotiatedMtu - 3 : 20;
  if (payloadSize > 180) {
    payloadSize = 180;
  }
  if (payloadSize < 20) {
    payloadSize = 20;
  }
  Serial.printf("MTU negotiated: %u, payload chunk: %u bytes\n",
                static_cast<unsigned>(negotiatedMtu),
                static_cast<unsigned>(payloadSize));

  if (!notifyChunk(pJpegCharacteristic, header, sizeof(header))) {
    Serial.println("Failed to send JPEG header");
    return;
  }

  size_t offset = 0;
  size_t chunkIndex = 0;
  const size_t totalChunks = (outJpeg.size() + payloadSize - 1) / payloadSize;

  while (offset < outJpeg.size() && deviceConnected) {
    const size_t chunk = std::min(payloadSize, outJpeg.size() - offset);
    if (!notifyChunk(pJpegCharacteristic, &outJpeg[offset], chunk)) {
      Serial.printf("Chunk send failed at %u/%u\n",
                    static_cast<unsigned>(chunkIndex + 1),
                    static_cast<unsigned>(totalChunks));
      break;
    }
    offset += chunk;
    chunkIndex += 1;
    if ((chunkIndex % 20) == 0 || chunkIndex == totalChunks) {
      Serial.printf("Chunk %u/%u sent (%u bytes total)\n",
                    static_cast<unsigned>(chunkIndex),
                    static_cast<unsigned>(totalChunks),
                    static_cast<unsigned>(offset));
    }
  }

  const bool transferComplete = (chunkIndex == totalChunks) && (offset == outJpeg.size());
  if (!transferComplete) {
    Serial.printf("JPEG transfer incomplete (%u/%u chunks)\n",
                  static_cast<unsigned>(chunkIndex),
                  static_cast<unsigned>(totalChunks));
  }
  M5.Lcd.setCursor(0, 200);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf("JPEG %s %luB (%u/%u)\n",
                transferComplete ? "sent" : "partial",
                static_cast<unsigned long>(outJpeg.size()),
                static_cast<unsigned>(chunkIndex),
                static_cast<unsigned>(totalChunks));
}
