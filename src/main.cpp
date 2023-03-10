//  FARM DATA RELAY SYSTEM
//
//  GATEWAY 2.000
//
//  Developed by Timm Bogner (timmbogner@gmail.com) in Urbana, Illinois, USA.
//

#include <Arduino.h>
#include "fdrs_gateway_config.h"
#include <fdrs_gateway.h> 
#include <Adafruit_BMP280.h>                                                                                                                                                                                                              

time_t lastRunTime = 0;
uint8_t LoRaAddress[] = { 0xFF, 0xFF };
Adafruit_BMP280 bmp; // use I2C interface
Adafruit_Sensor *bmp_temp = bmp.getTemperatureSensor();
Adafruit_Sensor *bmp_pressure = bmp.getPressureSensor();
sensors_event_t temp_event, pressure_event;
// TwoWire twi;
unsigned long bmpmeasure = 0;

#define LED GPIO_NUM_19
#define PIRPIN GPIO_NUM_34
#define BATTPIN GPIO_NUM_35
#define LEDON 1
#define LEDOFF 0
#define PRESSOFFSET 0
#define TEMPOFFSET 0

void initPir(gpio_num_t pin) {
  gpio_config_t gpioPir;
  gpioPir.pin_bit_mask = ((1ULL<< pin));
  gpioPir.mode         = GPIO_MODE_INPUT;
  gpioPir.pull_up_en   = GPIO_PULLUP_DISABLE;
  gpioPir.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpioPir.intr_type    = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&gpioPir));
}

void initBatt(gpio_num_t pin) {
  gpio_config_t gpioBatt;
  gpioBatt.pin_bit_mask = ((1ULL<< pin));
  gpioBatt.mode         = GPIO_MODE_INPUT;
  gpioBatt.pull_up_en   = GPIO_PULLUP_DISABLE;
  gpioBatt.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpioBatt.intr_type    = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&gpioBatt));
}

// initialize LED on GPIO2 as output and Pull down
void initLed(gpio_num_t pin) {
  gpio_config_t gpioLed;
  gpioLed.pin_bit_mask = ((1ULL<< pin));
  gpioLed.mode         = GPIO_MODE_OUTPUT;
  gpioLed.pull_up_en   = GPIO_PULLUP_DISABLE;
  gpioLed.pull_down_en = GPIO_PULLDOWN_ENABLE;
  gpioLed.intr_type    = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&gpioLed));
}

void initBMP280() {
    bmp.begin();
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                    Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                    Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                    Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                    Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

    bmp_temp->printSensorDetails();

}
void loopBMP280() {
    if(millis() - bmpmeasure > 5000) {
        
        bmp_temp->getEvent(&temp_event);
        bmp_pressure->getEvent(&pressure_event);

        DBG("Temperature = " + String(temp_event.temperature * 1.8 + 32 + TEMPOFFSET) + " *F");
        DBG("Pressure = " + String(pressure_event.pressure * 0.0295 + PRESSOFFSET) + " inHg");
        bmpmeasure = millis();
    }
}

void loopPir() {
    bool pirCurrentState = 0;

    pirCurrentState = gpio_get_level(PIRPIN);

    if(gpio_get_level(PIRPIN) && pirCurrentState == 0) {
        gpio_set_level(LED, LEDON);
        DBG("PIR High");
        pirCurrentState = 1;
        gpio_set_level(LED, LEDON);
    }
    else if(!gpio_get_level(PIRPIN) && pirCurrentState == 1) {
        gpio_set_level(LED, LEDOFF);
        DBG("PIR Low.");
        pirCurrentState = 0;
    }
}


void setup() {
    beginFDRS();
    initLed(LED);
    initPir(PIRPIN);
    initBatt(BATTPIN);
    // initBMP280();
}

void loop() {
    loopFDRS();
    // Send time to LoRa broadcast address every 5 minutes.
    // LoRa controllers should receive time and report via serial
    if(millis() - lastRunTime > (1000 * 60 * 5)) {
        #ifdef USE_LORA
        DBG("Sending LoRa time.");
        timeFDRSLoRa(LoRaAddress);
        #endif
        lastRunTime = millis();
    }
    loopPir();
    // loopBMP280();
}