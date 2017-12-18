/*
 * 1. Open up the project properties
 * 2. Visit C/C++ General > Preprocessor Include Paths, Macros, etc
 * 3. Select the Providers tab
 * 4. Check the box for "CDT GCC Built-in Compiler Settings"
 * 5. Set the compiler spec command to "xtensa-esp32-elf-gcc ${FLAGS} -E -P -v -dD "${INPUTS}""
 * 6. Rebuild the index
 *
 * Software Debounce via http://www.eng.utah.edu/~cs5780/debouncing.pdf
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

// The remote service we wish to connect to.
static BLEUUID serviceUUID("6d124ed1-50f5-4ebf-b490-c3db81cbaa8c");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("4c7a3456-6ac2-4e16-9951-028dc32c443c");

// GPIO interrupt stuff
//#define TEST_GPIO GPIO_NUM_18
//static QueueHandle_t q1;

#define ESP_INTR_FLAG_DEFAULT 0

#define GPIO_NC GPIO_NUM_4
#define GPIO_NO GPIO_NUM_5
#define GPIO_INPUT_PIN_SEL ((1<<GPIO_NO) | (1<<GPIO_NC))

//queue to hear all button noise
static xQueueHandle gpio_evt_nc_queue = NULL;
//queue to hear all confirmed short presses
static xQueueHandle short_evt_queue = NULL;
//queue to hear all outgoing messages (as button sequences are confirmed and sent out)
static xQueueHandle outgoing_queue = NULL;


extern "C" {
	void app_main(void);
}

static void gpio_isr_handler(void* arg)
{
	TickType_t tick = xTaskGetTickCount();
	xQueueSendFromISR(gpio_evt_nc_queue, &tick, NULL);
}

static void short_counter(void *arg)
{
	TickType_t event_tick;
	uint16_t messages_waiting;
	for (;;) {
		if (xQueueReceive(short_evt_queue, &event_tick, portMAX_DELAY)) {
				//after the first message is received, block this thread for N ticks and see how many messages have accumulated
				vTaskDelay(1000);
				messages_waiting = uxQueueMessagesWaiting(short_evt_queue);
				int i;
				for ( i = 0; i < messages_waiting; i = i+1 )
				{
					// TODO maybe check them for their actual time values...
					xQueueReceive(short_evt_queue, &event_tick, portMAX_DELAY);
				}
				messages_waiting = messages_waiting + 1;
				printf("I think there were %d button pushes\n", messages_waiting);
				if ( messages_waiting < 4 ) { //4+ short presses would be noise
					xQueueSendToBack(outgoing_queue, &messages_waiting, portMAX_DELAY);
				}
		}
	}
}

static void gpio_task_example(void* arg)
{
	TickType_t begin_event_tick = 0;
	TickType_t event_tick;

	uint32_t level_nc = gpio_get_level(GPIO_NC);
	uint32_t level_no = gpio_get_level(GPIO_NO);
	bool button_state = false;
	bool state = false;
	printf("NC initially registered as: %d\n", level_nc);
    for(;;) {

    	if(xQueueReceive(gpio_evt_nc_queue, &event_tick, portMAX_DELAY)) {
//    		printf("Somebody hit @ %d\n", event_tick);
    		level_nc = gpio_get_level(GPIO_NC);
    		if (level_nc) {
    			state = true;
    		}
    		level_no = gpio_get_level(GPIO_NO);
    		if (level_no) {
    			state = false;
    		}
    		if ( state != button_state )
    		{
    			//capture time and whether this is the beginning or end of a push
    			button_state = state;
    			if ( button_state ) {
    				begin_event_tick = event_tick; // capture start moment
    			} else {
    				uint32_t elapsed = event_tick - begin_event_tick;
    				if ( elapsed > 10 && elapsed < 500 )
    				{
    					printf("short press finished\n");
    					xQueueSendToBack(short_evt_queue, &event_tick, portMAX_DELAY);
    				} else if ( elapsed >= 500 ){
    					// TODO launch (D)
    					printf("long press finished\n");
    					uint16_t msg_code = 4;
    					xQueueSendToBack(outgoing_queue, &msg_code, portMAX_DELAY);
    				}
    				// things happening in sub-10 ticks are noise
    			}
    		}
    	}
    }
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
			ESP_LOGW(LOG_TAG, "Failed to find our service UUID: %s", serviceUUID.toString().c_str());
			return;
		}


		// Obtain a reference to the characteristic in the service of the remote BLE server.
		BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
		if (pRemoteCharacteristic == nullptr) {
			ESP_LOGW(LOG_TAG, "Failed to find our characteristic UUID: %s", charUUID.toString().c_str());
			return;
		}

		// Read the value of the characteristic.
		std::string value = pRemoteCharacteristic->readValue();
		ESP_LOGW(LOG_TAG, "The characteristic value was: %s", value.c_str());


//		TickType_t begin_event_tick = 0;
//		TickType_t event_tick;
//
//		uint32_t level_nc = gpio_get_level(GPIO_NC);
//		uint32_t level_no = gpio_get_level(GPIO_NO);
//		bool button_state = false;
//		bool state = false;
//		printf("NC initially registered as: %d\n", level_nc);
//		for(;;) {
//
//			if(xQueueReceive(gpio_evt_nc_queue, &event_tick, portMAX_DELAY)) {
//	//    		printf("Somebody hit @ %d\n", event_tick);
//				level_nc = gpio_get_level(GPIO_NC);
//				if (level_nc) {
//					state = true;
//				}
//				level_no = gpio_get_level(GPIO_NO);
//				if (level_no) {
//					state = false;
//				}
//				if ( state != button_state )
//				{
//					//capture time and whether this is the beginning or end of a push
//					button_state = state;
//					if ( button_state ) {
//						begin_event_tick = event_tick; // capture start moment
//					} else {
//						uint32_t elapsed = event_tick - begin_event_tick;
//						if ( elapsed > 10 && elapsed < 500 )
//						{
//							printf("short press finished\n");
//							xQueueSendToBack(short_evt_queue, &event_tick, NULL);
//						} else if ( elapsed >= 500 ){
//							// TODO launch (D)
//							pRemoteCharacteristic->writeValue("msg_D");
//							printf("long press finished\n");
//						}
//						// things happening in sub-10 ticks are noise
//					}
//				}
//			}
//		}



		// Time-based debounce values
//		TickType_t event_tick;
//		TickType_t last_event_tick = 0;
//
		uint16_t msg_code;
		while(1) {
//			// Just straight up block on this method until we get an interrupt message on the queue
			xQueueReceive(outgoing_queue, &msg_code, portMAX_DELAY);
			if (msg_code == 1) {
				printf("writing A\n");
				pRemoteCharacteristic->writeValue("A");
			} else if (msg_code == 2) {
				printf("writing B\n");
				pRemoteCharacteristic->writeValue("B");
			} else if (msg_code == 3) {
				printf("writing C\n");
				pRemoteCharacteristic->writeValue("C");
			} else if (msg_code == 4) {
				printf("writing D\n");
				pRemoteCharacteristic->writeValue("D");
			}
		}
//
//			/*
//			 * debounce approach
//			 * On each interrupt:
//			 *  if time_state is empty, set time_state to now and send value to server
//			 *  else compare current time to time_state,
//			 *    if < X, exit method
//			 *    else if >= X, set time_state to now and send value to server
//			 *
//			 */
//			if ( last_event_tick == 0 ) {
//				ESP_LOGD(LOG_TAG, "Trying to write first hit @ %d", event_tick);
//				last_event_tick = event_tick;
//				pRemoteCharacteristic->writeValue("FF");
//			} else {
//				if ( event_tick - last_event_tick > 500 ) {
//					ESP_LOGD(LOG_TAG, "Trying to write subsequent hit @ %d", event_tick);
//					last_event_tick = event_tick;
//					pRemoteCharacteristic->writeValue("FF");
//				} else {
//					// else do nothing because the window between hits is too small
//					ESP_LOGD(LOG_TAG, "Missed @ %d", event_tick);
//				}
//
//			}
//		}

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
			ESP_LOGW(LOG_TAG, "Evaluating Service ID: %s", id.toString().c_str());
		}
		if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
			advertisedDevice.getScan()->stop();

			ESP_LOGW(LOG_TAG, "Found our device!  address: %s", advertisedDevice.getAddress().toString().c_str());
			MyClient* pMyClient = new MyClient();
			pMyClient->setStackSize(18000);
			pMyClient->start(new BLEAddress(*advertisedDevice.getAddress().getNative()));
		} // Found our server
	} // onResult
}; // MyAdvertisedDeviceCallbacks

