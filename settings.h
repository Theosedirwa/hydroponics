
#ifndef SETTINGS_H
#define SETTINGS_H

#include "EEPROMex.h"

// Declare EEPROM values
#define SETTINGS_ID  ":)"
// Declare structure and default settings
struct SettingsStruct {
  uint8_t wateringDayPeriod, wateringNightPeriod, wateringSunrisePeriod;
  uint8_t mistingDayPeriod, mistingNightPeriod, mistingSunrisePeriod;
  uint8_t daytimeFrom, daytimeTo;
  uint8_t nighttimeFrom, nighttimeTo;
  uint8_t lightThreshold, lightDayDuration;
  uint8_t humidThreshold, tempThreshold, tempSubsThreshold;
  char id[3];
} settings = { 
  60, 180, 90,
  30, 90, 60,
  13, 16,
  21, 04,
  200, 14,
  40, 20, 20,
  SETTINGS_ID
}, memory;

// Debug info
#define DEBUG  false


class Storage 
{
  public:
	bool isChanged;

	bool load() {
	  // search through the EEPROM for a valid structure
	  for ( ; eeprom_offset < EEPROMSizeATmega328-sizeof(memory) ; ++eeprom_offset) {    
	    //read a struct sized block from the EEPROM
	    EEPROM.readBlock(eeprom_offset, memory);
	    if (strcmp(memory.id, SETTINGS_ID) == 0) {
	      // load settings        
	      settings = memory;
	      if(DEBUG) printf_P(PSTR("EEPROM: Info: Settings loaded.\n\r"));
	      return true;
	    }
	  }
	  printf_P(PSTR("EEPROM: Error: Can't load settings!\n\r"));
	  return false;
	}

	bool save() {
	  // prevent to burn EEPROM
	  EEPROM.setMaxAllowedWrites(50);
	  // move on one position
	  ++eeprom_offset;
	  // if writing at offset would mean going outside the EEPROM limit
	  if(eeprom_offset > EEPROMSizeATmega328-sizeof(settings)) 
	    eeprom_offset = 0;

         printf_P(PSTR("EEPROM: Warning: Write data to EEPRROM!\n\r"));
	  int writeCount = EEPROM.updateBlock(eeprom_offset, settings);
	  
	  if(writeCount = sizeof(settings)) {
	    if(DEBUG) printf_P(PSTR("EEPROM: Info: Saved settings at address %d with size %d.\n\r"),
	                eeprom_offset, writeCount);
	    return true;
	  }
	  printf_P(PSTR("EEPROM: Error: Can't save settings! Stored %d of %d at address %d.\n\r"),
	    writeCount, sizeof(settings), eeprom_offset);
	  return false;
	}

  private:
	uint16_t eeprom_offset;
};

#endif // __SETTINGS_H__
