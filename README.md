# LoRa/Bluetooth Smoke Detector

I have made a set of 3 smart smoke detectors using Haltec WiFi LoRa v2 boards, MQ2 smoke sensors, and loud buzzers. The detectors are designed to communicate with each other allowing the smoke detectors to be activated either by sensing smoke themselves, or recieving a signal from one of the other detectors. The system uses LoRa to communicate between the smoke detectors, and BLE to communicate with a web app so that the detectors can be silenced if desired.

When smoke is detected, the detector activates its buzzer and sends a signal to the other using LoRa. This activates the other detectors. While the detectors are active, the user can click the silence button on the web app, this will turn off the alarms, and silence them completely for 30 seconds.
