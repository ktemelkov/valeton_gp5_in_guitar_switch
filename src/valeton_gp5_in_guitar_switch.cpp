#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_pm.h"

#include "valeton_gp5_comm.h"
#include "guitar_switch.h"
#include "guitar_encoder.h"

#include "debug.h"

#define SWITCH_PIN1 GPIO_NUM_3
#define SWITCH_PIN2 GPIO_NUM_2

#define ENCODER_PIN1 GPIO_NUM_4
#define ENCODER_PIN2 GPIO_NUM_5
#define ENCODER_BUTTON_PIN GPIO_NUM_6

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
static GuitarEncoder guitarEncoder(ENCODER_PIN1, ENCODER_PIN2, ENCODER_BUTTON_PIN);
static int gp5PresetNo = -1;
static bool delayOn = false;

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
    delayOn = false;
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

  int targetPresetNo = bank * 30 + sect * 10 + indexInSect;
  int rythmPresetNo = bank * 30 + 10 + indexInSect; // Middle sect is rythm

  return targetPresetNo == currentPresetNo ? rythmPresetNo : targetPresetNo;
}

/**
 *
 */
bool connect_valeton_gp5(const NimBLEAdvertisedDevice *advDevice)
{
  if (!advDevice->isAdvertisingService(NimBLEUUID(Valeton_Service_UUID_Str)) || advDevice->getName().compare("GP-5 BLE Kalin") != 0)
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
void requestCurrentPreset()
{
  if (!sysExChannel)
  {
    return;
  }

  int len = 0;
  uint8_t *buff = valeton_gp5_current_preset_request(len);
  sysExChannel->writeValue(buff, len, false);

  DEBUG_MSG("%s", "Sent current preset query SysEx message to Valeton GP-5.\n");
  DEBUG_BUFFER(buff, len);
}

/**
 *
 */
void requestPresetChange(int targetPresetNo)
{
  if (!sysExChannel)
  {
    return;
  }

  int len = 0;
  uint8_t *buff = valeton_gp5_preset_change_request(targetPresetNo, len);
  sysExChannel->writeValue(buff, len, false);

  DEBUG_MSG("Sent preset change to #%d SysEx message to Valeton GP-5.\n", targetPresetNo);
  DEBUG_BUFFER(buff, len);
}

/**
 *
 */
void requestDelayToggle(bool on)
{
  if (!sysExChannel)
  {
    return;
  }

  int len = 0;
  uint8_t *buff = valeton_gp5_delay_toggle_request(on, len);
  sysExChannel->writeValue(buff, len, false);

  if (on) {
    DEBUG_MSG("%s", "Sent delay ON SysEx message to Valeton GP-5.\n");
  } else {
    DEBUG_MSG("%s", "Sent delay OFF SysEx message to Valeton GP-5.\n");
  }

  DEBUG_BUFFER(buff, len);
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
    if (requestTimer != 0)
    {
      requestTimer = 0;
    } else {
      requestTimer = millis();
      requestCurrentPreset();
    }
  }
  else if (event == EVENT_DEVICE_DISCONNECTED)
  {
    requestTimer = millis();
    setState(STATE_INIT);
  }
  else if (event == EVENT_PRESET_CHANGED)
  {
    requestTimer = 0;
  }
  else if (event == EVENT_IDLE && requestTimer != 0 && (millis() - requestTimer) >= 500)
  {
    DEBUG_MSG("%s", "\nSysEx request timed out.\n");

    requestTimer = 0;
  }
  else if (event == EVENT_IDLE && requestTimer == 0)
  {
    if (gp5PresetNo == -1)
    {
      requestTimer = millis();
      requestCurrentPreset();
      return;
    }

    // Check switch states and send preset change if needed
    if (!guitarEncoder.hasButtonBeenPressed())
    {
      return;
    }

    int targetPresetNo = selectTargetPreset(guitarSwitch.getPosition(), gp5PresetNo);

    if (targetPresetNo != gp5PresetNo)
    {
      requestTimer = millis();
      requestPresetChange(targetPresetNo);
      delay(50);
      requestCurrentPreset();
    } else {
      requestDelayToggle(delayOn = !delayOn);
      delay(50);
    }
  }
}

/**
 *
 */
void setup()
{
  esp_pm_config_esp32c3_t pm_config;
  pm_config.max_freq_mhz = 40; // Set maximum CPU frequency to 40 MHz
  pm_config.min_freq_mhz = 0; // Set minimum CPU frequency to 0 MHz
  pm_config.light_sleep_enable = true; // Enable light sleep mode
  
  esp_pm_configure(&pm_config);
  gpio_sleep_sel_dis(SWITCH_PIN1);
  gpio_sleep_sel_dis(SWITCH_PIN2);
  // gpio_sleep_sel_dis(GPIO_NUM_4);
  // gpio_sleep_sel_dis(GPIO_NUM_5);
  gpio_sleep_sel_dis(ENCODER_BUTTON_PIN);

  gpio_wakeup_enable(SWITCH_PIN1, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(SWITCH_PIN2, GPIO_INTR_LOW_LEVEL);
  // gpio_wakeup_enable(GPIO_NUM_4, GPIO_INTR_LOW_LEVEL);
  // gpio_wakeup_enable(GPIO_NUM_5, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(ENCODER_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);

#ifdef ENABLE_DEBUG_MESSAGES
  Serial.begin(115200);
  delay(1000);
#endif

  DEBUG_MSG("%s", "Starting BLE Client ...\n");

  NimBLEDevice::init("");
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */

  guitarSwitch.begin();
  guitarEncoder.begin();
}

/**
 *
 */
void loop()
{
  guitarSwitch.update();
  guitarEncoder.update();

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

extern "C" void app_main()
{
  initArduino();
  setup();

  while (true)
  {
    loop();
  }
}
