#include <Mylib.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <WiFiUdp.h>
#include <ESP32Time.h>
#include <NTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <FastBot.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <AsyncTCP.h>
#include <WiFiMulti.h>
#include <LCD_I2C.h>

WiFiMulti wifiMulti;

#define INFLUXDB_URL "http://192.168.1.200:8086"
#define INFLUXDB_TOKEN "HIl1thjspeKZBP2pdxay6QJmxYxMv5GomRJWiTGr0DcK_iPiFFYbB-EOXf4g9wKmOEf0KYO99DO1WNmcyxDD3g=="
#define INFLUXDB_ORG "efe810df40c0f3b1"
#define INFLUXDB_BUCKET "Hydroponic2"

const int oneWireBus = 17;

//Идентификатор бота
#define BOT_TOKEN "6898848903:AAEQV6wRl9uTf2puSjpBmnVZxgRem2iJOCs"
FastBot bot(BOT_TOKEN);

const int RelayPin = 25;

File dataFile;
ESP32Time rtc(0);
const byte NumberOfSensors = 2;

const char *ssid = "ASR";
const char *password = "94Cv45rt94";

AsyncWebServer server(80);

const int chipSelect = 5;

static double g_temperature;
static int g_moisure;
static String PumpSwitch;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

OneWire oneWire(oneWireBus);

DallasTemperature sensors(&oneWire);

LCD_I2C lcd(0x27, 16, 2);

Point sensor("2");

void setup()
{
    Serial.begin(115200);

    pinMode(RelayPin, OUTPUT);
    digitalWrite(RelayPin, LOW);

    xTaskCreatePinnedToCore(loop2, "loop2", 10000, NULL, 0, NULL, 0);

    Serial.print("Connecting to ");

    Serial.println(ssid);

    initWiFi(ssid,password);
    initSD();

    lcd.begin();
    lcd.backlight();
    //initTDS(TdsSensorPin);

    sensors.begin();

    timeClient.begin();
    timeClient.setTimeOffset(10800);

    //Отправка кода веб-страницы
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {request->send(SD, "/index.html", "text/html");});

    //Обработка запросов
    server.on("/ppm", HTTP_GET, [](AsyncWebServerRequest *request)
              { Serial.println("8-800-555-35-35");delay(100);});
    server.on("/ph", HTTP_GET, [](AsyncWebServerRequest *request)
              { Serial.println("8-800-944445448445489456478945894234875-35-35");delay(100);});
    server.on("/moisure1", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send_P(200, "text/plain", String(getMoisure(34)).c_str()); });
    server.on("/moisure2", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send_P(200, "text/plain", String(getMoisure(35)).c_str()); });
    server.on("/watch", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send_P(200, "text/plain", String(rtc.getTime()).c_str()); });
    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send_P(200, "text/plain", String(g_temperature).c_str()); });

    server.serveStatic("/",SD,"/");
    server.begin();

  }
String time_on;
long LastForInfluxDB;
long LastForTemp;
void loop()
{
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  rtc.setTime(epochTime);
  bot.tick();
  if (millis()-LastForTemp>5000){
    g_temperature = GetTemperature();
    LastForTemp = millis();
    ResetDisplay();
  }
  if (millis()%3000<90){
    g_moisure = ResetMidMoisure();
  }
  if ((millis() - LastForInfluxDB)>30000){
    LastForInfluxDB = millis();
    SendDataToInfluxDB(getMoisure(34),getMoisure(35));
  }

}

void loop2(void *pvParameters)
{
  //таймер для записи в файл
  unsigned long previousMillis;

  // интервал записи в файл в мс
  const long interval = 90000;

  while (WiFi.status() != WL_CONNECTED){delay(500);}


  delay(5000);
  while (1)
  {
    int mos = getMoisure(34);
    int mos1  = getMoisure(34);

    // Запись на карту (раз в 15 мин) 900000=60000mc*15
    if ((millis() - previousMillis)>interval){
        String filename1 = String(String(rtc.getDay()) + "-" + String(int(rtc.getMonth())+1) + "-" + String(rtc.getYear()));
        StoreDataToSd(mos, mos1, time_on, filename1);
        previousMillis = millis();
    }

    // Обработка бота
    bot.attach(newMsg);
    bot.setChatID(-1002041443761);
    time_on = GetPumpTimeOn();
    delay(50);
    if(millis()>300000){
      esp_restart();
    }
  }
}
/*====================   ФУНКЦИИ   ==================*/



// обработчик ТГ

unsigned long previousMillis1;

