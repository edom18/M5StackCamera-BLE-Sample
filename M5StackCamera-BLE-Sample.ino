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

#define SERVICE_UUID                 "FE56"
#define CONTROL_CHARACTERISTIC_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define JPEG_CHARACTERISTIC_UUID     "c9d1cba2-1f32-4fb0-b6bc-9b73c7d8b4e2"
#define SERVER_NAME                  "M5CoreS3_v2"

static LGFX_Sprite spr;
static bool inited = false;

/* ================ BLE State ================ */
BLEServer *pServer = nullptr;
BLECharacteristic *pControlCharacteristic = nullptr;
BLECharacteristic *pJpegCharacteristic = nullptr;
bool deviceConnected = false;
static std::vector<uint8_t> gJpegBuffer;

uint16_t negotiatedMtu = 23;
size_t payloadSize = 20;
bool isSending = false;
bool needsSendHeader = false;

size_t offset = 0;
size_t chunkIndex = 0;
size_t totalChunks = 0;
uint32_t totalSize = 0;

const int previewWindowX = 2;
const int previewWindowY = 2;
const int logWindowX = 1;
const int logWindowY = 120 + 5;
const int logCursorX = 15;
const int logCursorY = 120 + 20;

void prepareSendJpegToCentral();
void sendJpegToCentral();
void sendResponse();
void resetParams();
void log(String message);
void clearLog();

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    Serial.println("connect");
    log("Connected a client.");
    deviceConnected = true;
  }

  void onDisconnect(BLEServer *server) override {
    Serial.println("=== BLE DISCONNECTED ===");
    log("Disconnected a client.");
    deviceConnected = false;
    BLEAdvertising *advertising = server->getAdvertising();
    if (advertising != nullptr) {
      advertising->start();
    }
  }

  void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    Serial.println("On MTU changed.");

    if (!param) {
      return;
    }

    uint16_t mtu = param->mtu.mtu;
    Serial.printf("MTU=%u\n", mtu);
  }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *characteristic) override {
    Serial.println("Control characteristic read.");
    String value = characteristic->getValue();
    log(value);
  }

  void onWrite(BLECharacteristic *characteristic) override {
    if (isSending) return;

    String value = characteristic->getValue();
    Serial.printf("Received control characteristic: %s\n", value.c_str());
    if (value == "SEND_JPEG") {
      Serial.println("Command received.");
      log("Command received.");
      prepareSendJpegToCentral();
    }
  }
};

class JpegCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onStatus(BLECharacteristic* characteristic, Status s, uint32_t code) override {
    Serial.printf("s=%d, code=%d\n", s, code);
    
    switch (s) {
      case Status::SUCCESS_INDICATE:
        Serial.println("Incidate confirmed by client.");
        break;
      case Status::SUCCESS_NOTIFY:
        Serial.println("Notify success.");
        break;
      case Status::ERROR_INDICATE_DISABLED:
        Serial.println("Incidate disabled.");
        break;
      case Status::ERROR_NOTIFY_DISABLED:
        Serial.println("Notify disabled.");
        break;
      case Status::ERROR_GATT:
        Serial.println("Error GATT.");
        break;
      case Status::ERROR_NO_CLIENT:
        Serial.println("No Client.");
        break;
      case Status::ERROR_INDICATE_TIMEOUT:
        Serial.println("Indicate timeout.");
        break;
      case Status::ERROR_INDICATE_FAILURE:
        Serial.println("Indicate failure.");
        break;
    }
  }
};

///
/// チャンクをひとつ分送信
///
static bool sendChunk(BLECharacteristic *characteristic, const uint8_t *data, size_t length) {
  if (!deviceConnected) {
    Serial.println("sendChunk: device not connected");
    return false;
  }
  if (characteristic == nullptr) {
    Serial.println("sendChunk: characteristic is null");
    return false;
  }
  if (data == nullptr || length == 0) {
    Serial.println("sendChunk: invalid data");
    return false;
  }

  characteristic->setValue(const_cast<uint8_t*>(data), length);
  characteristic->indicate();

  if (!deviceConnected) {
    Serial.println("sendChunk: connection lost during notify");
    return false;
  }

  return true;
}

/* ================ Arduino ================ */
void setupBle();