void app_main(void)
{

	// Configure GPIO pin first
	ESP_LOGW(LOG_TAG, ">> test1_task");

//	q1 = xQueueCreate(10, sizeof(gpio_num_t));
//
//	gpio_config_t gpioConfig;
//	gpioConfig.pin_bit_mask = GPIO_SEL_18;
//	gpioConfig.mode         = GPIO_MODE_INPUT;
//	gpioConfig.pull_up_en   = GPIO_PULLUP_DISABLE;
//	gpioConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
//	gpioConfig.intr_type    = GPIO_INTR_POSEDGE;
//	gpio_config(&gpioConfig);
//
//	gpio_install_isr_service(0);
//	gpio_isr_handler_add(TEST_GPIO, handler, NULL);

	gpio_config_t gpioConfig;
	gpioConfig.pin_bit_mask = GPIO_INPUT_PIN_SEL;
	gpioConfig.mode         = GPIO_MODE_INPUT;
	gpioConfig.pull_up_en   = GPIO_PULLUP_ENABLE;
	gpioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpioConfig.intr_type    = GPIO_INTR_NEGEDGE;
	gpio_config(&gpioConfig);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(GPIO_NC, gpio_isr_handler, NULL);
	gpio_isr_handler_add(GPIO_NO, gpio_isr_handler, NULL);

	//create a queue to handle gpio event from isr
	gpio_evt_nc_queue = xQueueCreate(10, sizeof(uint32_t));
	short_evt_queue = xQueueCreate(10, sizeof(uint32_t));
	outgoing_queue = xQueueCreate(10, sizeof(uint32_t));

	//start gpio task
	xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);
	xTaskCreate(short_counter, "short_counter", 2048, NULL, 10, NULL);

	// BLE scan init
	ESP_LOGW(LOG_TAG, "Scanning sample starting");
	BLEDevice::init("ESP32-Client");
	BLEScan *pBLEScan = BLEDevice::getScan();
	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(true);
	pBLEScan->start(30);
}


/*

	INTERRUPT PATTERNS AND BEHAVIORS:
A 	1 button depression (with 3 sec appx)- this is the “reset”, sends the tie back to it’s natural vertical state
 		PROBABLY: press continuously for 1 second and then release. From 1 second to let's say 3, there is a dead window.
 				  after which we enter into D
B	2  quick depressions- this sends the tie fairly slowly into a half erect state where the base in now horizontal
 B1      -> 2 button depressions would make the tip flip up and stay (make it wave first?)
C	3 quick depressions- basically the same as 2 depressions, just done quickly
D	1 button press and hold- this sends the tie into “tremors” for appx 3 sec where is just sorta wiggles back and forth.

	THINGS TO DETECT:
	Long press for 1-3 seconds
	Long press for 3+ seconds
	2 quick depression (let's say each closed < 500ms with < 500ms between them)
	3 quick depression (same spacing as 2)

ALL OF THIS SUCKS MY ASS BECAUSE OF THE NEED TO DEBOUNCE THE ACTUAL SWITCH


 */






