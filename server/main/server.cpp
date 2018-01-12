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
#include <string.h>
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

#define SERVO_TIP_MIN_PULSEWIDTH 500 //Minimum pulse width in microsecond
#define SERVO_TIP_MAX_PULSEWIDTH 2000 //Maximum pulse width in microsecond
#define SERVO_TIP_MAX_DEGREE 1000 //Maximum angle in degree upto which servo can rotate

static xQueueHandle ble_to_servo_queue = NULL;
static xQueueHandle sequence_interrupt_queue = NULL;

#define SERVICE_UUID        "6d124ed1-50f5-4ebf-b490-c3db81cbaa8c"
#define CHARACTERISTIC_UUID "4c7a3456-6ac2-4e16-9951-028dc32c443c"

#define MAIN_SERVO_GPIO 18 //green, right on the front
#define TIP_SERVO_GPIO 19  //white, left on the front

static void mcpwm_example_gpio_initialize()
{
    printf("initializing mcpwm servo control gpio......\n");
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, MAIN_SERVO_GPIO);    //Set GPIO 18 as PWM0A, to which servo is connected
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0A, TIP_SERVO_GPIO);     //Set GPIO 19 as PWM0A, to which tip servo is connected
}


/*
 *
 	MAIN CAPTURED flat at:
	pulse width: 2132us
	pulse width: 2133us
	Angle of rotation: 1851

	MAIN CAPTURED UP at around:
	370-376 degrees, 986-992us

	TIP CAPTURED FLAT at:
	like 0 degrees, apparently....
	about 500us maybe

	TIP CAPTURED UP at:
	about 1455us, 635 degrees as of the latest iteration

 *
 */

/**
 * @brief Use this function to calcute pulse width for per degree rotation
 *
 * @param  degree_of_rotation the angle in degree to which servo has to rotate
 *
 * @return
 *     - calculated pulse width
 */
static uint32_t main_servo_per_degree_init(uint32_t degree_of_rotation)
{
    uint32_t cal_pulsewidth = 0;
    cal_pulsewidth = (SERVO_MIN_PULSEWIDTH + (((SERVO_MAX_PULSEWIDTH - SERVO_MIN_PULSEWIDTH) * (degree_of_rotation)) / (SERVO_MAX_DEGREE)));
    return cal_pulsewidth;
}

static uint32_t tip_servo_per_degree_init(uint32_t degree_of_rotation)
{
    uint32_t cal_pulsewidth = 0;
    cal_pulsewidth = (SERVO_TIP_MIN_PULSEWIDTH + (((SERVO_TIP_MAX_PULSEWIDTH - SERVO_TIP_MIN_PULSEWIDTH) * (degree_of_rotation)) / (SERVO_TIP_MAX_DEGREE)));
    return cal_pulsewidth;
}

//TODO active should probably something more sophisticated and thread-safe but whatever
static int active = false;
// Integer to track the current behavior state
static int state = -1;


static void sequence_tip_down()
{
	// TODO
//	uint32_t angle;
//	angle = servo_per_degree_init(10);
	mcpwm_set_duty_in_us(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_B, 0); //0 based on testing
	vTaskDelay(5);
}

// go home
static void sequence_home()
{
	state = 0;

	printf("Trying to run back\n");
//	uint32_t angle;
//	angle = main_servo_per_degree_init(10);
	mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 2051); //1951 based on testing
	vTaskDelay(5);
	sequence_tip_down();
}

static bool check_if_i_should_go_home()
{
	if (uxQueueMessagesWaiting(sequence_interrupt_queue) > 0)
	{
		// emergency interrupt
		printf("interrupting to go home!\n");
		sequence_home();
		// consume that message from the queue
		char value[1];
		xQueueReceive(sequence_interrupt_queue, &value, portMAX_DELAY);
		return true;
	}
	return false;
}

// raise up, slow
static void sequence_up_slow()
{
	state = 10;

	printf("Trying to go up slow\n");
	uint32_t angle, count;
	for (count = 376; count > 0; count--) {
		if (check_if_i_should_go_home()) {
			break;
		}
		angle = main_servo_per_degree_init(count);
		mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
	}

}

// raise up, fast
static void sequence_up_fast()
{
	state = 20;

	printf("Trying to go up fast\n");
//	uint32_t angle, count;
	mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 986); //arbitrary based on testing