void drawQuarter(const uint16_t* rgb565, int x, int y, int camW, int camH, float zoom = 0.5f) {
  if (!inited) {
    spr.setPsram(true);
    spr.setColorDepth(16);
    spr.createSprite(camW, camH);
    spr.setPivot(0, 0);
    inited = true;
  }

  spr.pushImage(0, 0, camW, camH, rgb565);

  int w = (camW * zoom) + 2;
  int h = (camH * zoom) + 2;
  CoreS3.Display.drawRect(x - 1, y - 1, w, h, TFT_WHITE);

  CoreS3.Display.startWrite();
  spr.pushRotateZoom(&CoreS3.Display, x, y, 0.0f, zoom, zoom);
  CoreS3.Display.endWrite();
}

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  CoreS3.begin(cfg);
  CoreS3.Display.setTextColor(TFT_WHITE);
  CoreS3.Display.setTextDatum(top_left);
  CoreS3.Display.setFont(&fonts::Orbitron_Light_24);
  CoreS3.Display.setTextSize(0.4f);

  if (!CoreS3.Camera.begin()) {
    CoreS3.Display.drawString("Camera Init Fail", CoreS3.Display.width() / 2, CoreS3.Display.height() / 2);
    return;
  }

  clearLog();

  setupBle();
}

void loop() {
  CoreS3.update();

  // if (!(needsSendHeader || isSending)) {
    if (CoreS3.Camera.get()) {
      float zoom = 0.5f;
      int w = CoreS3.Display.width();
      int h = CoreS3.Display.height();
      drawQuarter((uint16_t *)CoreS3.Camera.fb->buf, previewWindowX, previewWindowY, w, h, zoom);
      CoreS3.Camera.free();
    }
  // }

  if (!deviceConnected) {
    return;
  }

  if (needsSendHeader) {
    sendHedaerToCentral();
    return;
  }

  if (isSending) {
    sendJpegToCentral();
  }
}

///
/// 現在のカメラの状況をキャプチャ
///
bool captureFrameJPEG(std::vector<uint8_t>& outJpeg) {
  
  if (!CoreS3.Camera.get()) {
    Serial.println("Failed to get a camera frame.");
    return false;
  }

  uint8_t* out_jpg = NULL;
  size_t out_jpg_len = 0;
  frame2jpg(CoreS3.Camera.fb, 255, &out_jpg, &out_jpg_len);

  // Serial.printf("captured: %d\n", out_jpg_len);

  outJpeg.assign(out_jpg, out_jpg + out_jpg_len);
  free(out_jpg);
  CoreS3.Camera.free();
  return true;
}

void log(String message) {
  int x = logCursorX;
  int y = CoreS3.Display.getCursorY();
  CoreS3.Display.setCursor(x, y);
  CoreS3.Display.printf("%s\n", message.c_str());
}

void clearLog() {
  CoreS3.Display.setCursor(logCursorX, logCursorY);
  int w = CoreS3.Display.width() - logWindowX;
  int h = CoreS3.Display.height() - logWindowY;
  CoreS3.Display.fillRect(logWindowX, logWindowY, w, h, TFT_BLACK);
  CoreS3.Display.drawRect(logWindowX, logWindowY, w, h, TFT_WHITE);
}

