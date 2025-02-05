/*
  xsns_92_am2320.ino - Tasmota driver for I2C AM2320 Temp/Hum Sensor

  Copyright (C) 2019  Lars Wessels

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_AM2320

/*********************************************************************************************\
   AM2320 - Digital Temperature and Humidity Sensor
   https://akizukidenshi.com/download/ds/aosong/AM2320.pdf

   based on https://github.com/hibikiledo/AM2320 from Ratthanan Nalintasnai
   and https://github.com/adafruit/Adafruit_AM2320 from Limor Fried (Adafruit Industries)
  \*********************************************************************************************/

#define XSNS_92           92

#define AM2320_ADDR				0x5C	 // use 7bit address: 0xB8 >> 1
#define INIT_MAX_RETRIES  5

uint8_t am2320_found = 0;
struct AM2320_Readings {
  uint8_t valid = 0;
  float t = NAN;
  float h = NAN;
} AM2320;


bool Am2320Init(void)
{
  // wake AM2320 up, goes to sleep to not warm up and affect the humidity sensor
  Wire.beginTransmission(AM2320_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(1);

  Wire.beginTransmission(AM2320_ADDR); // start transmission
  Wire.write(0x03);  // read register data (function code)
  Wire.write(0x00);  // start address (0x00 = Temp, 0x02 = Humidity)
  Wire.write(0x04);  // read 4 bytes (2 byte temperature + 2 byte humidity)
  if (Wire.endTransmission(1) != 0) {
    return false;
  }
  return true;
}


unsigned int crc16(byte *ptr, byte length)
{
  unsigned int crc = 0xFFFF;

  while (length--) {
    crc ^= *ptr++;
    for (uint8_t i = 0; i < 8; i++) {
      if ((crc & 0x01) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}


bool Am2320Read(void)
{
  byte buf[8];

  if (AM2320.valid > 0) {
    AM2320.valid--;
  }
  if (!Am2320Init()) {
    return false;
  }

  delayMicroseconds(1600); // >=1.5ms
  Wire.requestFrom(AM2320_ADDR, 8); // request all data
  if (Wire.available() != 8) {
    return false;
  }

  // read 8 bytes: preamble(2) + data(2) + crc(2)
  memset(buf, 0, 8);
  for (uint8_t i = 0; i < 8; i++) {
    buf[i] = Wire.read();
  }

  if (buf[0] != 0x03) {
    return false;  // must be 0x03 function code reply
  }
  if (buf[1] != 4) {
    return false;  // must be 4 bytes reply
  }

  // verify checksum
  unsigned int receivedCrc = (buf[7] << 8) | buf[6];  // pack high and low byte together
  if (receivedCrc == crc16(buf, 6)) {  // preamble + data
    int temperature = ((buf[4] & 0x7F) << 8) | buf[5];
    AM2320.t = temperature / 10.0;
    // check for negative temp reading
    AM2320.t = ((buf[4] & 0x80) >> 7) == 1 ? AM2320.t * (-1) : AM2320.t;

    int humidity = (buf[2] << 8) | buf[3];
    AM2320.h = humidity / 10.0;

    AM2320.valid = SENSOR_MAX_MISS; // reset error counter
    return true;

  } else {
    snprintf_P(log_data, sizeof(log_data), "Am2320Read() checksum failed");
    AddLog(LOG_LEVEL_ERROR);
    return false;
  }
}


void Am2320Detect(void)
{
  if (Am2320Init()) {
    snprintf_P(log_data, sizeof(log_data), S_LOG_I2C_FOUND_AT, "AM2320", AM2320_ADDR);
    if (!am2320_found) {
      AddLog(LOG_LEVEL_INFO);
    } else {
      AddLog(LOG_LEVEL_DEBUG);
    }
    am2320_found = 3;
  } else {
    if (am2320_found) {
      am2320_found--;
    }
  }
}


void Am2320EverySecond(void)
{
  if (!(uptime % 10)) {
    Am2320Detect(); // look for sensor every 10 seconds, after three misses it's set to not found
  } else if (uptime & 1 && am2320_found) { // read from sensor every 2 seconds
    if (!Am2320Read()) {
      AddLogMissed("AM2320", AM2320.valid);
    }
  }
}


void Am2320Show(boolean json)
{
  if (!am2320_found) {
    return;  // no sensor, no show :(
  }

  // after SENSOR_MAX_MISS sec. with no update (AM2320.valid == 0),
  // skip propagation of old sensor values, instead show '--'
  char temperature[33];
  char humidity[33];
  if (AM2320.valid) {
    dtostrfd(AM2320.t, Settings.flag2.temperature_resolution, temperature);
    dtostrfd(AM2320.h, Settings.flag2.humidity_resolution, humidity);
  } else {
    strncpy(humidity, "-- ", 32);
    strncpy(temperature, "-- ", 32);
  }

  if (json) {
    ResponseAppend_P(JSON_SNS_TEMPHUM, "AM2320", temperature, humidity);
    //snprintf_P(mqtt_data, sizeof(mqtt_data), JSON_SNS_TEMPHUM, mqtt_data, "AM2320", temperature, humidity);
#ifdef USE_DOMOTICZ
    if ((0 == tele_period)) {
      DomoticzTempHumSensor(temperature, humidity);
    }
#endif  // USE_DOMOTICZ
#ifdef USE_KNX
    if ((0 == tele_period)) {
      KnxSensor(KNX_TEMPERATURE, temperature);
      KnxSensor(KNX_HUMIDITY, humidity);
    }
#endif  // USE_KN

#ifdef USE_WEBSERVER
  } else {
    WSContentSend_PD(HTTP_SNS_TEMP, "AM2320", temperature, TempUnit());
    WSContentSend_PD(HTTP_SNS_HUM, "AM2320", humidity);

    //snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_SNS_TEMP, mqtt_data, "AM2320", temperature, TempUnit());
    //snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_SNS_HUM, mqtt_data, "AM2320", humidity);
#endif  // USE_WEBSERVER
  }
}

/*********************************************************************************************\
   Interface
  \*********************************************************************************************/

boolean Xsns92(byte function)
{
  boolean result = false;

  if (i2c_flg) {
    switch (function) {
      case FUNC_INIT:
        Am2320Detect();
        break;
      case FUNC_EVERY_SECOND:
        Am2320EverySecond();
        break;
      case FUNC_JSON_APPEND:
        Am2320Show(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        Am2320Show(0);
        break;
#endif  // USE_WEBSERVER
    }
  }
  return result;
}

#endif  // USE_AM2320
#endif  // USE_I2C
