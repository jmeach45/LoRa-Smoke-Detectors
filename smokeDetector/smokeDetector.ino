#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <BLEDevice.h>
#include "HT_SSD1306Wire.h"


#define RF_FREQUENCY                                910000000 // Hz
#define TX_OUTPUT_POWER                             15        // dBm
#define LORA_BANDWIDTH                              0         // [0: 125 kHz, 1: 250 kHz, 2: 500 kHz]
#define LORA_SPREADING_FACTOR                       12        // [SF7..SF12]
#define LORA_CODINGRATE                             4         // [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                         0         // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false

#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 30 // Define the payload size here


#define DEVICE_NAME         "Smoke Detector BLE 2"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"


int buzzer = 12;
int smokeA0 = 33;
int sensorThres = 3500;

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];
unsigned long smokeDetectedTime = 0;
bool smokeDetected = false;
bool disableAlarm = false;
unsigned long disableEndTime = 0;

static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

typedef enum {
    LOWPOWER,
    STATE_RX,
    STATE_TX,
    STATE_ALARM
} States_t;

States_t state;

BLECharacteristic *pCharacteristic;

void printToScreen(String s);
void handleBLEWrite(BLECharacteristic *characteristic);

void setupLoRa() {
    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxDone = OnRxDone;

    Radio.Init(&RadioEvents);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                      true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                      0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    state = STATE_RX;
    Radio.Rx(0);
}

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        printToScreen("BLE client connected.");
    };

    void onDisconnect(BLEServer* pServer) {
        printToScreen("BLE client disconnected.");
        BLEDevice::startAdvertising();
    }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) {
        handleBLEWrite(characteristic);
    }
};

void setupBLE() {
    BLEDevice::init(DEVICE_NAME);
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );

    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    pCharacteristic->setValue("Init");
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

void handleBLEWrite(BLECharacteristic *characteristic) {
    String message = String(characteristic->getValue().c_str());
    Serial.println("Received via BLE: " + message);

    if (message == "silence") {
        disableAlarm = true;
        disableEndTime = millis() + 30000; // Disable alarm for 30 seconds
        analogWrite(buzzer, 0);
        Serial.println("Alarms disabled for 30 seconds.");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(buzzer, OUTPUT);
    pinMode(smokeA0, INPUT);

    setupLoRa();
    setupBLE();

    Serial.println("Setup complete.");
}

void loop() {
    int analogSensor = analogRead(smokeA0);
    //Serial.println(analogSensor);
    // Handle smoke detection
    if (!disableAlarm && analogSensor > sensorThres) {
        if (!smokeDetected) {
            smokeDetected = true;
            smokeDetectedTime = millis();
            Serial.println("FIRE DETECTED! Preparing to notify others...");
            state = STATE_TX;
        }
        if (millis() - smokeDetectedTime >= 5000) {
            Serial.println("Sending alarm to other detectors!");
            sprintf(txpacket, "ALARM");
            Radio.Send((uint8_t *)txpacket, strlen(txpacket));
            analogWrite(buzzer, 127);
        }
    } else if (!disableAlarm) {
        smokeDetected = false;
        analogWrite(buzzer, 0);
    }

    // Re-enable alarms after the disable period
    if (disableAlarm && millis() > disableEndTime) {
        disableAlarm = false;
        Serial.println("Alarms re-enabled.");
    }

    switch (state) {
    case STATE_TX:
        state = LOWPOWER;
        break;
    case STATE_RX:
        Radio.Rx(0);
        state = LOWPOWER;
        break;
    case STATE_ALARM:
        if (!disableAlarm) {
            Serial.println("ALARM RECEIVED! Activating buzzer...");
            analogWrite(buzzer, 127);
            delay(5000);
        }
        state = STATE_RX;
        break;
    case LOWPOWER:
        Radio.IrqProcess();
        break;
    default:
        break;
    }
}

void OnTxDone(void) {
    Serial.println("TX complete");
    state = STATE_RX;
}

void OnTxTimeout(void) {
    Radio.Sleep();
    Serial.println("TX Timeout...");
    state = STATE_RX;
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    memcpy(rxpacket, payload, size);
    rxpacket[size] = '\0';
    Serial.printf("Received packet: \"%s\" with RSSI %d\n", rxpacket, rssi);

    if (strcmp(rxpacket, "ALARM") == 0) {
        state = STATE_ALARM;
    } else {
        state = STATE_RX;
    }
    Radio.Sleep();
}

void printToScreen(String s) {
    Serial.println(s);
}