/* ================ BLE Helpers ================ */
void setupBle() {
  log("Init BLE");

  BLEDevice::init(SERVER_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pControlCharacteristic = pService->createCharacteristic(
    CONTROL_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_WRITE_NR |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pControlCharacteristic->setCallbacks(new ControlCallbacks());
  pControlCharacteristic->addDescriptor(new BLE2902());

  pJpegCharacteristic = pService->createCharacteristic(
    JPEG_CHARACTERISTIC_UUID,
    // BLECharacteristic::PROPERTY_NOTIFY   |
    BLECharacteristic::PROPERTY_READ     |
    BLECharacteristic::PROPERTY_INDICATE
  );
  pJpegCharacteristic->addDescriptor(new BLE2902());
  // pJpegCharacteristic->setCallbacks(new JpegCharacteristicCallbacks());
  // BLEDescriptor *jpegDescriptor = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
  // jpegDescriptor->setValue("image/jpeg");
  // pJpegCharacteristic->addDescriptor(jpegDescriptor);

  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  log("BLE ready");
}

///
/// CCCD をチェック
///
bool checkCCCD() {
  BLE2902 *cccd = (BLE2902 *)pJpegCharacteristic->getDescriptorByUUID((uint16_t)0x2902);
  if (cccd == nullptr) {
    Serial.println("CCCD descriptor missing");
    log("CCCD missing");
    return false;
  }

  // Check CCCD value
  uint8_t* cccdValue = cccd->getValue();
  uint16_t cccdLen = cccd->getLength();
  Serial.printf("CCCD length=%u value=", cccdLen);
  for (int i = 0; i < cccdLen; i++) {
    Serial.printf("%02X", cccdValue[i]);
  }
  
  bool notifyEnabled = (cccdLen >= 1 && (cccdValue[0] & 0x01) != 0);
  bool indicateEnabled = (cccdLen >= 1 && (cccdValue[0] & 0x02) != 0);
  Serial.printf("CCCD state: notify=%d indicate=%d\n", notifyEnabled, indicateEnabled);

  if (!notifyEnabled && !indicateEnabled) {
    Serial.println("ERROR: Client has not enabled notifications/indications");
    log("CCCD not enabled");
    return false;
  }

  return true;
}

///
/// パラメータをリセット
///
void resetParams() {
  offset = 0;
  chunkIndex = 0;
  totalChunks = 0;
  totalSize = 0;
  isSending = false;
  needsSendHeader = false;
}

void sendHedaerToCentral() {
  // 準備としてまずはヘッダーを送信する
  uint8_t header[8];
  header[0] = 'J';
  header[1] = 'P';
  header[2] = 'E';
  header[3] = 'G';
  // サイズをパック
  header[4] = (totalSize >> 24) & 0xFF;
  header[5] = (totalSize >> 16) & 0xFF;
  header[6] = (totalSize >> 8) & 0xFF;
  header[7] = totalSize & 0xFF;

  if (!sendChunk(pJpegCharacteristic, header, sizeof(header))) {
    Serial.println("Failed to send JPEG header");
    resetParams();
    return;
  }

  // 送信のための情報を初期化
  offset = 0;
  chunkIndex = 0;
  totalChunks = (gJpegBuffer.size() + payloadSize - 1) / payloadSize;

  needsSendHeader = false;
  isSending = true;
}

///
/// 1 チャンクの送信処理
///
void sendJpegToCentral() {
  const size_t chunk = std::min(payloadSize, gJpegBuffer.size() - offset);
  if (!sendChunk(pJpegCharacteristic, &gJpegBuffer[offset], chunk)) {
    Serial.printf("Chunk send failed at %u/%u (disconnected=%d)\n",
                  static_cast<unsigned>(chunkIndex + 1),
                  static_cast<unsigned>(totalChunks),
                  !deviceConnected);
    // 送信失敗時はすべてをリセット
    resetParams();
    return;
  }

  offset += chunk;
  chunkIndex += 1;
  if ((chunkIndex % 10) == 0 || chunkIndex == totalChunks || chunkIndex <= 3) {
    Serial.printf("Chunk %u/%u sent (%u bytes total)\n",
                  static_cast<unsigned>(chunkIndex),
                  static_cast<unsigned>(totalChunks),
                  static_cast<unsigned>(offset));
  }

  // まだ送信しきっていない場合は次の送信につなげる
  if (offset < gJpegBuffer.size() && deviceConnected) {
    return;
  }

  const bool transferComplete = (chunkIndex == totalChunks) && (offset == gJpegBuffer.size());
  if (!transferComplete) {
    Serial.printf("JPEG transfer incomplete (%u/%u chunks)\n",
                  static_cast<unsigned>(chunkIndex),
                  static_cast<unsigned>(totalChunks));
    // 送信失敗時はすべてをリセット
    resetParams();

    return;
  }

  Serial.println("Has been sent.");
  log("Has been sent");

  // CoreS3.Display.printf("JPEG %s %luB (%u/%u)\n",
  //               transferComplete ? "sent" : "partial",
  //               static_cast<unsigned long>(offset),
  //               static_cast<unsigned>(chunkIndex),
  //               static_cast<unsigned>(totalChunks));

  Serial.printf("JPEG %s %luB (%u/%u)\n",
              transferComplete ? "sent" : "partial",
              static_cast<unsigned long>(offset),
              static_cast<unsigned>(chunkIndex),
              static_cast<unsigned>(totalChunks));

  resetParams();
}

///
/// 画像送信の準備
///
void prepareSendJpegToCentral() {
  if (!deviceConnected || pJpegCharacteristic == nullptr) {
    Serial.println("No central");
    log("No central");
    return;
  }

  if (!checkCCCD()) {
    return;
  }

  // Wait a bit to ensure BLE connection is stable after CCCD write
  Serial.println("Waiting for BLE connection to stabilize...");

  gJpegBuffer.clear();
  if (!captureFrameJPEG(gJpegBuffer)) {
    Serial.println("Failed to capture camera image.");
    log("Failed to capture camera image.");
    return;
  }

  totalSize = static_cast<uint32_t>(gJpegBuffer.size());
  Serial.printf("JPEG size: %lu bytes\n", static_cast<unsigned long>(totalSize));
  // log("JPEG size: %lu bytes\n", static_cast<unsigned long>(totalSize));

  if (pServer != nullptr) {
    const uint16_t connId = pServer->getConnId();
    const uint16_t peerMtu = pServer->getPeerMTU(connId);
    if (peerMtu > 0) {
      negotiatedMtu = peerMtu;
    }
  }

  payloadSize = negotiatedMtu > 3 ? negotiatedMtu - 3 : 20;
  if (payloadSize < 20) {
    payloadSize = 20;
  }
  Serial.printf("MTU negotiated: %u, payload chunk: %u bytes\n",
                static_cast<unsigned>(negotiatedMtu),
                static_cast<unsigned>(payloadSize));

  needsSendHeader = true;
}
