// Import libraries
#include <Wire.h>
#include <SPI.h>
#include "LcdPanel.h"
#include "MemoryFree.h"
#include "Watchdog.h"
#include "DHT.h"
#include "DS18B20.h"
#include "BH1750.h"
#include "nRF24L01.h"
#include "RF24.h"
#include "RF24Layer2.h"
#include "MeshNet.h"

// Declare output
static int serial_putchar(char c, FILE *) {
  Serial.write(c);
  return 0;
};
FILE serial_out = {0};

// Declare Lcd Panel
LcdPanel panel;

// Declare MESH network
const uint32_t deviceType = 001;
uint32_t deviceUniqueId = 100002;
static const uint8_t CE_PIN = 9;
static const uint8_t CS_PIN = 10;
const uint8_t RF24_INTERFACE = 0;
const int NUM_INTERFACES = 1;
// Declare radio
RF24 radio(CE_PIN, CS_PIN);

// Declare DHT sensor
#define DHTTYPE DHT11

// Declare variables
uint16_t timerFast = 0;
uint16_t timerSlow = 0;
uint16_t lastMisting = 0;
uint16_t lastWatering = 0;
uint16_t sunrise = 0;
uint8_t startMisting = 0;
uint16_t startWatering = 0;
bool substTankFull = false;
// Define pins
static const uint8_t DHTPIN = 3;
static const uint8_t ONE_WIRE_BUS = 2;
static const uint8_t SUBSTRATE_FULLPIN = A0;
static const uint8_t SUBSTRATE_DELIVEREDPIN = A1;
static const uint8_t WATER_LEVELPIN = A6;
static const uint8_t SUBSTRATE_LEVELPIN = A7;
static const uint8_t PUMP_WATERINGPIN = 4;
static const uint8_t PUMP_MISTINGPIN = 5;
static const uint8_t LAMPPIN = 7;

/****************************************************************************/

//
// Setup
//
void setup()
{
  // Configure output
  Serial.begin(9600);
  fdev_setup_stream(&serial_out, serial_putchar, NULL, _FDEV_SETUP_WRITE);
  stdout = stderr = &serial_out;
  // prevent continiously restart
  delay(500);
  // restart if memory lower 512 bytes
  softResetMem(512);
  // restart after freezing for 8 sec
  softResetTimeout();
  // initialize network
  rf24init();
  // initialize lcd panel
  panel.begin();
}

//
// Loop
//
void loop()
{
  // watchdog
  heartbeat();
  // timer fo 1 sec
  if((millis()/1000) - timerFast >= 1) {
    timerFast = millis()/1000;
    // check level sensors
    check_levels();
    // update watering
    watering();
    // update misting
    misting();
    // timer fo 1 min
    if((millis()/1000) - timerSlow >= 60) {
      timerSlow = millis()/1000;
      // system check
      check();
      // manage light
      doLight();
      // manage misting and watering
      doWork();
      // save settings
      if(storage.changed && storage.ok) {
        // WARNING: EEPROM can burn!
        storage.save();
        storage.changed = false;
      }
      // send data to base
      sendCommand( 1, (void*) &"Hydroponica", sizeof("Hydroponica") );
      sendCommand( HUMIDITY, (void*) rtc.readnvram(HUMIDITY), 
        sizeof(rtc.readnvram(HUMIDITY)) );
      sendCommand( AIR_TEMP, (void*) rtc.readnvram(AIR_TEMP), 
        sizeof(rtc.readnvram(AIR_TEMP)) );
      sendCommand( COMPUTER_TEMP, (void*) rtc.readnvram(COMPUTER_TEMP),
        sizeof(rtc.readnvram(COMPUTER_TEMP)) );
      sendCommand( SUBSTRATE_TEMP, (void*) rtc.readnvram(SUBSTRATE_TEMP), 
        sizeof(rtc.readnvram(SUBSTRATE_TEMP)) );    
      sendCommand( LIGHT, (void*) rtc.readnvram(LIGHT), 
        sizeof(rtc.readnvram(LIGHT)) );
      sendCommand( PUMP_MISTING, (void*) rtc.readnvram(PUMP_MISTING), 
        sizeof(rtc.readnvram(PUMP_MISTING)) );
      sendCommand( PUMP_WATERING, (void*) rtc.readnvram(PUMP_WATERING), 
        sizeof(rtc.readnvram(PUMP_WATERING)) );
      sendCommand( LAMP, (void*) rtc.readnvram(LAMP), 
        sizeof(rtc.readnvram(LAMP)) );
      sendCommand( WARNING, (void*) rtc.readnvram(WARNING), 
        sizeof(rtc.readnvram(WARNING)) );
      sendCommand( ERROR, (void*) rtc.readnvram(ERROR), 
        sizeof(rtc.readnvram(ERROR)) );
    }
  }
  // update LCD 
  panel.update();
  // update network
  rf24receive();
}

