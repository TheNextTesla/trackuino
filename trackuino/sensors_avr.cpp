/* trackuino copyright (C) 2010  EA5HAV Javi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* Credit to:
 *
 * cathedrow for this idea on using the ADC as a volt meter:
 * http://code.google.com/p/tinkerit/wiki/SecretVoltmeter
 */

#ifdef AVR

#include "config.h"
#include "pin.h"
#include "sensors_avr.h"
#if (ARDUINO + 1) >= 100
#  include <Arduino.h>
#else
#  include <WProgram.h>
#endif

#ifdef USE_BAROMETER
  #include <Wire.h>
  #include <SFE_BMP180.h>
  SFE_BMP180 barometer;

  double last_pressure_reading = 0.0;
  bool safe_to_use_bmp = true;
#endif

/*
 * sensors_aref: measure an external voltage hooked up to the AREF pin,
 * optionally (and recommendably) through a pull-up resistor. This is
 * incompatible with all other functions that use internal references
 * (see config.h)
 */
#ifdef USE_AREF
void sensors_setup()
{
  // Nothing to set-up when AREF is in use
}

unsigned long sensors_aref()
{
  unsigned long result;
  // Read 1.1V reference against AREF (p. 262)
  ADMUX = _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = (ADCH << 8) | ADCL;

  // millivolts = 1.1 * 1024 * 1000 / result
  result = 1126400 / result;

  // aref = read aref * (32K + AREF_PULLUP) / 32K
  result = result * (32000UL + AREF_PULLUP) / 32000;

  return result;
}
#endif

#ifndef USE_AREF
void sensors_setup()
{
  pinMode(INTERNAL_LM60_VS_PIN, OUTPUT);
  pinMode(EXTERNAL_LM60_VS_PIN, OUTPUT);

  //Based upon the SFE_BMP180 example provided with the library
  #ifdef USE_BAROMETER
    barometer.begin();
  #endif
}

#ifdef USE_BAROMETER
long sensors_barometer_pressure()
{
   char status = barometer.startTemperature();
   double T, P;

   if(!safe_to_use_bmp)
   {
     if(status != 0)
     {
      safe_to_use_bmp = true;
     }

     return 0.0;
   }
   
   if(status != 0)
   {
     //TODO: See if a 5 milli delay in startup could cause timing issues
     delay(status);
     status = barometer.getTemperature(T);
      
     if(status != 0)
     {
       status = barometer.startPressure(BAROMETER_SAMPLING);

       if(status != 0)
       {
         delay(status);
         //TODO: Add Efficency for outdoor temp calculation which requires less time
         status = barometer.getPressure(P, T);

         if(status != 0)
         {
            return round_to_long(P * 100);
         }
       }
       else
       {
         safe_to_use_bmp = false;
       }
     }
     else
     {
        safe_to_use_bmp = false;
     }
   }
   else
   {
      safe_to_use_bmp = false;
   }

   return (long) 0;
}
#endif

long round_to_long(double numberDouble)
{
  double number = numberDouble;
  if((number - (long) number) >= 0.5)
  {
   return ((long) number) + 1; 
  }
  else
  {
    return ((long) number);
  }
}

long sensors_internal_temp()
{
  long result;
  // Read temperature sensor against 1.1V reference
  ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX3);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = (ADCH << 8) | ADCL;
  
  result = (result - 125) * 1075;

  return result;
}

int sensors_lm60(int powerPin, int readPin)
{
  pin_write(powerPin, HIGH);      // Turn the LM60 on
  analogReference(INTERNAL);      // Ref=1.1V. Okay up to 108 degC (424 + 6.25*108 = 1100mV)
  analogRead(readPin);            // Disregard the 1st conversion after changing ref (p.256)
  delay(10);                      // This is needed when switching references
  int adc = analogRead(readPin);  // Real read
  pin_write(powerPin, LOW);       // Turn the LM60 off

  int mV = 1100L * adc / 1024L;   // Millivolts

  switch(TEMP_UNIT) {
    case 1: // C
      // Vo(mV) = (6.25*T) + 424 -> T = (Vo - 424) * 100 / 625
      return (4L * (mV - 424) / 25) + CALIBRATION_VAL;
    case 2: // K
      // C + 273 = K
      return (4L * (mV - 424) / 25) + 273 + CALIBRATION_VAL;
    case 3: // F
      // (9/5)C + 32 = F
      return (36L * (mV - 424) / 125) + 32 + CALIBRATION_VAL;
  }
}

int sensors_ext_lm60()
{
  return sensors_lm60(EXTERNAL_LM60_VS_PIN, EXTERNAL_LM60_VOUT_PIN);
}

int sensors_int_lm60()
{
  return sensors_lm60(INTERNAL_LM60_VS_PIN, INTERNAL_LM60_VOUT_PIN);
}

int sensors_vin()
{
  analogReference(DEFAULT);      // Ref=5V
  analogRead(VMETER_PIN);        // Disregard the 1st conversion after changing ref (p.256)
  delay(10);                     // This is needed when switching references

  uint16_t adc = analogRead(VMETER_PIN); 
  uint16_t mV = 5000L * adc / 1024;
   
  // Vin = mV * R2 / (R1 + R2)
  int vin = (uint32_t)mV * (VMETER_R1 + VMETER_R2) / VMETER_R2;
  return vin;
}


#endif
#endif // ifdef AVR
