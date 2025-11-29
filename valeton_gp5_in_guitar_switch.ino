#include <Arduino.h>
#include <NimBLEDevice.h>
#include "valeton_gp5_comm.h"
#include "guitar_switch.h"

#include "debug.h"

#define SWITCH_PIN1 2
#define SWITCH_PIN2 3

// Target 128-bit Service and Characteristic UUIDs
static const char *Valeton_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char *Valeton_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

#define STATE_INIT 0
#define STATE_SCANNING 1
#define STATE_CONNECTING 2
#define STATE_CONNECTED 23

#define EVENT_IDLE 0
#define EVENT_DEVICE_FOUND 1
#define EVENT_DEVICE_CONNECTED 2
#define EVENT_DEVICE_DISCONNECTED 3
#define EVENT_DEVICE_NOTIFY 4
#define EVENT_STATE_ENTERED 5
#define EVENT_PRESET_CHANGED 6

static int currentState = STATE_INIT;
static int currentEvent = EVENT_IDLE;
static void *eventData = nullptr;

static NimBLERemoteCharacteristic *sysExChannel = nullptr;
static GuitarSwitch guitarSwitch(SWITCH_PIN1, SWITCH_PIN2);
static int gp5PresetNo = 0;

/**
 *
 */
void fireEvent(int event, void *data = nullptr)
{
  currentEvent = event;
  eventData = data;
}

/**
 *
 */
void setState(int newState)
{
  currentState = newState;
  fireEvent(EVENT_STATE_ENTERED);
}

/**
 *
 */
class ScanCallbacksImpl : public NimBLEScanCallbacks
{
  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
  {
    DEBUG_MSG("Advertised Device found: %s\n", advertisedDevice->toString().c_str());

    fireEvent(EVENT_DEVICE_FOUND, (void *)advertisedDevice);
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override
  {
    Serial.printf("Scan Ended, reason: %d, device count: %d; Restarting scan\n", reason, results.getCount());
  }
} ScanCallbacks;

/**
 *
 */
class ClientCallbacksImpl : public NimBLEClientCallbacks
{
  void onConnect(NimBLEClient *pClient)
  {
    DEBUG_MSG("Connected to: %s\n", pClient->getPeerAddress().toString().c_str());
    DEBUG_MSG("RSSI: %d\n", pClient->getRssi());

    pClient->updateConnParams(120, 120, 0, 60);

    fireEvent(EVENT_DEVICE_CONNECTED, (void *)pClient);
  }

  void onConnectFail(NimBLEClient *pClient, int reason)
  {
    DEBUG_MSG("Failed to connect to: %s, reason: %d\n", pClient->getPeerAddress().toString().c_str(), reason);

    fireEvent(EVENT_DEVICE_DISCONNECTED);
  }

  void onDisconnect(NimBLEClient *pClient, int reason)
  {
    DEBUG_MSG("Disconnected from: %s, reason: %d\n", pClient->getPeerAddress().toString().c_str(), reason);

    fireEvent(EVENT_DEVICE_DISCONNECTED);
  }
} ClientCallbacks;

/**
 *
 */
void deviceNotifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool /* isNotify */)
{
  DEBUG_MSG("%s", "\nNotification/Indication received.\n");
  DEBUG_BUFFER(pData, length);

  // Process the response/status data sent by the GP-5 here.
  uint8_t op = valeton_gp5_decode_op(pData, length);
  uint8_t preset_no = valeton_gp5_decode_preset_no(pData, length);

  if (op == 0x43 && preset_no != 0xFF) // Preset change notification
  {
    DEBUG_MSG("Preset changed to #%d\n", preset_no);

    gp5PresetNo = preset_no;
    fireEvent(EVENT_PRESET_CHANGED);
  }
}

/**
 *
 */
int selectTargetPreset(SwitchPosition position, int currentPresetNo)
{
  if (currentPresetNo > 89)
  {
    return currentPresetNo;
  }

  int bank = currentPresetNo / 30;
  int presetIndex = currentPresetNo % 30;

  int sect = presetIndex / 10;
  int indexInSect = presetIndex % 10;

  if (position == TOP)
  {
    sect = 0;
  }
  else if (position == MIDDLE)
  {
    sect = 1;
  }
  else if (position == BOTTOM)
  {
    sect = 2;
  }

  return currentPresetNo = bank * 30 + sect * 10 + indexInSect;
}

/**
 *
 */
bool connect_valeton_gp5(const NimBLEAdvertisedDevice *advDevice)
{
  if (!advDevice->isAdvertisingService(NimBLEUUID(Valeton_Service_UUID_Str)))
  {
    return false;
  }

  NimBLEClient *pClient = NimBLEDevice::getDisconnectedClient();

  if (!pClient)
  {
    pClient = NimBLEDevice::createClient(advDevice->getAddress());
    pClient->setSelfDelete(true, true);

    if (!pClient)
    {
      DEBUG_MSG("%s", "Failed to create client.\n");
      return false;
    }
  }

  pClient->setClientCallbacks(&ClientCallbacks, false);
  pClient->setConnectionParams(12, 12, 0, 150);
  pClient->setConnectTimeout(5 * 1000);

  if (!pClient->connect(advDevice, true, true))
  {
    DEBUG_MSG("%s", "Failed to connect.\n");
    return false;
  }

  return true;
}