/****************************************************************************/

// Pass a layer3 packet to the layer2 of MESH network
int sendPacket(uint8_t* message, uint8_t len, 
    uint8_t interface, uint8_t macAddress) {  
  // Here should be called the layer2 function corresponding to interface
  if(interface == RF24_INTERFACE) {
    rf24sendPacket(message, len, macAddress);
    return 1;
  }
  return 0;
}

void onCommandReceived(uint8_t command, void* data, uint8_t dataLen) {
  #ifdef DEBUG_MESH
    printf_P(PSTR("MESH: INFO: Received %d, %d\n\r"), command, data);
  #endif
}

/****************************************************************************/

bool read_DHT() {
  DHT dht(DHTPIN, DHTTYPE);
  dht.begin();
  rtc.writenvram(HUMIDITY, dht.readHumidity());
  rtc.writenvram(AIR_TEMP, dht.readTemperature());

  if( isnan(rtc.readnvram(HUMIDITY)) || isnan(rtc.readnvram(AIR_TEMP)) ) {
    #ifdef DEBUG_DHT11
      printf_P(PSTR("DHT11: Error: Communication failed!\n\r"));
    #endif
    return false;
  }
  #ifdef DEBUG_DHT11
    printf_P(PSTR("DHT11: Info: Air humidity: %d, temperature: %dC.\n\r"), 
      rtc.readnvram(HUMIDITY), rtc.readnvram(AIR_TEMP));
  #endif
  return true;
}

bool read_DS18B20() {
  DS18B20 ds(ONE_WIRE_BUS);
  
  int value = ds.read(0);
  if(value == DS_DISCONNECTED) {
    #ifdef DEBUG_DS18B20
      printf_P(PSTR("DS18B20: Error: Computer sensor communication failed!\n\r"));
    #endif
    return false;
  }
  #ifdef DEBUG_DS18B20
    printf_P(PSTR("DS18B20: Info: Computer temperature: %dC.\n\r"), value);
  #endif
  rtc.writenvram(COMPUTER_TEMP, value);

  value = ds.read(1);
  if(value == DS_DISCONNECTED) {
    #ifdef DEBUG_DS18B20
      printf_P(PSTR("DS18B20: Error: Substrate sensor communication failed!\n\r"));
    #endif
    return false;
  }
  #ifdef DEBUG_DS18B20
    printf_P(PSTR("DS18B20: Info: Substrate temperature: %dC.\n\r"), value);
  #endif
  rtc.writenvram(SUBSTRATE_TEMP, value);
  return true;
}

bool read_BH1750() {
  BH1750 lightMeter;
  lightMeter.begin(BH1750_ONE_TIME_HIGH_RES_MODE_2);
  uint16_t value = lightMeter.readLightLevel();

  if(value < 0) {
    #ifdef DEBUG_BH1750
      printf_P(PSTR("BH1750: Error: Light sensor communication failed!\n\r"));
    #endif
    return false;
  }
  #ifdef DEBUG_BH1750
    printf_P(PSTR("BH1750: Info: Light intensity: %d.\n\r"), value);
  #endif
  rtc.writenvram(LIGHT, value);
  return true;
}

void check_levels() {
  // no pull-up for A6 and A7
  pinMode(SUBSTRATE_LEVELPIN, INPUT);
  if(analogRead(SUBSTRATE_LEVELPIN) > 700) {
    rtc.writenvram(ERROR, ERROR_NO_SUBSTRATE);
    return;
  }
  pinMode(SUBSTRATE_DELIVEREDPIN, INPUT_PULLUP);
  if(digitalRead(SUBSTRATE_DELIVEREDPIN) == 1) {
    rtc.writenvram(WARNING, INFO_SUBSTRATE_DELIVERED);
    return;
  }
  // no pull-up for A6 and A7
  pinMode(WATER_LEVELPIN, INPUT);
  if(analogRead(WATER_LEVELPIN) > 700) {
    rtc.writenvram(WARNING, WARNING_NO_WATER);
    return;
  }
  pinMode(SUBSTRATE_FULLPIN, INPUT_PULLUP);
  if(digitalRead(SUBSTRATE_FULLPIN) == 1) { 	  
  	if(substTankFull == false) {
      substTankFull = true;
      rtc.writenvram(WARNING, INFO_SUBSTRATE_FULL);
      return;
    }
  } else {
  	substTankFull = false;
  }
}

void relayOn(uint8_t relay) {
  if(rtc.readnvram(relay)) {
    // relay is already on
    return;
  }
  bool status = relays(relay, 0); // 0 is ON
  if(status) {
    #ifdef DEBUG_RELAY
      printf_P(PSTR("RELAY: Info: '%s' is enabled.\n\r"), relay);
    #endif
    rtc.writenvram(relay, true);
  }
}

