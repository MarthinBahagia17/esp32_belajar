#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <TFT_eSPI.h>
#include <Regexp.h>
#include <RTClib.h>
#include <Wire.h>
#include <EEPROM.h>

#define EEPROM_SIZE 256
RTC_DS3231 rtc;

// MOTOR
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;
bool motorRotate = false;

#define motor1Pin1 12
#define motor1Pin2 14
#define enable1Pin 13
#define motor2Pin1 25
#define motor2Pin2 26
#define enable2Pin 27
#define motor3Pin2 32
#define motor3Pin1 33
#define enable3Pin 5

int listEnablePin[3] = {enable1Pin, enable2Pin, enable3Pin};
int listMotor1Pin[3] = {motor1Pin1, motor2Pin1, motor3Pin1};
int listMotor2Pin[3] = {motor1Pin2, motor2Pin2, motor3Pin2};

// RFID
TFT_eSPI tft = TFT_eSPI();
MFRC522 mfrc522(17, 16);
MFRC522::MIFARE_Key key;

long tagihan = 1;
int sisasaldo = 0;
bool ceksaldo = false;

bool notif = true;

long saldo;
int digit;
int digit_saldo;

long OLDsaldo;
int OLDdigit;

void dump_byte_array(byte *buffer, byte bufferSize);
void resetReader();
void readRFID();

// DISPLAY
#define BUTTON_TEXTSIZE 6
#define BUTTON_W 120
#define BUTTON_H 120
#define BUTTON_SPACING_X 40

TFT_eSPI_Button button[3];
char buttonLabel[3][3] = {"1", "2", "3"};
int restItems[3];

bool flag_pay = false;
bool less_balance_flag = false;
int flag_wait = 0;
String UID = "";
String date = "";
String data = "";

void drawButton();
void drawRestofItems();
void removeRestItems();
void reduceItems(int index);
void drawMenu();
void drawWaitPayment(String product_number, bool draw);
void drawSuccessPayment(bool success, bool draw);
void waitingAndReset();
void motorDriver(int indexDriver);
void drawLessBalance(bool draw);
void check_balance();
void drawBalance(int saldo, bool draw);
void handlePressedButton(uint8_t index, uint16_t x, uint16_t y);
void processButtonPress(uint8_t b);
void handleSuccessfulPayment(String product_number);
void handleFailedPayment(String product_number, bool less_balance);
void drawItemEmpty(bool draw);

void setup(){
  Serial.begin(115200);
  tft.setRotation(1);
  uint16_t calData[5] = { 331, 3285, 479, 2813, 5 };
  tft.setTouch(calData);

  if (! rtc.begin()) {
    Serial.println("RTC module is NOT found");
  }
   
  // rtc.adjust(DateTime(F(_DATE), F(TIME_)));

  EEPROM.begin(EEPROM_SIZE);
   for (int i = 0; i < 3; i++) EEPROM.write(i, 10);
   EEPROM.commit();

  for (int i = 0; i < 3; i++) restItems[i] = EEPROM.read(i);

  drawMenu();
  drawRestofItems();
  drawButton();

  for (int i = 0; i < 3; i++){
    pinMode(listEnablePin[i], OUTPUT);
    pinMode(listMotor1Pin[i], OUTPUT);
    pinMode(listMotor2Pin[i], OUTPUT);
  }

  // pinMode(irPin, INPUT_PULLUP);
  ledcSetup(pwmChannel, freq, resolution);

  SPI.begin();        
  mfrc522.PCD_Init(); 

  for (byte i = 0; i < 6; i++) {
      key.keyByte[i] = 0xFF;
  }
  if (tagihan > 255000){
    while(1){
    }
  }

  digit = tagihan/1000;
}

void loop(){
  check_balance();
  if (ceksaldo){
    drawBalance(sisasaldo, true);
    waitingAndReset();
    drawBalance(sisasaldo, false);
    sisasaldo = 0;
    ceksaldo = false;
  }

  uint16_t t_x = 0, t_y = 0;

  for (uint8_t index = 0; index < 3; index++){
    handlePressedButton(index, t_x, t_y);
  }
}

void handlePressedButton(uint8_t index, uint16_t x, uint16_t y){
  bool pressed = tft.getTouch(&x, &y);

  if (pressed && button[index].contains(x, y)){
    button[index].press(true); // tell the button it is pressed
  } else {
    button[index].press(false); // tell the button it is NOT pressed
  }

  if (button[index].justReleased()){
    button[index].drawButton(); // draw normal
  }

  if (button[index].justPressed()){
    if (restItems[index] <= 0){
      drawItemEmpty(true);
      waitingAndReset();
      drawItemEmpty(false);
      return;
    } else {
       processButtonPress(index);
    }
  }
}

