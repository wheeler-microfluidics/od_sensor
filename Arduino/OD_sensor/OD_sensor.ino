// slave code
#include <Wire.h>
#include <avr/eeprom.h>

#define TSL_FREQ_PIN 2 // output use digital pin2 for interrupt
#define TSL_S0 3
#define TSL_S1 4
#define TSL_S2 5
#define TSL_S3 6

uint32_t pulse_cnt = 0;
uint32_t cur_tm   = millis();
uint32_t pre_tm = cur_tm; 
uint16_t tm_diff = 0; 
int16_t freq_mult = 100;
int16_t calc_sensitivity = 10;
float freq_ = 0.0;

const int16_t motor1Pin = 10;    // H-bridge leg 1 (pin 5, 1A)
const int16_t motor2Pin = 11;    // H-bridge leg 2 (pin 7, 2A)
const int16_t enablePin = 12;    // H-bridge enable pin (pin 6)
int16_t per=23;
int16_t Int=0;

uint8_t i2c_address_;
uint16_t polling_period_ms_;
char buffer[100];
int32_t last_update = 0;

uint16_t EEPROM_I2C_ADDRESS = 0;
uint16_t EEPROM_POLLING_PERIOD_MS = EEPROM_I2C_ADDRESS+sizeof(i2c_address_);

void handleRequest() {
  Wire.write((uint8_t*)&freq_, 4);
}

void setup() {
  i2c_address_ = eeprom_read_byte((uint8_t*)EEPROM_I2C_ADDRESS);
  polling_period_ms_ = eeprom_read_word((uint16_t*)EEPROM_POLLING_PERIOD_MS);
  Serial.begin(115200);
  Wire.begin(i2c_address_);
  Wire.onRequest(handleRequest);

  // attach interrupt to pin2, send output pin of TSL230R to arduino 2
  // call handler on each rising pulse
  attachInterrupt(0, add_pulse, RISING);
   
  pinMode(TSL_FREQ_PIN, INPUT);
  pinMode(TSL_S0, OUTPUT);
  pinMode(TSL_S1, OUTPUT);
  pinMode(TSL_S2, OUTPUT);
  pinMode(TSL_S3, OUTPUT);
  
  // 1x sensitivity,
  // divide-by-100 scaling
   
  digitalWrite(TSL_S0, HIGH);
  digitalWrite(TSL_S1, HIGH);
  digitalWrite(TSL_S2, HIGH);
  digitalWrite(TSL_S3, HIGH);
  
  pinMode(motor1Pin, OUTPUT); 
  pinMode(motor2Pin, OUTPUT); 
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  //val=per*(255./100.);
  //Int=val;
  Int=(per*255/100);
  analogWrite(motor1Pin, Int);  // set leg 1 of the H-bridge high
  digitalWrite(motor2Pin, LOW);   // set leg 2 of the H-bridge low
}

void add_pulse() {
  pulse_cnt++;
  return;
}

void update_tsl_freq() {
  // we have to scale out the frequency --
  // Scaling on the TSL230R requires us to multiply by a factor
  // to get actual frequency
  freq_ = (float)(pulse_cnt * freq_mult);  
  Serial.println(freq_);

  // reset the pulse counter
  pulse_cnt = 0;
}

void config() {
  Serial.println("OD_sensor config:");
  print_i2c_address();
  print_polling_period_ms();
}

void print_i2c_address() {
  Serial.print("i2c_address=");
  Serial.println(i2c_address_);
}

void print_polling_period_ms() {
  Serial.print("polling_period_ms=");
  Serial.println(polling_period_ms_);
}

void print_freq() {
  Serial.print("freq=");
  Serial.println(freq_);
}

void loop() {
  // check the value of the light sensor every polling_period_ms_
  // calculate how much time has passed  
  pre_tm = cur_tm;
  cur_tm = millis();
  if( cur_tm > pre_tm ) {
    tm_diff += cur_tm - pre_tm;
  } else if( cur_tm < pre_tm ) {
    // handle overflow and rollover (Arduino 011)
    tm_diff += ( cur_tm + ( 34359737 - pre_tm ));
  }
  
  // if enough time has passed to do a new reading...
  if( tm_diff >= polling_period_ms_ ) {
    // re-set the ms counter
    tm_diff = 0;

    // update our current frequency reading
    update_tsl_freq();
  }
  
  if(Serial.available()) {
    byte len = Serial.readBytesUntil('\n', buffer, sizeof(buffer));
    buffer[len]=0;
    char* substr;
    substr = strstr(buffer, "config?");
    if(substr) {
      config();
      return;
    }

    substr = strstr(buffer, "i2c_address?");
    if(substr) {
      print_i2c_address();
      return;
    }

    substr = strstr(buffer, "i2c_address=");
    if(substr) {
      i2c_address_ = atoi(substr+sizeof("i2c_address=")-1);
      eeprom_write_byte((uint8_t*)EEPROM_I2C_ADDRESS, i2c_address_);
      print_i2c_address();
      return;
    }

    substr = strstr(buffer, "polling_period_ms?");
    if(substr) {
      print_polling_period_ms();
      return;
    }

    substr = strstr(buffer, "polling_period_ms=");
    if(substr) {
      polling_period_ms_ = atoi(substr+sizeof("polling_period_ms=")-1);
      eeprom_write_word((uint16_t*)EEPROM_POLLING_PERIOD_MS, polling_period_ms_);
      print_polling_period_ms();
      return;
    }

    substr = strstr(buffer, "freq?");
    if(substr) {
      print_freq();
      return;
    }
    
    Serial.println("unrecognized command");
  }
}

