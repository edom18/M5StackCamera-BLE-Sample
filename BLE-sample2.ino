#include <M5CoreS3.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#define SERVICE_UUID                 "FE55"
#define CONTROL_CHARACTERISTIC_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define JPEG_CHARACTERISTIC_UUID     "c9d1cba2-1f32-4fb0-b6bc-9b73c7d8b4e2"
#define SERVER_NAME                  "M5CoreS3"

/* ================ JPEG Payload (base64) ================ */
static const char kJpegBase64[] = R"(/9j/4QAYRXhpZgAASUkqAAgAAAAAAAAAAAAAAP/sABFEdWNreQABAAQAAABQAAD/4QMwaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wLwA8P3hwYWNrZXQgYmVnaW49Iu+7vyIgaWQ9Ilc1TTBNcENlaGlIenJlU3pOVGN6a2M5ZCI/PiA8eDp4bXBtZXRhIHhtbG5zOng9ImFkb2JlOm5zOm1ldGEvIiB4OnhtcHRrPSJBZG9iZSBYTVAgQ29yZSA5LjEtYzAwMyA3OS45NjkwYTg3ZmMsIDIwMjUvMDMvMDYtMjA6NTA6MTYgICAgICAgICI+IDxyZGY6UkRGIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF4LW5zIyI+IDxyZGY6RGVzY3JpcHRpb24gcmRmOmFib3V0PSIiIHhtbG5zOnhtcD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wLyIgeG1sbnM6eG1wTU09Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9tbS8iIHhtbG5zOnN0UmVmPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvc1R5cGUvUmVzb3VyY2VSZWYjIiB4bXA6Q3JlYXRvclRvb2w9IkFkb2JlIFBob3Rvc2hvcCAyNi4xMSAoV2luZG93cykiIHhtcE1NOkluc3RhbmNlSUQ9InhtcC5paWQ6MzlCRjRBNURBNDI0MTFGMDg0NzI4MzQwQTg3QUM1MjgiIHhtcE1NOkRvY3VtZW50SUQ9InhtcC5kaWQ6MzlCRjRBNUVBNDI0MTFGMDg0NzI4MzQwQTg3QUM1MjgiPiA8eG1wTU06RGVyaXZlZEZyb20gc3RSZWY6aW5zdGFuY2VJRD0ieG1wLmlpZDozOUJGNEE1QkE0MjQxMUYwODQ3MjgzNDBBODdBQzUyOCIgc3RSZWY6ZG9jdW1lbnRJRD0ieG1wLmRpZDozOUJGNEE1Q0E0MjQxMUYwODQ3MjgzNDBBODdBQzUyOCIvPiA8L3JkZjpEZXNjcmlwdGlvbj4gPC9yZGY6UkRGPiA8L3g6eG1wbWV0YT4gPD94cGFja2V0IGVuZD0iciI/Pv/uAA5BZG9iZQBkwAAAAAH/2wCEAAICAgICAgICAgIDAgIC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)";

/* ================ BLE State ================ */
BLEServer *pServer = nullptr;
BLECharacteristic *pControlCharacteristic = nullptr;
BLECharacteristic *pJpegCharacteristic = nullptr;
bool deviceConnected = false;
static std::vector<uint8_t> gJpegBuffer;

int counter = 0;

static inline int8_t decodeBase64Char(unsigned char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

static std::vector<uint8_t> decodeBase64(const char *input) {
  std::vector<uint8_t> output;
  output.reserve((std::strlen(input) * 3) / 4);
  int val = 0;
  int bits = -8;
  const unsigned char *ptr = reinterpret_cast<const unsigned char *>(input);
  while (*ptr) {
    unsigned char c = *ptr++;
    if (c == '=') {
      break;
    }
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      continue;
    }
    int8_t decoded = decodeBase64Char(c);
    if (decoded < 0) {
      continue;
    }
    val = (val << 6) + decoded;
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return output;
}

static void ensureJpegLoaded() {
  if (!gJpegBuffer.empty()) {
    return;
  }
  gJpegBuffer = decodeBase64(kJpegBase64);
}

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

/* ================ Arduino ================ */
void setupBle();

void setup() {
  M5.begin();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Start!");

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

/* ================ BLE Helpers ================ */
void setupBle() {
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Init BLE");

  BLEDevice::init(SERVER_NAME);
  BLEDevice::setMTU(517);  // Request full L2CAP throughput.

  ensureJpegLoaded();

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
    BLECharacteristic::PROPERTY_READ
  );
  pJpegCharacteristic->addDescriptor(new BLE2902());
  BLEDescriptor *jpegDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
  jpegDescriptor->setValue("image/jpeg");
  pJpegCharacteristic->addDescriptor(jpegDescriptor);

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

  if (gJpegBuffer.empty()) {
    M5.Lcd.println("JPEG missing");
    return;
  }

  uint16_t mtu = BLEDevice::getMTU();
  size_t payloadSize = mtu > 3 ? mtu - 3 : 20;  // Subtract ATT header bytes.
  if (payloadSize == 0) {
    payloadSize = 20;
  }

  size_t offset = 0;
  size_t chunkIndex = 0;
  const size_t totalChunks = (gJpegBuffer.size() + payloadSize - 1) / payloadSize;

  while (offset < gJpegBuffer.size() && deviceConnected) {
    const size_t chunk = std::min(payloadSize, gJpegBuffer.size() - offset);
    pJpegCharacteristic->setValue(&gJpegBuffer[offset], chunk);
    pJpegCharacteristic->notify();
    offset += chunk;
    chunkIndex += 1;
    delay(15);  // Give the stack time to flush notifications.
  }

  M5.Lcd.setCursor(0, 200);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf("JPEG sent (%u/%u)\n", static_cast<unsigned>(chunkIndex), static_cast<unsigned>(totalChunks));
}