void drawItemEmpty(bool draw){
  if (draw) tft.setTextColor(TFT_BLACK); else tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(C_BASELINE);
  tft.setTextSize(2);
  tft.drawString("Item kosong", tft.width()/2, 240, 1);
}

void processButtonPress(uint8_t index) {
  button[index].drawButton(true); // Draw the button in its pressed state
  String product_number = String(index + 1, DEC);
  drawWaitPayment(product_number, true);

  DateTime now = rtc.now();
  date = String(now.day(), DEC) + "/" + String(now.month(), DEC) + "/" + String(now.year(), DEC) + " " + String(now.hour(), DEC) + ":" + String(now.minute(), DEC) + ":" + String(now.second(), DEC);
  
  while (flag_wait < 10000 && !flag_pay) {
    flag_wait += 10;
    delay(10);
    readRFID();

    if (less_balance_flag) {
      handleFailedPayment(product_number, true);
      return;
    }
  }

  if (flag_pay) {
    handleSuccessfulPayment(product_number);
  } else {
    handleFailedPayment(product_number, false);
  }
}

void handleFailedPayment(String product_number, bool less_balance) {
  drawWaitPayment(product_number, false);
  drawSuccessPayment(false, true);
  if (less_balance) {
    drawLessBalance(true);
    String old_saldo = String(OLDsaldo);
    // make fail at the last column
    data = "data: " + product_number + "," + UID + "," + date + "," + old_saldo + "," + old_saldo + "," + "0";
    Serial.println(data);
  }
  // data = uid + date + success/fail with csv format
  // data = "data: " + UID + "," + date + "," + "fail";

  waitingAndReset();
  drawSuccessPayment(false, false);
  if (less_balance) {
    drawLessBalance(false);
  }
  less_balance_flag = false;
}

void handleSuccessfulPayment(String product_number) {
  drawWaitPayment(product_number, false);
  drawSuccessPayment(true, true);
  String old_saldo = String(OLDsaldo);
  String new_saldo = String(saldo);
  data = "data: " + product_number + "," + UID + "," + date + "," + old_saldo + "," + new_saldo + "," + "1";
  Serial.println(data);

  waitingAndReset();
  motorDriver(product_number.toInt() - 1);
  drawSuccessPayment(true, false);
  reduceItems(product_number.toInt() - 1);
}

void drawMenu(){
  tft.init();
  tft.fillScreen(TFT_WHITE);
  tft.setTextDatum(C_BASELINE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(tft.width(), 5);
  tft.drawString("Tekan salah satu tombol", tft.width()/2, 20, 1);
  tft.drawString("untuk memilih item", tft.width()/2, 40, 1);
}

int BUTTON_X[3] = {- BUTTON_W - BUTTON_SPACING_X, 0, BUTTON_W + BUTTON_SPACING_X};

void drawButton(){
  
  for (uint8_t b = 0; b < 3; b++){
    button[b].initButton(&tft, tft.width()/2 + BUTTON_X[b], tft.height()/2, BUTTON_W, BUTTON_H, TFT_BLACK, TFT_WHITE, TFT_BLACK, buttonLabel[b], BUTTON_TEXTSIZE);
    button[b].drawButton();
  }
}

void drawRestofItems(){
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);
  for (uint8_t b = 0; b < 3; b++){
    tft.drawNumber(restItems[b], tft.width()/2 + BUTTON_X[b], tft.height()/2 - BUTTON_H/2 - 20, 1);
  }
}

void removeRestItems(){
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  for (uint8_t b = 0; b < 3; b++){
    tft.drawNumber(restItems[b], tft.width()/2 + BUTTON_X[b], tft.height()/2 - BUTTON_H/2 - 20, 1);
  }
}

void reduceItems(int index){
  EEPROM.write(index, restItems[index]-1);
  EEPROM.commit();
  removeRestItems();
  restItems[index]--;
  drawRestofItems();
}

void drawWaitPayment(String product_number, bool draw){
  if (draw) tft.setTextColor(TFT_BLACK); else tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(C_BASELINE);
  tft.setTextSize(2);
  String memilih_produk = "Memilih produk " + product_number;
  tft.drawString(memilih_produk, tft.width()/2, 240, 1);
  tft.drawString("Menunggu pembayaran...", tft.width()/2, 260, 1);
}

void drawLessBalance(bool draw){
  if (draw) tft.setTextColor(TFT_BLACK); else tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(C_BASELINE);
  tft.setTextSize(2);
  tft.drawString("Saldo tidak cukup", tft.width()/2, 260, 1);
}

