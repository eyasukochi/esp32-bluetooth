/**
 * Create a BLE Server such that when a client connects and requests a change to the characteristic
 * value, the callback associated with the server will be invoked such that the server can perform
 * some action based on the new value.  The action in this sample is merely to log the new value to
 * the console.
 */
#include "BLEUtils.h"
#include "BLEServer.h"
#include <esp_log.h>
#include <string>
#include <sys/time.h>
#include <sstream>
#include "BLEDevice.h"

#include "sdkconfig.h"

static char LOG_TAG[] = "SampleWrite";

extern "C" {
void app_main(void);
}

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "6d124ed1-50f5-4ebf-b490-c3db81cbaa8c"
#define CHARACTERISTIC_UUID "4c7a3456-6ac2-4e16-9951-028dc32c443c"

class MyCallbacks: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string value = pCharacteristic->getValue();
		if (value.length() > 0) {
				ESP_LOGD(LOG_TAG, "*********");
				ESP_LOGD(LOG_TAG, "New value: %.2x", value[0]);
				ESP_LOGD(LOG_TAG, "*********");
			}
		}
};


static void run() {
	BLEDevice::init("MYDEVICE");
	BLEServer *pServer = BLEDevice::createServer();

	BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID));

	BLECharacteristic *pCharacteristic = pService->createCharacteristic(
		BLEUUID(CHARACTERISTIC_UUID),
		BLECharacteristic::PROPERTY_BROADCAST | BLECharacteristic::PROPERTY_READ  |
		BLECharacteristic::PROPERTY_NOTIFY    | BLECharacteristic::PROPERTY_WRITE |
		BLECharacteristic::PROPERTY_INDICATE
	);

	pCharacteristic->setCallbacks(new MyCallbacks());

	pCharacteristic->setValue("Hello World");

	pService->start();

	BLEAdvertising *pAdvertising = pServer->getAdvertising();
	// As it turns out, the default Advertising Object from the Service, even with the UUID set doesn't form all of the Ad Types that the Client was expecting
	// Manually constructing the advertisement data does set the expected Ad types and then everything seems to work.
	BLEAdvertisementData pAdvertisementData;
	pAdvertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));
//	pAdvertisementData.setFlags(06);
	pAdvertisementData.setName("A_Great_Advertisement");
	pAdvertisementData.setManufacturerData("F_YO_SELF");
	pAdvertising->setAdvertisementData(pAdvertisementData);

	pAdvertising->start();
}


void app_main(void)
{
	run();
} // app_main


