#include <Wire.h>

#include <Adafruit_NeoPixel.h>
#include <avr/power.h>

#define PIN 6

Adafruit_NeoPixel strip = Adafruit_NeoPixel(16, PIN, NEO_GRB + NEO_KHZ800);

// AD7746 
#define I2C_ADDRESS 0x48        // From AD7746_clean.ino. It is the Write or read Addr shift 1 bit to 
                                // the right thus this is the 7-bit address part of the start byte
                                // the arduino Wire library should take care of the read/write bit
#define WRITE_ADDRESS 0x90
#define READ_ADDRESS 0x91
#define RESET_ADDRESS 0xBF

// Register Definitions
#define REGISTER_STATUS 0x00     // Read-only, Indicates status of the converter
#define REGISTER_CAP_DATA 0x01   // 24 bits includes 0x02 and 0x03
#define REGISTER_VT_DATA 0x04    // 24 bits includes 0x05 and 0x06
#define REGISTER_CAP_SETUP 0x07  // Capacitive channel setup
#define REGISTER_VT_SETUP 0x08   // Voltage/Temperature channel setup
#define REGISTER_EXC_SETUP 0x09  // Capacitive channel excitation setup
#define REGISTER_CONFIG 0x0A     // Converter update rate and mode of operation setup
#define REGISTER_CAP_DAC_A 0x0B  // Capacitive DAC setup
#define REGISTER_CAP_DAC_B 0x0C  // Capacitive DAC setup
#define REGISTER_CAP_OFFSET 0x0D // 16 bits includes register 0x0E
#define REGISTER_CAP_GAIN 0x0F   // 16 bits includes register 0x10
#define REGISTER_VOLT_GAIN 0x11  // 16 bits includes register 0x12

// --- Mode Definitions ---
// CAP_SETUP Modes
#define CAPEN _BV(7)             // Enables capacitive channel for single or continuous conversion or calibration
#define CIN2 _BV(6)              // Switches the internal multiplexer to the second capacitive input 
#define CAPDIFF _BV(5)           // Sets differntial mode on the selected capacitive input
#define CAPCHOP _BV(0)           // Doubles capacitive channel conversion times

// EXC_SETUP Modes
#define CLKCTRL _BV(7)           // Decreases the excitation signal freq and the clock freq by a factor of 2
#define EXCON _BV(6)             // Exc. signal is present on the output during both cap and vt conversion
#define EXCB _BV(5)              // Enables EXCB pin as the excitation output
#define NOTEXCB _BV(4)           // Enables EXCB pin as the inverted excitation output
#define EXCA _BV(3)              // Enables EXCA pin as the excitation output
#define NOTEXCA _BV(2)           // Enables EXCA pin as the inverted excitation output
// Excitation Voltage Level Modes
#define EXCVL_LOWEST 0                // Voltage on Cap +-Vdd/4 
#define EXCVL_LOW _BV(0)           // Voltage on Cap +-Vdd/4 
#define EXCVL_HIGH _BV(1)           // Voltage on Cap +-Vdd * 3/8
#define EXCVL_HIGHEST _BV(0) | _BV(1)  // Voltage on Cap +-Vdd/2

// CONFIG Modes
// Capacitive channel digital filter setup -- conversion time/update rate setup
#define CAPF_FASTEST 0                         // Conversion Time: 11.0ms, Update Rate: 90.9, Hz: 87.2
#define CAPF_FASTER _BV(0)                     // Conversion Time: 11.9ms, Update Rate: 83.8, Hz: 79.0
#define CAPF_FAST _BV(1)                       // Conversion Time: 20.0ms, Update Rate: 50.0, Hz: 43.6
#define CAPF_MEDFAST _BV(0) | _BV(1)           // Conversion Time: 38.0ms, Update Rate: 26.3, Hz: 21.8
#define CAPF_MEDSLOW _BV(2)                    // Conversion Time: 62.0ms, Update Rate: 16.1, Hz: 13.1
#define CAPF_SLOW _BV(0) | _BV(2)              // Conversion Time: 77.0ms, Update Rate: 13.0, Hz: 10.5
#define CAPF_SLOWER _BV(1) | _BV(2)            // Conversion Time: 92.0ms, Update Rate: 10.9, Hz: 8.9
#define CAPF_SLOWEST _BV(0) | _BV(1) | _BV(2)  // Conversion Time: 109.6ms, Update Rate: 9.1, Hz: 8.0
// Converter Mode of operation setup
#define MD_IDLE 0                           // Idle
#define MD_CONTINOUS _BV(0)                      // Continuous
#define MD_SINGLE _BV(1)                      // Single conversion
#define MD_POWERDOWN _BV(0) | _BV(1)             // Power-Down
#define MD_CAPOFFSETCAL _BV(0) | _BV(2)             // Capacitive system offset calibration
#define MD_CAPVTGAINCAL _BV(0) | _BV(1) | _BV(2)    // Capacitance or voltage system gain calibration

