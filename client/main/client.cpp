/*
 * 1. Open up the project properties
 * 2. Visit C/C++ General > Preprocessor Include Paths, Macros, etc
 * 3. Select the Providers tab
 * 4. Check the box for "CDT GCC Built-in Compiler Settings"
 * 5. Set the compiler spec command to "xtensa-esp32-elf-gcc ${FLAGS} -E -P -v -dD "${INPUTS}""
 * 6. Rebuild the index
*/

#include <esp_log.h>
#include <string>
#include <sstream>
//#include <sys/time.h>
#include "BLEDevice.h"

#include "BLEAdvertisedDevice.h"
#include "BLEClient.h"
#include "BLEScan.h"
#include "BLEUtils.h"
#include "Task.h"

// GPIO includes
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "sdkconfig.h"

static const char* LOG_TAG = "SampleClient";

// https://www.uuidgenerator.net/
// The remote service we wish to connect to.
static BLEUUID serviceUUID("6d124ed1-50f5-4ebf-b490-c3db81cbaa8c");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("4c7a3456-6ac2-4e16-9951-028dc32c443c");

// GPIO interrupt stuff
#define TEST_GPIO GPIO_NUM_18
static QueueHandle_t q1;

extern "C" {
	void app_main(void);
}

static void handler(void *args) {
	// log when handler received the interrupt and push that value into the queue
	TickType_t tick = xTaskGetTickCount();
	xQueueSendToBackFromISR(q1, &tick, NULL);
}


/**
 * Become a BLE client to a remote BLE server.  We are passed in the address of the BLE server
 * as the input parameter when the task is created.
 */
class MyClient: public Task {

	void run(void* data) {
		BLEAddress* pAddress = (BLEAddress*)data;
		BLEClient*  pClient  = BLEDevice::createClient();

		// Connect to the remove BLE Server.
		pClient->connect(*pAddress);

		// Obtain a reference to the service we are after in the remote BLE server.
		BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
		if (pRemoteService == nullptr) {
			ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", serviceUUID.toString().c_str());
			return;
		}


		// Obtain a reference to the characteristic in the service of the remote BLE server.
		BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
		if (pRemoteCharacteristic == nullptr) {
			ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", charUUID.toString().c_str());
			return;
		}

		// Read the value of the characteristic.
		std::string value = pRemoteCharacteristic->readValue();
		ESP_LOGD(LOG_TAG, "The characteristic value was: %s", value.c_str());

		// Time-based debounce values
		TickType_t event_tick;
		TickType_t last_event_tick = 0;

		while(1) {
			// Just straight up block on this method until we get an interrupt message on the queue
			xQueueReceive(q1, &event_tick, portMAX_DELAY);

			/*
			 * debounce approach
			 * On each interrupt:
			 *  if time_state is empty, set time_state to now and send value to server
			 *  else compare current time to time_state,
			 *    if < X, exit method
			 *    else if >= X, set time_state to now and send value to server
			 *
			 */
			if ( last_event_tick == 0 ) {
				ESP_LOGD(LOG_TAG, "Trying to write first hit @ %d", event_tick);
				last_event_tick = event_tick;
				pRemoteCharacteristic->writeValue("FF");
			} else {
				if ( event_tick - last_event_tick > 500 ) {
					ESP_LOGD(LOG_TAG, "Trying to write subsequent hit @ %d", event_tick);
					last_event_tick = event_tick;
					pRemoteCharacteristic->writeValue("FF");
				} else {
					// else do nothing because the window between hits is too small
					ESP_LOGD(LOG_TAG, "Missed @ %d", event_tick);
				}

			}
		}

		// UNREACHABLE... I think
		pClient->disconnect();

		ESP_LOGD(LOG_TAG, "%s", pClient->toString().c_str());
		ESP_LOGD(LOG_TAG, "-- End of task");
	} // run
}; // MyClient


/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
	/**
	 * Called for each advertising BLE server.
	 */
	void onResult(BLEAdvertisedDevice advertisedDevice) {
//		ESP_LOGW(LOG_TAG, "Advertised Device: %s", advertisedDevice.toString().c_str());

		if ( advertisedDevice.haveServiceUUID() ) {
			BLEUUID id = advertisedDevice.getServiceUUID();
			ESP_LOGD(LOG_TAG, "Evaluating Service ID: %s", id.toString().c_str());
		}
		if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
			advertisedDevice.getScan()->stop();

			ESP_LOGD(LOG_TAG, "Found our device!  address: %s", advertisedDevice.getAddress().toString().c_str());
			MyClient* pMyClient = new MyClient();
			pMyClient->setStackSize(18000);
			pMyClient->start(new BLEAddress(*advertisedDevice.getAddress().getNative()));
		} // Found our server
	} // onResult
}; // MyAdvertisedDeviceCallbacks

void app_main(void)
{

	// Configure GPIO pin first
	ESP_LOGD(LOG_TAG, ">> test1_task");

	q1 = xQueueCreate(10, sizeof(gpio_num_t));

	gpio_config_t gpioConfig;
	gpioConfig.pin_bit_mask = GPIO_SEL_18;
	gpioConfig.mode         = GPIO_MODE_INPUT;
	gpioConfig.pull_up_en   = GPIO_PULLUP_DISABLE;
	gpioConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
	gpioConfig.intr_type    = GPIO_INTR_POSEDGE;
	gpio_config(&gpioConfig);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(TEST_GPIO, handler, NULL);

	// BLE scan init
	ESP_LOGD(LOG_TAG, "Scanning sample starting");
	BLEDevice::init("ESP32-Client");
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true);
	pBLEScan->start(30);
}

