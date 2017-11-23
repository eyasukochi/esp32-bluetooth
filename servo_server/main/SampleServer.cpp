/*
 * server_test.cpp
 *
 * Create a new BLE server.
 *
 *  Created on: Nov 19, 2017
 *      Author: ellison
 */
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include <esp_log.h>
#include <string>
#include <Task.h>


#include "sdkconfig.h"

static char LOG_TAG[] = "SampleServer";

class MainBLEServer: public Task {
	void run(void *data) {
		ESP_LOGD(LOG_TAG, "Starting BLE work!");

		BLEDevice::init("ESP32");
		BLEServer* pServer = BLEDevice::createServer();

		BLEService* pService = pServer->createService("6d124ed1-50f5-4ebf-b490-c3db81cbaa8c");

		BLECharacteristic* pCharacteristic = pService->createCharacteristic(
			BLEUUID("4c7a3456-6ac2-4e16-9951-028dc32c443c"),
			BLECharacteristic::PROPERTY_BROADCAST | BLECharacteristic::PROPERTY_READ  |
			BLECharacteristic::PROPERTY_NOTIFY    | BLECharacteristic::PROPERTY_WRITE |
			BLECharacteristic::PROPERTY_INDICATE
		);

		pCharacteristic->setValue("Hello World!");

		BLE2902* p2902Descriptor = new BLE2902();
		p2902Descriptor->setNotifications(true);
		pCharacteristic->addDescriptor(p2902Descriptor);

		pService->start();

		BLEAdvertising* pAdvertising = pServer->getAdvertising();
		pAdvertising->addServiceUUID(BLEUUID(pService->getUUID()));
		pAdvertising->start();

		ESP_LOGD(LOG_TAG, "Advertising started!");
		delay(1000000);
	}
};


void SampleServer(void)
{
	//esp_log_level_set("*", ESP_LOG_DEBUG);
	MainBLEServer* pMainBleServer = new MainBLEServer();
	pMainBleServer->setStackSize(20000);
	pMainBleServer->start();

} // app_main



