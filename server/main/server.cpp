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
	printf("pulse width: %dus\n", angle);
	// probably don't iterate just jump back to flat, so figure out whatever that value is?
	mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
	vTaskDelay(5);
//	for (count = 0; count < SERVO_MAX_DEGREE; count++) {
//		printf("Angle of rotation: %d\n", count);
//		angle = servo_per_degree_init(count);
//		printf("pulse width: %dus\n", angle);
//		mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, angle);
//		vTaskDelay(5);     //Add delay, since it takes time for servo to rotate, generally 100ms/60degree rotation at 5V
//	}
}

static uint16_t state = 0;
/*
 * I'm thinking this one should probably call into a thread that can be interrupted?
 * For now it can just manage a non-volatile state, since I'm pretty sure multiple onWrites won't blow anything up?
 * might cause weird behavior.
 * SUBSEQUENT BUTTON PUSHES ARE EFFECTIVELY QUEUED/BLOCKED
 *
 * 0 is uninitialized, so set to 1, go forward (0,2 may be redundant but I may want to treat startup differently?)
 * 1 is go forward, so set to 2, go backwards
 * 2 is go backward, so set to 1, go forwards
 */
static void run_servo_routine()
{
	if ( state == 0 ) {
		state = 1;
		run_forward();
	} else if ( state == 1 ) {
		state = 2;
		run_backward();
	} else {
		state = 1;
		run_forward();
	}
}


class MyCallbacks: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string value = pCharacteristic->getValue();
		if (value.length() > 0) {
			ESP_LOGD(LOG_TAG, "*********");
			ESP_LOGD(LOG_TAG, "New value: %.2x", value[0]);
			ESP_LOGD(LOG_TAG, "*********");

			run_servo_routine();
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