void relayOff(uint8_t relay) {
  if(rtc.readnvram(relay) == false) {
    // relay is already off
    return;
  }
  bool status = relays(relay, 1); // 1 is OFF
  if(status) {
    #ifdef DEBUG_RELAY
      printf_P(PSTR("RELAY: Info: '%s' is disabled.\n\r"), relay);
    #endif
    rtc.writenvram(relay, false);
  }
}

bool relays(uint8_t relay, uint8_t state) {
  if(relay == PUMP_MISTING) {
    pinMode(PUMP_MISTINGPIN, OUTPUT);
    digitalWrite(PUMP_MISTINGPIN, state);
    return true;
  } 
  if(relay == PUMP_WATERING) {
    pinMode(PUMP_WATERINGPIN, OUTPUT);    
    digitalWrite(PUMP_WATERINGPIN, state);
    return true;
  } 
  if(relay == LAMP) {
    pinMode(LAMPPIN, OUTPUT);
    digitalWrite(LAMPPIN, state);
    return true;
  }
  #ifdef DEBUG_RELAY
    printf_P(PSTR("RELAY: Error: '%s' is unknown!\n\r"), relay);
  #endif
  return false;
}

void check() {
  #ifdef DEBUG
    printf_P(PSTR("Free memory: %d bytes.\n\r"), freeMemory());
  #endif
  // check memory
  if(freeMemory() < 600) {
    rtc.writenvram(ERROR, ERROR_LOW_MEMORY);
    return;
  }
  // check EEPROM
  if(storage.ok == false) {
    rtc.writenvram(ERROR, ERROR_EEPROM);
    return;
  }
  // read DHT sensor
  if(read_DHT() == false) {
    rtc.writenvram(ERROR, ERROR_DHT);
    return;
  }
  // read BH1750 sensor
  if(read_BH1750() == false) {
    rtc.writenvram(ERROR, ERROR_BH1750);
    return;
  }
  // read DS18B20 sensors
  if(read_DS18B20() == false) {
    rtc.writenvram(ERROR, ERROR_DS18B20);
    return;
  }
  // reset error
  rtc.writenvram(ERROR, NO_ERROR);

  // check substrate temperature
  if(rtc.readnvram(SUBSTRATE_TEMP) <= settings.subsTempMinimum) {
    rtc.writenvram(WARNING, WARNING_SUBSTRATE_COLD);
    return;
  }
  // check air temperature
  if(rtc.readnvram(AIR_TEMP) <= settings.airTempMinimum && 
      // prevent nightly alarm
      clock.hour() >= 7) {
    rtc.writenvram(WARNING, WARNING_AIR_COLD);
    return;
  } else if(rtc.readnvram(AIR_TEMP) >= settings.airTempMaximum) {
    rtc.writenvram(WARNING, WARNING_AIR_HOT);
  }
  // reset warning
  rtc.writenvram(WARNING, NO_WARNING);
}

void doWork() {
  // don't do any work while error
  if(rtc.readnvram(ERROR) != NO_ERROR) {
    return;
  }
  uint8_t speed = 60; // sec per min
  // check humidity
  if(rtc.readnvram(HUMIDITY) <= settings.humidMinimum) {
    speed /= 2; // do work twice often
  } else if(rtc.readnvram(HUMIDITY) >= settings.humidMaximum) {
    speed *= 2; // do work twice rarely
  }
  // sunny time (11-16 o'clock) + light
  if(11 <= clock.hour() && clock.hour() < 16 && 
      rtc.readnvram(LIGHT) >= 2500) {
    #ifdef DEBUG
      printf_P(PSTR("Work: Info: Sunny time.\n\r"));
    #endif
    checkWateringPeriod(settings.wateringSunnyPeriod, speed);
    checkMistingPeriod(settings.mistingSunnyPeriod, speed);
    return;
  }
  // night time (20-8 o'clock) + light
  if((20 <= clock.hour() || clock.hour() < 8) && 
      rtc.readnvram(LIGHT) <= settings.lightMinimum) {
    #ifdef DEBUG
      printf_P(PSTR("Work: Info: Night time.\n\r"));
    #endif
    checkWateringPeriod(settings.wateringNightPeriod, speed);
    checkMistingPeriod(settings.mistingNightPeriod, speed);
    return;
  }
  // other time period
  checkWateringPeriod(settings.wateringOtherPeriod, speed);
  checkMistingPeriod(settings.mistingOtherPeriod, speed);
}

void checkWateringPeriod(uint8_t _period, uint8_t _time) {
  if(_period != 0 && millis()/1000 > lastWatering + (_period * _time))
    startWatering = millis()/1000;
}

