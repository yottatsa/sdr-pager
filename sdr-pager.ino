
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
  void fail();
  void waitSPI();
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
  POWER_UP_t power_up = {0b00000001, 0, htonl(30000000)};
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

  return true;
}
void Si4362::end() {
  spi.end();
  reset(true);
}

bool Si4362::findChip() {
  GET_INT_STATUS_REQ_t get_int_status = {0, 0, 0};

  PART_INFO_t part_info;

  Serial.print("Resetting Si4362: ");
  reset();
  wait();
  Serial.println("reset successfully");

  if (digitalRead(nIRQ) == LOW)
    Serial.println("interrupts; ");
  spi.beginTransaction(spiSettings);
  digitalWrite(nSEL, LOW);
  spi.transfer(RADIOLIB_SI4362_CMD_POWER_UP);
  Serial.print("Power up: ");
  if (digitalRead(CTS) == LOW)
    Serial.print("Chip responded; ");
  for (int i = 0; i < sizeof(this->power_up.bytes); i++) {
    spi.transfer(this->power_up.bytes[i]);
  }
  digitalWrite(nSEL, HIGH);
  spi.endTransaction();
  wait();
  Serial.println("powered up");

  if (digitalRead(nIRQ) == LOW) {
    spi.beginTransaction(spiSettings);
    digitalWrite(nSEL, LOW);
    spi.transfer(RADIOLIB_SI4362_CMD_GET_INT_STATUS);
    Serial.print("Clear interrupts: ");
    if (digitalRead(CTS) == LOW)
      Serial.print("Chip responded; ");
    for (int i = 0; i < sizeof(get_int_status.bytes); i++) {
      spi.transfer(get_int_status.bytes[i]);
    }
    digitalWrite(nSEL, HIGH);
    spi.endTransaction();
    wait();
    if (digitalRead(nIRQ) != HIGH) {
      Serial.println("interrupts didn't clear");
      fail();
    } else {
      Serial.println("cleared");
    }
  }

  wait();
  spi.beginTransaction(spiSettings);
  digitalWrite(nSEL, LOW);
  spi.transfer(RADIOLIB_SI4362_CMD_PART_INFO);
  Serial.print("Get part info: ");
  digitalWrite(nSEL, HIGH);
  wait();
  digitalWrite(nSEL, LOW);
  waitSPI();
  for (int i = 0; i < sizeof(part_info.bytes); i++) {
    part_info.bytes[i] = spi.transfer(0);
  }
  digitalWrite(nSEL, HIGH);
  spi.endTransaction();

  uint16_t partno = htons(part_info.part);
  if (partno == 0x4362) {
    Serial.println("found Si4362");
    return true;
  }

  Serial.print("chip id wrong, 0x");
  Serial.print(partno, HEX);
  Serial.println("!=0x4362");
  return false;
}

void Si4362::reset(bool keep_down = false) {
  pinMode(SDN, OUTPUT);
  digitalWrite(SDN, HIGH);
  if (keep_down == false) {
    delayMicroseconds(10);
    digitalWrite(SDN, LOW);
    delayMicroseconds(14);
  }
}

void Si4362::wait() {
  for (uint8_t i = 0; i < 100; i++) {
    if (digitalRead(CTS) == HIGH)
      return;
    if (i == 0)
      Serial.print("Wait for CTS .");
    else
      Serial.print(".");

    delayMicroseconds(50);
  }
  Serial.println("CTS did not return");
  fail();
}

void Si4362::waitSPI() {
  uint8_t cts = 0xFF;
  spi.transfer(RADIOLIB_SI4362_CMD_READ_CMD_BUFF);
  for (uint8_t i = 0; i < 100; i++) {
    cts = spi.transfer(0xFF);
    if (cts == 0xFF)
      return;
    if (i == 0)
      Serial.print("Wait for CTS over SPI.");
    else
      Serial.print(".");
    delay(100);
  }
  Serial.println("SPI did not return");
  fail();
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
