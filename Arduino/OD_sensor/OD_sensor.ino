// slave code
#include <Wire.h>
#include <EEPROM.h>

byte dataOut[3] = {};
byte dataIn[6] = {};
boolean requestIn;
boolean receiveIn;
unsigned char reg;
char buffer[100];
long last_update = 0;

// peltier controller used
int target_temp_;
int ramp_speed_;

int TARGET_TEMP_EEPROM_ADDRESS = 0;
int RAMP_SPEED_EEPROM_ADDRESS = 4;

// temperature of all three thermocouples
int t_peltier = 20;
int t_droplet = 21;
int t_2nd_peltier = 22;

// Python thermocouple_id
#define PELTIER_T_PIN 0 // 0
#define DROPLET_T_PIN 1 // 1
#define SCD_PELTIER_T_PIN 2   // 2

//Function for Temp(C) depending on Voltage (milliamp)
#define SIZE_FTV_TBL 32
int ftv[32][2] = { // voltage to temp table
  {-20,-189},
  {-10,-94},
  {0,3},
  {10,101},
  {20,200},
  {25,250},
  {30,300},
  {40,401},
  {50,503},
  {60,605},
  {80,810},
  {100,1015},
  {120,1219},
  {140,1420},
  {160,1620},
  {180,1817},
  {200,2015},
  {220,2213},
  {240,2413},
  {260,2614},
  {280,2817},
  {300,3022},
  {320,3227},
  {340,3434},
  {360,3641},
  {380,3849},
  {400,4057},
  {420,4266},
  {440,4476},
  {460,4686},
  {480,4896},
  {500,5107}
};
  

// Thermocouple read code
float TH_Temp(int ANALOGUE_PIN) {
  float temp;
  // take average of 100 samples
  float sampleTotal = 0;
  for(int i=0; i<100; i++) {
    sampleTotal += (float)analogRead(ANALOGUE_PIN)/100;
  }
  int mV = ((sampleTotal)/1023.0) * 5000;
  int i=1;
  int C1=0;
  int C2=0;
  int V1,V2;

  // for loop
  for(int i=1; (i<32) && C2==0; i++) {
    if (ftv[i][1]>mV) {
	C2 = ftv[i][0];
	C1 = ftv[i-1][0];
	V1 = ftv[i-1][1];
	V2 = ftv[i][1];
    }
  }

  // check size of stuffs
  if (i==SIZE_FTV_TBL) {
    temp = 9999;
  } else {
    temp = float(mV-V1)/(V2-V1);
    temp = temp * (C2-C1) + C1;
  }
  
  return temp;
}

// peltier control code
void control(int heat = 5, int cool = 6) {
  // if we've updated < 100ms ago, return
  long time = millis();
  if(time-last_update<100) {
    return;
  }
  last_update = time;
  
  #define FORWARD 0
  #define REVERSE 1

  pinMode(heat, OUTPUT); // heating
  pinMode(cool, OUTPUT); // cooling 

  int bias = 0;  

  delay(100);

  double peltier = TH_Temp(PELTIER_T_PIN); //0
  double droplet = TH_Temp(DROPLET_T_PIN); //1
  double scd_peltier = TH_Temp(SCD_PELTIER_T_PIN); //2
  
  t_peltier = (int)peltier;
  t_droplet = (int)droplet;
  t_2nd_peltier = (int)scd_peltier;
  
  // change to droplet
  if (target_temp_ >= peltier) {
    bias = FORWARD;
  } else {
    bias = REVERSE;
  }
  
  int targetpin = bias + heat;
  int otherpin = (bias ^ 1) + heat;

  // changed to glass, analog pin 2, for automation link
  // pin 0 is peltier (between top and bottom peltier layers)
  // pin 1 droplet (measure actual droplet temp for comparison)
  double delta = abs(peltier - target_temp_);

  digitalWrite(otherpin, LOW);
  
  if (delta > 0.5) {
    digitalWrite(targetpin, HIGH);
  } else {
    //double fade = (pow(delta, 0.3333)/1.587) * 255;//(delta/4.0) * 255;
    double fade = delta * 255;
    analogWrite(targetpin, fade);
  }
}

void checkCommand() {
  switch(dataIn[0]) {
  case 0: // i2c is asking us for the temperatures of all 3 thermocouples
    dataOut[0] = (char)t_peltier;
    dataOut[1] = (char)t_droplet;
    dataOut[2] = (char)t_2nd_peltier;
    Serial.print("t_peltier=");
    Serial.println(t_peltier);
    break;
  case 1: // i2c is asking us to set the temperature for the peltier device
          // use peltier thermocouple as feedback controller
    setTargetTemp((int)dataIn[1]);
    setRampSpeed((int)dataIn[2]);
    break;
  default:
    break;
  }
}

void handleReceive(int howMany) {
  int i = 0;
  while(Wire.available()) {
    dataIn[i++] = Wire.read();
  }
  checkCommand();
}

void handleRequest() {
  Wire.write(dataOut, 3);
}

void setTargetTemp(int target_temp) {
  target_temp_=target_temp;
  for(int i=0; i<sizeof(target_temp_); i++) {
    EEPROM.write(i+TARGET_TEMP_EEPROM_ADDRESS, ((byte*)&target_temp_)[i]);
  }
  Serial.print("target_temp=");
  Serial.println(target_temp_);  
}

void setRampSpeed(int ramp_speed) {
  ramp_speed_=ramp_speed;
  for(int i=0; i<sizeof(ramp_speed_); i++) {
    EEPROM.write(i+RAMP_SPEED_EEPROM_ADDRESS, ((byte*)&ramp_speed_)[i]);
  }
  Serial.print("ramp_speed=");
  Serial.println(ramp_speed_);  
}

void setup() {
  Serial.begin(115200);
  Wire.begin(1);
  Wire.onRequest(handleRequest);
  Wire.onReceive(handleReceive);
  for(int i=0; i<sizeof(target_temp_); i++) {
     ((byte*)&target_temp_)[i] = EEPROM.read(i+TARGET_TEMP_EEPROM_ADDRESS);
  }
  for(int i=0; i<sizeof(ramp_speed_); i++) {
     ((byte*)&ramp_speed_)[i] = EEPROM.read(i+RAMP_SPEED_EEPROM_ADDRESS);
  }
}

void loop() {
  int dataLen = Serial.available();
  if(dataLen) {
    Serial.readBytes(buffer, min(sizeof(buffer), dataLen));
    Serial.flush();
    buffer[min(sizeof(buffer), dataLen)]=0;
    char* substr = strstr(buffer, "target_temp=");
    if(substr) {
      setTargetTemp(atoi(substr+sizeof("target_temp=")-1));
    }

    substr = strstr(buffer, "ramp_speed=");
    if(substr) {
      setRampSpeed(atoi(substr+sizeof("ramp_speed=")-1));
    }

    substr = strstr(buffer, "target_temp?");
    if(substr) {
      Serial.print("target_temp=");
      Serial.println(target_temp_);
    }

    substr = strstr(buffer, "ramp_speed?");
    if(substr) {
      Serial.print("ramp_speed=");
      Serial.println(ramp_speed_);
    }

    substr = strstr(buffer, "t_peltier?");
    if(substr) {
      Serial.print("t_peltier=");
      Serial.println(t_peltier);
    }
  }
  control();
}