void checkMistingPeriod(uint8_t _period, uint8_t _time) {
  if(_period != 0 && millis()/1000 > lastMisting + (_period * _time))
    startMisting = settings.mistingDuration;
}

void doLight() { 
  // try to up temperature
  if(rtc.readnvram(AIR_TEMP) <= settings.airTempMinimum &&
      rtc.readnvram(LIGHT) > 100) {
    // turn on lamp
    relayOn(LAMP);
    return;
  }
  uint16_t dtime = clock.hour()*60+clock.minute();

  // light enough
  if(rtc.readnvram(LIGHT) > settings.lightMinimum) {
    // turn off lamp
    relayOff(LAMP);
    
    bool morning = 4 < clock.hour() && clock.hour() <= 8;
    // save sunrise time
    if(morning && sunrise == 0) {
      sunrise = dtime;
    } else
    // watch for 30 min to check
    if(morning && sunrise+30 <= dtime) {
      #ifdef DEBUG
        printf_P(PSTR("Light: Info: Set new Day Start time: %02d:%02d.\n\r"), 
        sunrise/60, sunrise%60);
      #endif
      // save to EEPROM if big difference
      if(sunrise-30 > settings.lightDayStart || 
          sunrise+30 < settings.lightDayStart) {
        storage.changed = true;
      }
      settings.lightDayStart = sunrise;
      // prevent rewrite, move to out of morning
      sunrise += 300;
    }
    return;
  }
  // reset sunrise time
  sunrise = 0;
  // keep light day
  uint16_t lightDayEnd = settings.lightDayStart+(settings.lightDayDuration*60);
  if(settings.lightDayStart <= dtime && dtime <= lightDayEnd) {
    #ifdef DEBUG
      printf_P(PSTR("Light: Info: Lamp On till: %02d:%02d.\n\r"), 
        lightDayEnd/60, lightDayEnd%60);
    #endif
    // turn on lamp
    relayOn(LAMP);
    return;
  }
  // turn off lamp
  relayOff(LAMP);
}

void misting() {
  if(startMisting == 0 || 
      rtc.readnvram(WARNING) == WARNING_NO_WATER) {
    // stop misting
    if(rtc.readnvram(PUMP_MISTING)) {
      #ifdef DEBUG
        printf_P(PSTR("Misting: Info: Stop misting.\n\r"));
      #endif
      relayOff(PUMP_MISTING);
      if(rtc.readnvram(WARNING) == WARNING_MISTING)
        rtc.writenvram(WARNING, NO_WARNING);
    }
    return;
  }
  #ifdef DEBUG
    printf_P(PSTR("Misting: Info: Misting...\n\r"));
  #endif
  rtc.writenvram(WARNING, WARNING_MISTING);
  startMisting--;
  lastMisting = millis()/1000;
  relayOn(PUMP_MISTING);
}

void watering() {
  if(startWatering == 0 && 
  	  rtc.readnvram(PUMP_WATERING) == false) {
    return;
  }
  bool timeIsOver = startWatering + (settings.wateringDuration*60) <= millis()/1000;
  // stop watering
  if(rtc.readnvram(WARNING) == INFO_SUBSTRATE_DELIVERED && timeIsOver) {
    #ifdef DEBUG
      printf_P(PSTR("Watering: Info: Stop watering.\n\r"));
    #endif
    relayOff(PUMP_WATERING);
    startWatering = 0;
    if(rtc.readnvram(WARNING) == WARNING_WATERING)
      rtc.writenvram(WARNING, NO_WARNING);
    return;
  }
  // emergency stop
  if(timeIsOver || rtc.readnvram(ERROR) == ERROR_NO_SUBSTRATE) {
    relayOff(PUMP_WATERING);
    rtc.writenvram(WARNING, WARNING_SUBSTRATE_LOW);
    startWatering = 0;
    #ifdef DEBUG
      printf_P(PSTR("Watering: Error: Emergency stop watering.\n\r"));
    #endif
    return;
  }
  // set pause for cleanup pump and rest
  uint8_t pauseDuration = 5;
  if(rtc.readnvram(WARNING) == INFO_SUBSTRATE_DELIVERED)
    pauseDuration = 10;
  // pause every 30 sec
  if((millis()/1000-startWatering) % 30 <= pauseDuration) {
    #ifdef DEBUG
      printf_P(PSTR("Watering: Info: Pause for clean up.\n\r"));
    #endif
    relayOff(PUMP_WATERING);
    return;
  }
  #ifdef DEBUG
    printf_P(PSTR("Misting: Info: Watering...\n\r"));
  #endif
  rtc.writenvram(WARNING, WARNING_WATERING);
  lastWatering = millis()/1000;
  relayOn(PUMP_WATERING);
}