void drawSuccessPayment(bool success, bool draw){
  if (draw) tft.setTextColor(TFT_BLACK); else tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(C_BASELINE);
  tft.setTextSize(2);
  if (success) tft.drawString("Pembayaran berhasil", tft.width()/2, 240, 1); else tft.drawString("Pembayaran gagal", tft.width()/2, 240, 1);
}

void drawBalance(int saldo, bool draw){
  if (draw) tft.setTextColor(TFT_BLACK); else tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(C_BASELINE);
  tft.setTextSize(2);
  tft.drawString("Saldo anda : " + String(saldo), tft.width()/2, 240, 1);
}

void waitingAndReset(){
  flag_pay = false;
  flag_wait = 0;
  UID = "";
  date = "";
  data = "";
  delay(5000);
}

void motorDriver(int indexDriver) {
  Serial.print("Memutar motor: ");
  Serial.println(indexDriver);
  // Menghubungkan saluran PWM dengan pin enable
  ledcAttachPin(listEnablePin[indexDriver], pwmChannel);
  // Mengatur arah putaran motor
  digitalWrite(listMotor1Pin[indexDriver], HIGH);
  digitalWrite(listMotor2Pin[indexDriver], LOW);
  // Mengatur kecepatan motor
  ledcWrite(pwmChannel, 255); // Mengatur kecepatan ke maksimum
  
  // Menghitung waktu berdasarkan rumus
  float timeInSeconds = 0.0;
  switch (indexDriver) {
    case 0:
      timeInSeconds = 3.8405 - 0.20727 * (10 - restItems[indexDriver]);
      break;
    case 1:
      timeInSeconds = 3.78767 - 0.2118 * (10 - restItems[indexDriver]);
      break;
    case 2:
      timeInSeconds = 3.74683 - 0.2262 * (10 - restItems[indexDriver]);
      break;
    default:
      break;
  }

  // Menunggu waktu yang ditentukan sebelum mematikan motor
  delay(timeInSeconds * 1000); // Konversi detik ke milidetik
  
  Serial.println("motor stop");
  // Mematikan motor
  ledcWrite(pwmChannel, 0);
  // Melepaskan saluran PWM dari pin enable
  ledcDetachPin(listEnablePin[indexDriver]);
}

void readRFID() {
  if (notif) {
    notif = false;
  }
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
      piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
      piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
    notif = true;
    resetReader();
    return;
  }

  byte sector = 1;
  byte blockAddr = 4;
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);

  mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);

  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    resetReader();
    return;
  }
  OLDdigit = buffer[0];
  OLDsaldo = OLDdigit;
  OLDsaldo *= 1000;

  for (byte i = 0; i < mfrc522.uid.size; i++) {
    UID.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    UID.concat(String(mfrc522.uid.uidByte[i], HEX));
  }

  if (OLDdigit < digit) {
    less_balance_flag = true;
    resetReader();
    return;
  }

  OLDdigit -= digit;

  byte dataBlock[] = {static_cast<byte>(OLDdigit), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(blockAddr, dataBlock, 16);
  if (status != MFRC522::STATUS_OK) {
  }

  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
  }
  if (buffer[0] == dataBlock[0]) {
    saldo = buffer[0];
    saldo *= 1000;
  } else {
  }

  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println();
  flag_pay = true;
  resetReader();
}

void check_balance() {
  bool isisaldo = false;
  if ( ! mfrc522.PICC_IsNewCardPresent()){
      return;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()){
      return;
  }
  
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  if (    piccType != MFRC522::PICC_TYPE_MIFARE_MINI
      &&  piccType != MFRC522::PICC_TYPE_MIFARE_1K
      &&  piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
      notif = true;
      // delay(2000);
      resetReader();
      return;
  }

  // that is: sector #1, covering block #4 up to and including block #7
  byte sector         = 1;
  byte blockAddr      = 4;
  
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);

  // Show the whole sector as it currently is
  mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);

  if (!isisaldo) {
    status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(blockAddr, buffer, &size);
    if (status != MFRC522::STATUS_OK) {
        saldo = 0;
        digit_saldo = 0;
        resetReader();
        return;
    }
    saldo = buffer[0];
    saldo *= 1000;
    ceksaldo = true;
    sisasaldo = saldo;
  }
  saldo = 0;
  digit_saldo = 0;
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  resetReader();
}

void dump_byte_array(byte *buffer, byte bufferSize) {
    for (byte i = 0; i < bufferSize; i++) {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}

void resetReader(){
  // Halt PICC
  mfrc522.PICC_HaltA();
  // Stop encryption on PCD
  mfrc522.PCD_StopCrypto1();

  notif = true;
}