// Calibration Variables
#define VALUE_UPPER_BOUND 16000000L 
#define VALUE_LOWER_BOUND 0xFL
#define MAX_OUT_OF_RANGE_COUNT 3
#define CALIBRATION_INCREASE 1
byte calibration;
byte outOfRangeCount = 0;



// --- Multiplexer Variables ---
int TOP_S0_PIN = 8;
int TOP_S1_PIN = 9;
int TOP_S2_PIN = 10;

int BOT_S0_PIN = 4;
int BOT_S1_PIN = 3;
int BOT_S2_PIN = 5;

// --- Sensor Flags ---
int sensors[] = {1, 6};  // The sensor at index 0 is the most sensitive

// Status Register shortcut
int CAPINT = 2;  // This pin is connected to the status register and a falling edge (going from
                 // high to low) indicates a conversion is finished.
int prevState;
int currState;
float normalizationVal;

unsigned long offset = 0;   // This is a temporary measure it should be found during the calibration step.
int ind = 0;
String transmit = "";
void setup()
{
  strip.begin();
  for(uint16_t i=0; i<strip.numPixels(); i++) {
      strip.setBrightness(10);
  }
  strip.show(); // Initialize all pixels to 'off'
  
  pinMode(6,OUTPUT);
  digitalWrite(6,LOW);
  
   // --- Configure Multiplexer ---
  pinMode(TOP_S0_PIN,OUTPUT);
  pinMode(TOP_S1_PIN,OUTPUT);
  pinMode(TOP_S2_PIN,OUTPUT);
  pinMode(BOT_S0_PIN,OUTPUT);
  pinMode(BOT_S1_PIN,OUTPUT);
  pinMode(BOT_S2_PIN,OUTPUT);
  selectCapacitor(sensors[0]);

  Wire.begin();                 // Set up I2C for operation
  Serial.begin(9600);           // Set Baud Rate
  
  delay(15);
  Serial.println("Setup Begin");
  
  //  --- Reset Device ---
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(RESET_ADDRESS);    // reset device
  Wire.endTransmission();
  
  delay(1);                     // need to wait at least 200 microseconds for device to reset
  
  // --- Calibrate offset ---

  delay(10);
  offset = readRegister(REGISTER_CONFIG,1);
  Serial.println(offset);

  
  // --- Configure Modes ---
  writeConfig(REGISTER_CAP_SETUP, _BV(7));
  writeConfig(REGISTER_EXC_SETUP, _BV(3) | _BV(1) | _BV(0));
  
  Serial.println("Getting offset");
  offset = readRegister(REGISTER_CONFIG,1);
  Serial.print("Factory offset: ");
  Serial.println(offset);
  
  writeConfig(REGISTER_CONFIG, _BV(7) | _BV(6) | _BV(5) | _BV(4) | _BV(3) | _BV(2) | _BV(0)); // set configuration to calib. mode, slow sample
  // wait for calibration
  delay(10);

  Serial.print("Calibrated offset: ");
  offset = readRegister(REGISTER_CONFIG,1);
  Serial.println(offset);
  
  
  writeConfig(REGISTER_CAP_SETUP, _BV(7));
  writeConfig(REGISTER_EXC_SETUP, _BV(3) | _BV(1) | _BV(0));
  writeConfig(REGISTER_CONFIG, _BV(7) | _BV(6) | _BV(5) | _BV(4) | _BV(3)  | _BV(0));

  calibrate();
 
 
  
  // --- Configure CAPINT and initalize loop variables---
  pinMode(CAPINT,INPUT);
  prevState = digitalRead(CAPINT);
  
  Serial.println("Setup End");
  boolean happen = false;
  while(!happen){
  // jigger
  currState = digitalRead(CAPINT);
  if(prevState == 1 && currState == 0){
    // Read 3 bytes from CAPDATA live yo life
    unsigned long code = readRegister(REGISTER_CAP_DATA,3);
    Serial.println(code);
    normalizationVal = 1000*(4.096*2.0*(code-calibration))/(1.0 * 0xffffffUL)-4.096;

    //Serial.println(code);
    //transmit = transmit + String(ind) + ":" + String(tempCode) + ",";
    //selectCapacitor(ind++ % 8);
    happen = true;
    
  }
    prevState = currState; 
  }

  delay(5000);  
}
int totRange = 0;
boolean flag = false;
void loop()
{
  
    colorWipe(strip.Color(0, 255, 0), 50,totRange); // Red
    int rand = random(0, 3);
    if(rand >= 1){
      if(!flag){
        totRange ++;  
      }else{
        totRange--; 
      }
      if(totRange == 4){
       flag = true;
       delay(5000);
        
      }else if(totRange == 4){
        flag = false;
       delay(5000);
      }
    }
    delay(1000);
  
  /*
  if(ind % 8 == 0){
    Serial.println(transmit);
    transmit = "";
  }
  */
}
// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait,float val) {
  //int range = int((normalizationVal-val)/(600)*4);
  int range = val;
  int range2 = range-1;
  if(range > 4){
   range = 4; 
  }
  for(uint16_t i=0; i<strip.numPixels()/4; i++) {
      if(range > 0){
        strip.setPixelColor(4*i, c);
        strip.setPixelColor(4*i + 1, c);
        range--;
        strip.show();
        delay(wait);  
      }else{
        strip.setPixelColor(4*i, 0);
        strip.setPixelColor(4*i + 1, 0);
        strip.show();
      }
      if(range2 > 0){
        strip.setPixelColor(4*i +2, c);
        strip.setPixelColor(4*i + 3, c);
        range2--;
        strip.show();
        delay(wait);  
        
      }else{
        
        strip.setPixelColor(4*i + 2, 0);
        strip.setPixelColor(4*i + 3, 0);
        strip.show();
      }
      
  }
}

