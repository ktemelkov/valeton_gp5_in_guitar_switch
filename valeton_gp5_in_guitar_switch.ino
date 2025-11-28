#include <NimBLEDevice.h>
#include "valeton_gp5_comm.h"

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

static int currentState = STATE_INIT;
static int currentEvent = EVENT_IDLE;
static void *eventData = nullptr;

static NimBLERemoteCharacteristic* sysExChannel = nullptr; 

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
    Serial.print("Advertised Device found: ");
    Serial.println(advertisedDevice->toString().c_str());

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
    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    Serial.print("RSSI: ");
    Serial.println(pClient->getRssi());

    pClient->updateConnParams(120, 120, 0, 60);

    fireEvent(EVENT_DEVICE_CONNECTED, (void *)pClient);
  }

  void onConnectFail(NimBLEClient *pClient, int reason)
  {
    Serial.print("Failed to connect to: ");
    Serial.print(pClient->getPeerAddress().toString().c_str());
    Serial.print(" , reason: ");
    Serial.println(reason);

    fireEvent(EVENT_DEVICE_DISCONNECTED);
  }

  void onDisconnect(NimBLEClient *pClient, int reason)
  {
    Serial.print("Disconnected from ");
    Serial.println(pClient->getPeerAddress().toString().c_str());

    fireEvent(EVENT_DEVICE_DISCONNECTED);
  }
} ClientCallbacks;

/**
 *
 */
void deviceNotifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool /* isNotify */)
{
  // F0 09 0D 00 01 00 00 00 06 01 02 04 03 00 0B 00 00 00 00 00 00 F7
  // F0 0F 0F 00 01 00 00 00 06 01 02 04 03 00 0C 00 00 00 00 00 00 F7 -> preset data response
  // F0 0E 02 00 01 00 00 00 06 01 02 01 0B 00 01 00 00 00 00 00 00 F7 -> preset change response
  // F0 0B 02 00 01 00 00 00 03 01 04 00 08 00 00 F7 -> status response
  // F0 08 08 00 01 00 00 00 0A 01 02 01 01 00 02 00 02 00 04 00 00 04 0E 00 00 00 00 00 00 F7 -> button press response
  //


  Serial.print("Notification/Indication received: ");
  Serial.print(pRemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(", Data length: ");
  Serial.println(length);

  Serial.print("Data: ");
  for (int i = 0; i < length; i++)
  {
    Serial.printf("%02X ", pData[i]);
  }

  Serial.println();

  // Process the response/status data sent by the GP-5 here.
  uint8_t op = valeton_gp5_decode_op(pData, length);
  uint8_t preset_no = valeton_gp5_decode_preset_no(pData, length);

  if (op == 0x43) // Preset change notification
  {
    Serial.print("Preset changed to #: ");
    Serial.println(preset_no);
  }
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
      Serial.println("Failed to create client");
      return false;
    }
  }

  pClient->setClientCallbacks(&ClientCallbacks, false);
  pClient->setConnectionParams(12, 12, 0, 150);
  pClient->setConnectTimeout(5 * 1000);

  if (!pClient->connect(advDevice, true, true))
  {
    Serial.println("Failed to connect");
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
      Serial.println("Valeton service not found.");
      break;
    }

    NimBLERemoteCharacteristic *pChr = pSvc->getCharacteristic(Valeton_Char_UUID_Str);

    if (!pChr)
    {
      Serial.println("SysEx characteristic not found.");
      break;
    }

    if (!pChr->canNotify() || !pChr->subscribe(true, deviceNotifyCB))
    {
      Serial.println("Failed to subscribe to notifications.");
      break;
    }

    sysExChannel = pChr;
    Serial.println("Subscribed to Valeton GP-5 SysEx service.");
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

    if (connect_valeton_gp5(advDevice)) {
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
    setState(subscribe_valeton_gp5((NimBLEClient*)data) ? STATE_CONNECTED : STATE_INIT);
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
  if (event == EVENT_STATE_ENTERED)
  {
    int len = 0;
    uint8_t* buff = valeton_gp5_current_preset_request(len);
    sysExChannel->writeValue(buff, len, false);

    Serial.println("Sent query current preset SysEx message to Valeton GP-5.");
    Serial.print("Data: ");

    for (int i = 0; i < len; i++)
    {
      Serial.printf("%02X ", buff[i]);
    }

    Serial.println();
  }
  else if (event == EVENT_DEVICE_DISCONNECTED)
  {
    setState(STATE_INIT);
  }
}

/**
 *
 */
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting BLE Client");

  NimBLEDevice::init("");
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
}

/**
 *
 */
void loop()
{
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