/**
 *
 */
bool subscribe_valeton_gp5(NimBLEClient *pClient)
{
  while (1)
  {
    NimBLERemoteService *pSvc = pClient->getService(Valeton_Service_UUID_Str);

    if (!pSvc)
    {
      DEBUG_MSG("%s", "Valeton service not found.\n");
      break;
    }

    NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(Valeton_Char_UUID_Str);

    if (!pChr)
    {
      DEBUG_MSG("%s", "SysEx characteristic not found.\n");
      break;
    }

    if (!pChr->canNotify() || !pChr->subscribe(true, deviceNotifyCB))
    {
      DEBUG_MSG("%s", "Failed to subscribe to notifications.\n");
      break;
    }

    sysExChannel = pChr;
    DEBUG_MSG("%s", "Subscribed to Valeton GP-5 SysEx service.\n");
    return true;
  }

  pClient->disconnect();
  return false;
}

/**
 *
 */
void handle_init(int event, void *data)
{
  delay(1000);

  sysExChannel = nullptr;

  NimBLEScan *pScan = NimBLEDevice::getScan();

  pScan->setScanCallbacks(&ScanCallbacks);
  pScan->setInterval(97);
  pScan->setWindow(67);

  pScan->setActiveScan(true);
  pScan->start(0 /* scan forever */, false, true);

  setState(STATE_SCANNING);
}

/**
 *
 */
void handle_scanning(int event, void *data)
{
  if (event == EVENT_DEVICE_FOUND)
  {
    const NimBLEAdvertisedDevice *advDevice = (NimBLEAdvertisedDevice *)data;

    if (connect_valeton_gp5(advDevice))
    {
      advDevice->getScan()->stop();
      setState(STATE_CONNECTING);
    }
  }
}

/**
 *
 */
void handle_connecting(int event, void *data)
{
  if (event == EVENT_DEVICE_CONNECTED)
  {
    setState(subscribe_valeton_gp5((NimBLEClient *)data) ? STATE_CONNECTED : STATE_INIT);
  }
  else if (event == EVENT_DEVICE_DISCONNECTED)
  {
    setState(STATE_INIT);
  }
}

/**
 *
 */
void handle_connected(int event, void *data)
{
  static time_t requestTimer = 0;

  if (event == EVENT_STATE_ENTERED)
  {
    requestTimer = millis();

    int len = 0;
    uint8_t *buff = valeton_gp5_current_preset_request(len);
    sysExChannel->writeValue(buff, len, false);

    DEBUG_MSG("%s", "Sent current preset query SysEx message to Valeton GP-5.\n");
    DEBUG_BUFFER(buff, len);
  }
  else if (event == EVENT_DEVICE_DISCONNECTED)
  {
    setState(STATE_INIT);
  }
  else if (event == EVENT_PRESET_CHANGED)
  {
    requestTimer = 0;
  }
  else if (event == EVENT_IDLE && requestTimer != 0 && (millis() - requestTimer) >= 2000)
  {
    DEBUG_MSG("%s", "\nSysEx request timed out.\n");

    requestTimer = 0;
  }
  else if (event == EVENT_IDLE && requestTimer == 0)
  {
    // Check switch states and send preset change if needed
    int targetPresetNo = selectTargetPreset(guitarSwitch.getPosition(), gp5PresetNo);

    if (targetPresetNo != gp5PresetNo)
    {
      requestTimer = millis();

      int len = 0;
      uint8_t *buff = valeton_gp5_preset_change_request(targetPresetNo, len);
      sysExChannel->writeValue(buff, len, false);

      DEBUG_MSG("Sent preset change to #%d SysEx message to Valeton GP-5.\n", targetPresetNo);
      DEBUG_BUFFER(buff, len);

      buff = valeton_gp5_current_preset_request(len);
      sysExChannel->writeValue(buff, len, false);

      DEBUG_MSG("%s", "Sent current preset query SysEx message to Valeton GP-5.\n");
      DEBUG_BUFFER(buff, len);
    }
  }
}

/**
 *
 */
void setup()
{
  Serial.begin(115200);
  delay(1000);

  DEBUG_MSG("%s", "Starting BLE Client ...\n");

  NimBLEDevice::init("");
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */

  guitarSwitch.begin();
}

/**
 *
 */
void loop()
{
  guitarSwitch.loop();

  int event = currentEvent;
  void *data = eventData;

  currentEvent = EVENT_IDLE;
  eventData = nullptr;

  switch (currentState)
  {
  case STATE_INIT:
    handle_init(event, data);
    break;
  case STATE_SCANNING:
    handle_scanning(event, data);
    break;
  case STATE_CONNECTING:
    handle_connecting(event, data);
    break;
  case STATE_CONNECTED:
    handle_connected(event, data);
    break;
  default:
    break;
  }
}