/*
   Writes a bitmask to the specified address.  This configures
   the operation of the AD7746 
*/
void writeConfig(unsigned char address, unsigned char mask)
{
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(address);  // may have to resolve to two bytes
    Wire.write(mask);
    Wire.endTransmission();
    delay(5);
}
/*
  Reads numBytes bytes from the selected address and adds
  the bytes into one number. 
  NOTE: May want to remove Signaling printlines for normal operation
  NOTE: Add error checking for numBytes, i.e. less than 1
*/
unsigned long readRegister(unsigned char address, unsigned int numBytes)
{
  //Serial.println("Begin Read");
 
  union {
    char data[3];
    unsigned long code;
  } byteMappedCode;
  
  byteMappedCode.code = 0;
  
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(address);
  Wire.endTransmission();
  
  //Wire.requestFrom(READ_ADDRESS, numBytes);
  Wire.requestFrom(I2C_ADDRESS, numBytes, false);
  
  while(Wire.available() < numBytes){  }
  /*
  unsigned long code = Wire.read();           // read byte and initialize code
  // The for-loop will not execute if there is only 1 byte to be read
  for(int i = 0; i <numBytes-1; i++){
    code <<= 8;                     // shift to accomodate next byte
    code += Wire.read();            // read next byte   
  }
  */
  for(int i = 0; i <numBytes; i++){
    byteMappedCode.data[i] = Wire.read();
  }

  //Serial.print("Code: ");
  //Serial.println(code);
  
  //Serial.print("Capacitance: ");
  //Serial.println((4.096*2*code)/(1.0 *0xffffff)-4.096 + OFFSET);
  
  //Serial.println("End Read");
  return byteMappedCode.code;
}
/*
  Multiplexes to set a different capacitor to funnel to the AD7746
  should probably put a delay after this or to disregard the next conversion.
  NOTE: Should also move this to a separate multiplexer library or look into 
  downloading one to use.
*/
void selectCapacitor(int capIndex){
  
  if(capIndex >= 0 && capIndex < 8){
     switch(capIndex){
        case 0:                            // A0 channel, pads 1 and 9
            digitalWrite(TOP_S0_PIN,LOW);
            digitalWrite(TOP_S1_PIN,LOW);
            digitalWrite(TOP_S2_PIN,LOW);
            
            digitalWrite(BOT_S0_PIN,LOW);
            digitalWrite(BOT_S1_PIN,LOW);
            digitalWrite(BOT_S2_PIN,LOW);
            break;
        case 1:                            // A1 channel, pads 2 and 10
            digitalWrite(TOP_S0_PIN,HIGH);
            digitalWrite(TOP_S1_PIN,LOW);
            digitalWrite(TOP_S2_PIN,LOW);
            
            digitalWrite(BOT_S0_PIN,HIGH);
            digitalWrite(BOT_S1_PIN,LOW);
            digitalWrite(BOT_S2_PIN,LOW);
            break;
        case 2:                            // A2 channel, pads 3 and 11
            digitalWrite(TOP_S0_PIN,LOW);
            digitalWrite(TOP_S1_PIN,HIGH);
            digitalWrite(TOP_S2_PIN,LOW);
            
            digitalWrite(BOT_S0_PIN,LOW);
            digitalWrite(BOT_S1_PIN,HIGH);
            digitalWrite(BOT_S2_PIN,LOW);
            break;
        case 3:                            // A3 channel, pads 4 and 12
            digitalWrite(TOP_S0_PIN,HIGH);
            digitalWrite(TOP_S1_PIN,HIGH);
            digitalWrite(TOP_S2_PIN,LOW);
            
            digitalWrite(BOT_S0_PIN,HIGH);
            digitalWrite(BOT_S1_PIN,HIGH);
            digitalWrite(BOT_S2_PIN,LOW);
            break;
        case 4:                            // A4 channel, pads 5 and 13
            digitalWrite(TOP_S0_PIN,LOW);
            digitalWrite(TOP_S1_PIN,LOW);
            digitalWrite(TOP_S2_PIN,HIGH);
            
            digitalWrite(BOT_S0_PIN,LOW);
            digitalWrite(BOT_S1_PIN,LOW);
            digitalWrite(BOT_S2_PIN,HIGH);
            break;
        case 5:                            // A5 channel, pads 6 and 14
            digitalWrite(TOP_S0_PIN,HIGH);
            digitalWrite(TOP_S1_PIN,LOW);
            digitalWrite(TOP_S2_PIN,HIGH);
            
            digitalWrite(BOT_S0_PIN,HIGH);
            digitalWrite(BOT_S1_PIN,LOW);
            digitalWrite(BOT_S2_PIN,HIGH);
            break;
        case 6:                            // A6 channel, pads 7 and 15
            digitalWrite(TOP_S0_PIN,HIGH);
            digitalWrite(TOP_S1_PIN,HIGH);
            digitalWrite(TOP_S2_PIN,LOW);
            
            digitalWrite(BOT_S0_PIN,HIGH);
            digitalWrite(BOT_S1_PIN,HIGH);
            digitalWrite(BOT_S2_PIN,LOW);
            break;
        case 7:                            // A7 channel, pads 8 and 16
            digitalWrite(TOP_S0_PIN,HIGH);
            digitalWrite(TOP_S1_PIN,HIGH);
            digitalWrite(TOP_S2_PIN,HIGH);
            
            digitalWrite(BOT_S0_PIN,HIGH);
            digitalWrite(BOT_S1_PIN,HIGH);
            digitalWrite(BOT_S2_PIN,HIGH);
            break;
     }
    
  } else{
    Serial.println("capIndex out of range"); 
  }
  
}
void calibrate (byte direction) {
  calibration += direction;
  //assure that calibration is in 7 bit range
  calibration &=0x7f;
  writeConfig(REGISTER_CAP_DAC_A, _BV(7) | calibration);
}
void calibrate() {
  calibration = 0;

  Serial.println("Calibrating CapDAC A");

  unsigned long value = readRegister(REGISTER_CAP_DATA,3);

  while (value>VALUE_UPPER_BOUND && calibration < 128) {
    calibration++;
    writeConfig(REGISTER_CAP_DAC_A, _BV(7) | calibration);
    value = readRegister(REGISTER_CAP_DATA,3);
  }
  Serial.println("done");
}
void modifyCalibration(unsigned long value){
 if ((value<VALUE_LOWER_BOUND) or (value>VALUE_UPPER_BOUND)) {
    outOfRangeCount++;
  }
  if (outOfRangeCount>MAX_OUT_OF_RANGE_COUNT) {
    if (value < VALUE_LOWER_BOUND) {
      calibrate(-CALIBRATION_INCREASE);
    } 
    else {
      calibrate(CALIBRATION_INCREASE);
    }
    outOfRangeCount=0;
  } 
  
}