void newMsg(FB_msg &msg)
{
  const long interval = 6000;
  //((mos+mos1)/num)
  bool longMsg;
  unsigned long now1 = millis();

  if (String(msg.text) == "/send_mm")
  {
    if ((millis()- previousMillis1) > interval and g_moisure < 49 )
    {
      //собираем длинное сообщение
      longMsg = true;
      previousMillis1 = now1;
    }
    else
    {
      //собираем короткое сообщение
      longMsg = false;
    }
    bot.sendMessage(CreateMessage(longMsg));
  }
}

//включает насос и возвращает время выключения
String GetPumpTimeOn()
{
  ResetMidMoisure();
  if (g_moisure < 49 and g_temperature < 30.1) {

    digitalWrite(RelayPin,LOW);
    delay(15000);
    digitalWrite(RelayPin,HIGH );


    return String(rtc.getTime());}

  if (time_on==""){
    return String(rtc.getTime());
  }
  return time_on;

}

//Собирает сообщение для ТГ бота
String CreateMessage(bool LendthOfMsg){
  String time = String(rtc.getHour(true)) + String(":") + String(rtc.getMinute());
  String MoisureValue = String(String(" ") +String(g_moisure) + String(" %"));

  /*if (mid<49 and LendthOfMsg== true and temp>0){
    return String("Внимание! Средняя влажность упала ниже 48 % и составляет " + MoisureValue + "\n" + "В связи с высокой температурой окружающей среды полив будет произведен позже");
  }
  String BeginOfLine = String("Средняя влажность на " + time);
  return String(BeginOfLine + "\n" + MoisureValue);*/
  if (LendthOfMsg){
      Serial.println(g_temperature);
      if( g_temperature >= 30.0){
            return String("Внимание! Средняя влажность упала ниже 48 % и составляет " + MoisureValue + "\n" + "В связи с высокой температурой окружающей среды полив будет произведен позже");
      }
      else if (g_temperature < 30.0){
            return String("Внимание! Средняя влажность упала ниже 48 % и составляет " + MoisureValue + "\n");
      }
    }
  else{
        String BeginOfLine = String("Средняя влажность на " + time);
        return String(BeginOfLine + "\n" + MoisureValue);
  }
}
void StoreDataToSd(int mos1, int mos, String time_on, String filename1)
{
  const String filename = "/" + filename1 + ".csv";

  if (SD.exists(filename))
  {
    dataFile = SD.open(filename.c_str(), FILE_APPEND);
    Serial.println("file exists");
  }
  else
  {
    dataFile = SD.open(filename.c_str(), FILE_WRITE);
    Serial.println("file created");
  }

  dataFile.print(mos);
  dataFile.print(";");

  dataFile.print(mos1);
  dataFile.print(";");

  dataFile.print(time_on);
  dataFile.print(";");

  dataFile.print(int(g_temperature));
  dataFile.println(";");

  dataFile.close();

}
//Считывает Tds
/*float GetTds()
{
    //gravityTds.setTemperature(temperature);  set the temperature and execute temperature compensation
    gravityTds.update();  //sample and calculate
    float TdsValue = gravityTds.getTdsValue();  // then get the value
    return round(TdsValue);
}

//Задает значения по умолчанию и инициализирует TDs метр
void initTDS(byte TdsSensorPin)
{   gravityTds.setPin(TdsSensorPin);
    gravityTds.setAref(5.0);
    gravityTds.setAdcRange(4096);
    gravityTds.begin();
}*/
float GetTemperature(){
  sensors.requestTemperatures();
  delay(750);
  float CelsiusTemp = sensors.getTempCByIndex(0);
  return CelsiusTemp;
}

int ResetMidMoisure(){
  return int((getMoisure(34) + getMoisure(35))/2);
}

void ResetDisplay(){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("hum1:");
  lcd.print(getMoisure(34));
  lcd.print(" hum2:");
  lcd.print(getMoisure(35));
  lcd.setCursor(0,1);
  lcd.print("Temperature:");
  lcd.print(int(g_temperature));
}
void SendDataToInfluxDB(int moisure1, int moisure2 ){
  InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

  sensor.clearFields();
  sensor.addField("moisure1", moisure1);
  sensor.addField("moisure2", moisure2);
  sensor.addField("temperature" ,g_temperature);


  Serial.print("Writing: ");
  Serial.println(sensor.toLineProtocol());

  // Check WiFi connection and reconnect if needed
  // Write point
  if (!client.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client.getLastErrorMessage());
  }
}