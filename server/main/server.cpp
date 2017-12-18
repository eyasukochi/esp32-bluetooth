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
//#include <sys/time.h>
#include <sstream>
#include "BLEDevice.h"

// Servo PWM stuff
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_attr.h"

#include "driver/mcpwm.h"
#include "soc/mcpwm_reg.h"
#include "soc/mcpwm_struct.h"


#include "sdkconfig.h"

static char LOG_TAG[] = "SampleWrite";

extern "C" {
void app_main(void);
}

#define SERVO_MIN_PULSEWIDTH 700 //Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH 2250 //Maximum pulse width in microsecond
#define SERVO_MAX_DEGREE 2000 //Maximum angle in degree upto which servo can rotate

static xQueueHandle sequence_interrupt_queue = NULL;

#define SERVICE_UUID        "6d124ed1-50f5-4ebf-b490-c3db81cbaa8c"
#define CHARACTERISTIC_UUID "4c7a3456-6ac2-4e16-9951-028dc32c443c"


static void mcpwm_example_gpio_initialize()
{
    printf("initializing mcpwm servo control gpio......\n");
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, 18);    //Set GPIO 18 as PWM0A, to which servo is connected
}

/**
 * @brief Use this function to calcute pulse width for per degree rotation
 *
 * @param  degree_of_rotation the angle in degree to which servo has to rotate
 *
 * @return
 *     - calculated pulse width
 */
static uint32_t servo_per_degree_init(uint32_t degree_of_rotation)
{
    uint32_t cal_pulsewidth = 0;
    cal_pulsewidth = (SERVO_MIN_PULSEWIDTH + (((SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * (degree_of_rotation)) / (SERVO_MAX_DEGREE)));
    return cal_pulsewidth;
}

static void run_forward()
{
	printf("Trying to run forward\n");
	uint32_t angle, count;
	for (count = 0; count < SERVO_MAX_DEGREE; count++) {
//		printf("Angle of rotation: %d\n", count);
		angle = servo_per_degree_init(count);
//		printf("pulse width: %dus\n", angle);
		mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
	}
}

static void run_backward()
{
	printf("Trying to run back\n");
	uint32_t angle;
	angle = servo_per_degree_init(10);
//	printf("pulse width: %dus\n", angle);
	// probably don't iterate just jump back to flat, so figure out whatever that value is?
	mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
	vTaskDelay(5);
}

static bool active = false;

// go home
static void sequence_home()
{
	printf("Trying to run back\n");
	uint32_t angle;
	angle = servo_per_degree_init(10);
//	printf("pulse width: %dus\n", angle);
	// probably don't iterate just jump back to flat, so figure out whatever that value is?
	mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
	vTaskDelay(5);
}

// raise up, slow
static void sequence_up_slow()
{
	active = true;
	uint32_t angle, count;
	for (count = 0; count < SERVO_MAX_DEGREE; count++) {
//		printf("Angle of rotation: %d\n", count);
		angle = servo_per_degree_init(count);
//		printf("pulse width: %dus\n", angle);
		mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
	}
	active = false;
}

// raise up, fast
static void sequence_up_fast()
{
	active = true;
	uint32_t angle, count;
	for (count = 0; count < 1500; count++) { //guess at better max degree
		if (uxQueueMessagesWaiting(sequence_interrupt_queue) > 0)
		{
			// emergency interrupt
			sequence_home();
			// consume that message from the queue
			std::string value;
			xQueueReceive(sequence_interrupt_queue, &value, portMAX_DELAY);
			break;
		}
		angle = servo_per_degree_init(count);
//		printf("Angle of rotation: %d\n", count);
//		printf("pulse width: %dus\n", angle);
		mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
	}
	active = false;
}

static void sequence_tip_up()
{
	//TODO make this a waggle that then stays up
}

static void sequence_tip_down()
{
	//TODO, because NA
}

// tremors
static void sequence_tremors()
{

}

/*

	INTERRUPT PATTERNS AND BEHAVIORS:
A 	1 button depression (with 3 sec appx)- this is the “reset”, sends the tie back to it’s natural vertical state

B	2  quick depressions- this sends the tie fairly slowly into a half erect state where the base in now horizontal
 B1      -> 2 button depressions would make the tip flip up and stay (make it wave first?)
C	3 quick depressions- basically the same as 2 depressions, just done quickly
D	1 button press and hold- this sends the tie into “tremors” for appx 3 sec where is just sorta wiggles back and forth.


 */

class MyCallbacks: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string value = pCharacteristic->getValue();
		if (value.length() > 0) {
			ESP_LOGD(LOG_TAG, "*********");
			ESP_LOGD(LOG_TAG, "New value: %.2x", value[0]);
			ESP_LOGD(LOG_TAG, "*********");

			//TODO push the received hex value into another queue which is checked by all routines continously for messages?
			/*
			 * That's a little hectic since there are two states,
			 * one where somebody needs to start it cold
			 * and one where somebody is midway through and receives an interrupt to go home
			 * So a global "active" state needs to be maintained.
			 *
			 * Could detect which state was running, and if there is an active state, push messages into a special interrupt queue which is continously checked
			 * so that way you can launch by calling the method and then interrupt them with the special ones (like home, probably)
			 */

			if ( active ){

			} else {
				//interpret the signal and call the appropriate method
				if ( value.compare("A") == 0 ) {
					sequence_home();
				} else if ( value.compare("B") == 0 ) {
					sequence_up_slow();
				} else if ( value.compare("C") == 0 ) {
					sequence_up_fast();
				} else if ( value.compare("D") == 0 ) {
					sequence_tremors();
				}
			}
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
	pAdvertisementData.setName("A_Great_Advertisement");
	pAdvertisementData.setManufacturerData("F_YO_SELF");
	pAdvertising->setAdvertisementData(pAdvertisementData);

	pAdvertising->start();
}


void app_main(void)
{

	//1. mcpwm gpio initialization
	mcpwm_example_gpio_initialize();
	sequence_interrupt_queue = xQueueCreate(10, sizeof(uint32_t));

	//2. initial mcpwm configuration
	printf("Configuring Initial Parameters of mcpwm......\n");
	mcpwm_config_t pwm_config;
	pwm_config.frequency = 50;    //frequency = 50Hz, i.e. for every servo motor time period should be 20ms
	pwm_config.cmpr_a = 0;    //duty cycle of PWMxA = 0
	pwm_config.cmpr_b = 0;    //duty cycle of PWMxb = 0
	pwm_config.counter_mode = MCPWM_UP_COUNTER;
	pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
	mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);    //Configure PWM0A & PWM0B with above settings


	run();
} // app_main


