/*
 * 1. Open up the project properties
 * 2. Visit C/C++ General > Preprocessor Include Paths, Macros, etc
 * 3. Select the Providers tab
 * 4. Check the box for "CDT GCC Built-in Compiler Settings"
 * 5. Set the compiler spec command to "xtensa-esp32-elf-gcc ${FLAGS} -E -P -v -dD "${INPUTS}""
 * 6. Rebuild the index
*/
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include <esp_log.h>
#include <string>
#include <Task.h>


#include "sdkconfig.h"


//static char tag[]="cpp_helloworld";
static char LOG_TAG[] = "SampleServer";

extern "C" {
	void app_main(void);
}


class MainBLEServer: public Task {
	void run(void *data) {
		ESP_LOGD(LOG_TAG, "Starting BLE work!");

		BLEDevice::init("ESP32-Server");
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

//class Greeting {
//public:
//	void helloEnglish() {
//		ESP_LOGD(tag, "Hello %s", name.c_str());
//	}
//
//	void helloFrench() {
//		ESP_LOGD(tag, "Bonjour %s", name.c_str());
//	}
//
//	void setName(std::string name) {
//		this->name = name;
//	}
//private:
//	std::string name = "";
//
//};

void app_main(void)
{
//	Greeting myGreeting;
//	myGreeting.setName("Neil");
//	myGreeting.helloEnglish();
//	myGreeting.helloFrench();

	//esp_log_level_set("*", ESP_LOG_DEBUG);
	printf("Server Entry Point Test....???");
	MainBLEServer* pMainBleServer = new MainBLEServer();
	pMainBleServer->setStackSize(20000);
	pMainBleServer->start();
}

