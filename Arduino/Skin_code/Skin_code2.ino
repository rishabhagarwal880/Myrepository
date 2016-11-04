#include <AD7746.h>
#include <Wire.h>

AD7746 capac;
void setup() {
  Wire.begin();
  Serial.begin(9600);
  capac.initialize();
  Serial.println("Initializing");
  delay(1000);
}

void loop() {
  uint32_t c;
  float d;
  if (capac.testConnection()) {
    capac.writeCapSetupRegister(0x80);
    capac.writeVtSetupRegister(0x00);
    capac.writeExcSetupRegister(0x0B);
    capac.writeConfigurationRegister(0xA2);
    c = capac.getCapacitance() - 0x800000;
    d = float(c);
    d = d / 100000;
    //Serial.println(d);
    if (d > 5) {
      Serial.println("Positive"); 
    }
    if (d < 2.7) {
      Serial.println("Negative");
    }
    delay(100);
  }
}
