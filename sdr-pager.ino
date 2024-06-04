
#include <SPI.h>
#include <Wire.h>

#define RADIOLIB_SI4362_CMD_POWER_UP 0x02
#define RADIOLIB_SI4362_CMD_PART_INFO 0x01
#define RADIOLIB_SI4362_CMD_FUNC_INFO 0x01
#define RADIOLIB_SI4362_CMD_GET_INT_STATUS 0x20
#define RADIOLIB_SI4362_CMD_READ_CMD_BUFF 0x44

#define nSEL 10
#define nIRQ 16
#define SDN 5
#define CTS A3

#define htons(x) __builtin_bswap16((uint16_t)(x))
#define htonl(x) __builtin_bswap32((uint32_t)(x))

class Si4362 {
public:
  //  Si4362();

  bool begin();
  void end();

protected:
  void reset(bool keep_down = false);
  bool findChip();
  void wait();
  void transferSPI(uint8_t cmd, uint8_t *in=NULL, uint8_t in_l=0, uint8_t *out=NULL, uint8_t out_l=0);
  void fail();
  SPIClass spi;
  SPISettings spiSettings = {100000, MSBFIRST, SPI_MODE0};
  union PART_INFO_t;
  union FUNC_INFO_t;
  union GET_INT_STATUS_REQ_t;
  union POWER_UP_t {
    struct {
      uint8_t boot_options;
      uint8_t xtal_options;
      uint32_t xo_freq;
    };
    uint8_t bytes[6];
  };
  POWER_UP_t power_up = {1, 0, htonl(30000000)};
};

union Si4362::PART_INFO_t {
  struct {
    uint8_t chiprev;
    uint16_t part;
    uint8_t pbuild;
    uint16_t id;
    uint8_t customer;
    uint8_t romid;
  };
  uint8_t bytes[8];
};

union Si4362::FUNC_INFO_t {
  struct {
    uint8_t revext;
    uint8_t revbranch;
    uint8_t reving;
    uint16_t patch;
    uint8_t func;
  };
  uint8_t bytes[6];
};

union Si4362::GET_INT_STATUS_REQ_t {
  struct {
    uint8_t ph_clr_pend;
    uint8_t modem_clr_pend;
    uint8_t chip_clr_pend;
  };
  uint8_t bytes[3];
};

bool Si4362::begin() {
  pinMode(nIRQ, INPUT);
  pinMode(CTS, INPUT);
  pinMode(SDN, OUTPUT);
  pinMode(nSEL, OUTPUT);
  digitalWrite(SDN, HIGH);
  digitalWrite(nSEL, HIGH);

  spi.begin();
  if (!Si4362::findChip())
    return false;

  if (digitalRead(nIRQ) == LOW) {
    GET_INT_STATUS_REQ_t get_int_status = {127, 255, 255};
    Serial.print("Clearing interrupts: ");
    transferSPI(RADIOLIB_SI4362_CMD_GET_INT_STATUS, get_int_status.bytes, sizeof(get_int_status.bytes), NULL, 0);
    if (digitalRead(nIRQ) != LOW) {
      Serial.println("interrupts didn't stay");
      return false;
    }
    Serial.print("nop ok, now clearing: ");
    get_int_status = {0, 0, 0};
    transferSPI(RADIOLIB_SI4362_CMD_GET_INT_STATUS, get_int_status.bytes, sizeof(get_int_status.bytes), NULL, 0);
    if (digitalRead(nIRQ) != HIGH) {
      Serial.println("interrupts didn't clear");
      return false;
    } else {
      Serial.println("cleared");
    }
  } else {
    Serial.print("No interrupt signal!");
    return false;
  }

  return true;
}
void Si4362::end() {
  spi.end();
  reset(true);
}

bool Si4362::findChip() {
  PART_INFO_t part_info;

  Serial.print("Resetting Si4362: ");
  reset();
  wait();
  Serial.println("finished");

  Serial.print("Powering up: ");
  transferSPI(RADIOLIB_SI4362_CMD_POWER_UP, power_up.bytes, sizeof(power_up.bytes), NULL, 0);
  Serial.println("powered up");

  Serial.print("Getting part info:");
  transferSPI(RADIOLIB_SI4362_CMD_PART_INFO, NULL, 0, part_info.bytes, sizeof(part_info.bytes));
  uint16_t partno = htons(part_info.part);
  if (partno != 0x4362) {
    Serial.print("chip id is wrong, got 0x");
    Serial.print(partno, HEX);    
    return false;
  }

  Serial.println("found Si4362");
  return true;
}

void Si4362::reset(bool keep_down = false) {
  digitalWrite(SDN, HIGH);
  if (keep_down == false) {
    delayMicroseconds(10);
    digitalWrite(SDN, LOW);
    delayMicroseconds(14);
  }
}

void Si4362::wait() {
  for (uint8_t i = 0; i < 200; i++) {
    if (digitalRead(CTS) == HIGH) {
      if (i != 0) Serial.print("ok; ");
      return;
    }

    if (i == 0)
      Serial.print("waiting for CTS .");
    else
      Serial.print(".");

    delayMicroseconds(50);
  }
  Serial.println("CTS did not return");
  fail();
}

void Si4362::transferSPI(uint8_t cmd, uint8_t *in=NULL, uint8_t in_l=0, uint8_t *out=NULL, uint8_t out_l=0) {
  uint8_t cts = 0xFF;
  spi.beginTransaction(spiSettings);
  
  wait();  
  digitalWrite(nSEL, LOW);
  spi.transfer(cmd);
  if (digitalRead(CTS) == LOW)
    Serial.print("chip responded; ");
  for (int i = 0; i < in_l; i++) {
    spi.transfer(in[i]);
  }  
  digitalWrite(nSEL, HIGH);
  
  if (out_l != 0) {
    Serial.print("reading back; ");
    wait();
    digitalWrite(nSEL, LOW);
    spi.transfer(RADIOLIB_SI4362_CMD_READ_CMD_BUFF);
    if (digitalRead(CTS) == LOW)
      Serial.print("chip responded; ");
    cts = spi.transfer(0xFF);
    for (uint8_t i = 0; (i < 100 && cts != 0xFF); i++) {
      if (i == 0)
        Serial.print("waiting for response .");
      else
        Serial.print(".");
      delay(100);
    }
    if (cts != 0xFF) {
      Serial.println("SPI did not respond");
      return false;
    }
    
    for (int i = 0; i < out_l; i++) {
      out[i] = spi.transfer(0);
    }
    digitalWrite(nSEL, HIGH);
    return true;
  } else {
    wait();
  }
  
  spi.endTransaction();
}

void Si4362::fail() {
  while (true) {
    delay(1);
  }
}

Si4362 radio;
void setup() {
  Serial.begin(57600);
  Serial.println();
  Serial.println("sdr-pager up");

  radio.begin();
  delay(2000);
  radio.end();
}

void loop() {
  delay(5000);
  Serial.print("boop ");
}