//	for (count = 0; count < 1500; count++) { //guess at better max degree
//		if (check_if_i_should_go_home()) {
//			break;
//		}
//		angle = main_servo_per_degree_init(count);
//		mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
//	}

}

static void sequence_tip_up()
{
	state = 12;
	//TODO
	printf("Trying to flip the tip up\n");
	//TODO make this a waggle that then stays up
	uint32_t angle, count;
	for (count = 0; count < 635; count++) { //635 is arbitrary by testing
		if (check_if_i_should_go_home()) {
			break;
		}
		angle = tip_servo_per_degree_init(count);
		mcpwm_set_duty_in_us(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_B, angle);
		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
	}
}

// tremors
static void sequence_tremors()
{
	state = 30;

	//TODO I DON'T KNOW
}

static void servo_controller(void *arg)
{

	// Manages the operation graph
	printf("servo_controller started up\n");
	int state = -1; //tracks last executed state for operation graph

//	std::string value;
	char value[1];
	while(1) {

		xQueueReceive(ble_to_servo_queue, &value, portMAX_DELAY);
		printf("servo_controller message received:  %s\n", value);
		//interpret the signal and call the appropriate method

		if ( value[0] == 'A' )  {
			sequence_home();
		} else if ( value[0] == 'B' ) {
			active = true;
			if ( state == 10 || state == 20 ) { // meaning it's already in up_slow or up_fast
				sequence_tip_up();
			} else {
				sequence_up_slow();
			}
		} else if ( value[0] == 'C' ) {
			active = true;
			sequence_up_fast();
		} else if ( value[0] == 'D' ) {
			active = true;
			sequence_tremors();
		}
		active = false;
	}
}
/*

	INTERRUPT PATTERNS AND BEHAVIORS:
A(0) 	1 button depression (with 3 sec appx)- this is the “reset”, sends the tie back to it’s natural vertical state

B(10)	2  quick depressions- this sends the tie fairly slowly into a half erect state where the base in now horizontal
 B1(12)      -> 2 button depressions would make the tip flip up and stay (make it wave first?)
C(20)	3 quick depressions- basically the same as 2 depressions, just done quickly
			 -> 2 button depressions would make the tip flip up and stay (make it wave first?)
D(30)	1 button press and hold- this sends the tie into “tremors” for appx 3 sec where is just sorta wiggles back and forth.


 */



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


class MyCallbacks: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string value = pCharacteristic->getValue();
		if (value.length() > 0) {
			ESP_LOGD(LOG_TAG, "*********");
			ESP_LOGD(LOG_TAG, "New value: %.2x", value[0]);
			ESP_LOGD(LOG_TAG, "*********");
//			char val_clone[1];
//			strncpy(val_clone, value.c_str(), 1);

//			std::string val_clone = value.c_str();
			char val_array[1];
			strncpy(val_array, value.c_str(), 1);
			printf("Trying to deal with value: %s\n", val_array);
			// Sequence A should override all other sequences and goes into a special queue
			if ( value.compare("A") == 0 ) {
				if (active) {
					xQueueSendToBack(sequence_interrupt_queue, &val_array, portMAX_DELAY);
				} else {
					sequence_home();
				}
			} else {
				xQueueSendToBack(ble_to_servo_queue, &val_array, portMAX_DELAY);
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
	pAdvertisementData.setManufacturerData("datadatadata");
	pAdvertising->setAdvertisementData(pAdvertisementData);

	pAdvertising->start();
}


void app_main(void)
{


	//1. mcpwm gpio initialization
	mcpwm_example_gpio_initialize();
	sequence_interrupt_queue = xQueueCreate(10, sizeof(uint32_t));
	ble_to_servo_queue = xQueueCreate(10, sizeof(uint32_t));
	xTaskCreate(servo_controller, "servo_controller", 2048, NULL, 10, NULL);

	//2. initial mcpwm configuration
	printf("Configuring Initial Parameters of mcpwm......\n");
	mcpwm_config_t pwm_config;
	pwm_config.frequency = 50;    //frequency = 50Hz, i.e. for every servo motor time period should be 20ms
	pwm_config.cmpr_a = 0;    //duty cycle of PWMxA = 0
	pwm_config.cmpr_b = 0;    //duty cycle of PWMxb = 0
	pwm_config.counter_mode = MCPWM_UP_COUNTER;
	pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
	mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);    //Configure first servo with above settings
	mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_0, &pwm_config);    //Configure second servo with above settings

	run();
} // app_main


