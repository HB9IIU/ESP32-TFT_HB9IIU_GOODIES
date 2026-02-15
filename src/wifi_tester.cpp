#include <Arduino.h>
#include <config.h>
#include <HB9IIU_RobustWIfiConnection.h>

void setup()
{
  Serial.begin(115200);
  delay(5000);

  // Default: connect to first available (fixed order)
  HB9IIUWifiConnection(false);
  // To force strongest-known (scan), use true:

  }
void loop()
{
delay(500);

  }