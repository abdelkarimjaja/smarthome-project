/**
* @file smarthome.ino
* @authors ELMehdi Atri 
*          JAJA Abdelkarim   
*          Armali Houda
**/
#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#include <analogWrite.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid ="SSID";//"SM-G960W3620";// "REPLACE_WITH_YOUR_SSID";
const char* password ="PassWord";// "REPLACE_WITH_YOUR_PASSWORD";
const char* mqtt_server ="test.mosquitto.org";//"192.168.1.6";


// WiFi AP SSID
#define WIFI_SSID ssid
// WiFi password
#define WIFI_PASSWORD password

// InfluxDB v2 server url, e.g. https://eu-central-1-1.aws.cloud2.influxdata.com (Use: InfluxDB UI -> Load Data -> Client Libraries)
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
// InfluxDB v2 server or cloud API token (Use: InfluxDB UI -> Data -> API Tokens -> Generate API Token)
#define INFLUXDB_TOKEN "KHtUDvjZiEYoAnrubc4_XXaNTgPVS9LFd14nwCQwncC5MLO3-ZGssi9_2TJkf4WL-AwQxegeo5h0vK5cbEKl7A=="
// InfluxDB v2 organization id (Use: InfluxDB UI -> User -> About -> Common Ids )
#define INFLUXDB_ORG "influxorg"
// InfluxDB v2 bucket name (Use: InfluxDB UI ->  Data -> Buckets)
#define INFLUXDB_BUCKET "smarthome"

#define TZ_INFO "PST8PDT"


WiFiClient espClient;
PubSubClient client(espClient);

#define SS_PIN  5  // ESP32 pin GIOP5 
#define RST_PIN 27 // ESP32 pin GIOP27 
#define RELAY 22 // ESP32 pin GIOP22 connects to relay
#define Buzzer 15
#define Red_LED 13
#define Green_LED 12
#define LDR_Sensor 35
#define LED_PIN 4
//use first channel of 16 channels (started from zero)
#define LEDC_CHANNEL_0 0
// use 13 bit precission for LEDC timer
#define LEDC_TIMER_13_BIT 13
// use 5000 Hz as a LEDC base frequencyZ
#define LEDC_BASE_FREQ 5000

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client_db(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

void timeSync() {
  // Synchronize UTC time with NTP servers
  // Accurate time is necessary for certificate validaton and writing in batches
  configTime(0, 0, "pool.ntp.org", "time.nis.gov");
  // Set timezone
  setenv("TZ", TZ_INFO, 1);

  // Wait till time is synced
  Serial.print("Syncing time");
  int i = 0;
  while (time(nullptr) < 1000000000ul && i < 100) {
    Serial.print(".");
    delay(100);
    i++;
  }
  Serial.println();

  // Show time
  time_t tnow = time(nullptr);
  Serial.print("Synchronized time: ");
  Serial.println(String(ctime(&tnow)));
}


// Data point
Point sensor("smarthome");

byte keyTagUID[4] = {0xFF, 0xFF, 0xFF, 0xFF};

const char* subtopic1 = "LED"; 
const char* subtopic2 = "Authorized";
const char* subtopic3 = "Denied";
const char* pubtopic1 = "RFID_Read";
const char* pubtopic2 = "Luminosity_Sensor";
const char* pubtopic3 = "Authorized";
const char* pubtopic4 = "Denied";

MFRC522 rfid(SS_PIN, RST_PIN);
char RFID_Code;
long lastMsg = 0;
char msg[50];
int value = 0;
double* card_code;
bool enter = false ;
String recRFID;
String card_id;
String readString;
int Brightness;
int Lum;
byte readCard[4]; 
char LumString[8];

void ledcAnalogWrite(uint32_t value, uint32_t valueMax = 3400) {
// calculate duty
uint32_t duty = (LEDC_BASE_FREQ / valueMax) * min(value, valueMax);
// write duty to LEDC
ledcWrite(LEDC_CHANNEL_0, duty);
}

void setup() 
{

ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
ledcAttachPin(LED_PIN, LEDC_CHANNEL_0);

Serial.begin(115200);
 setup_wifi();
 client.setServer(mqtt_server, 1883);
 client.setCallback(callback);
  SPI.begin(); // init SPI bus
  rfid.PCD_Init(); // init MFRC522

  Serial.println("Tap an RFID/NFC tag on the RFID-RC522 reader");
  pinMode(Red_LED, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(LDR_Sensor, INPUT);
  pinMode(Green_LED, OUTPUT);
  pinMode(Buzzer, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_Sensor, INPUT);

   // Setup wifi
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to wifi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  // Add tags
  sensor.addTag("device", DEVICE);

  // Sync time for certificate validation
  timeSync();

  // Check server connection
  if (client_db.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client_db.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client_db.getLastErrorMessage());
  }
}
void array_to_string(byte a[],unsigned int len,char buffer[])
{
  for(unsigned int i=0;i<len;i++)
  {
    byte nib1=(a[i]>>4)&0x0F;
    byte nib2=(a[i]>>0)&0x0F;
    buffer[i*2+0]=nib1 < 0x0A ? '0' + nib1 : 'A'+ nib1 - 0x0A;
    buffer[i*2+1]=nib2 < 0x0A ? '0' + nib2 : 'A'+ nib2 - 0x0A;
  }
  buffer[len*2]='\0';
}
void setup_wifi() 
{
  delay(10);
 // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
void callback(char* topic, byte* message, unsigned int length) 
{
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  Serial.println();
  String messageTemp;
  for (int i = 0; i < length; i++) 
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
 Serial.println();

 if (String(topic) == "Authorized") 
 {
    Serial.print("Authorized Access");
    if(messageTemp == "Acces_Granted")
     Serial.println();
    {
    Serial.println("Lock Opened");
    Serial.println();
    digitalWrite(Green_LED, HIGH);
    digitalWrite(RELAY, HIGH);
    delay(5000);
    digitalWrite(Green_LED, LOW);
    digitalWrite(RELAY, LOW);
    }
 }
 if (String(topic) == "Denied") 
 {
    if(messageTemp == "Access_Denied")
    {
    Serial.println("Access_Denied");
    digitalWrite(Red_LED, HIGH);
    digitalWrite(Buzzer, HIGH);
    delay(5000);
    digitalWrite(Red_LED, LOW);
    digitalWrite(Buzzer, LOW);
    }
 }
  if (String(topic) == "Green_LED_CMD") {
 Serial.print("Green_LED: Changing output to ");
 Serial.println();
 if(messageTemp == "true"){
 Serial.println("on");
 Serial.println();
 digitalWrite(Green_LED, HIGH);
 }
 else if(messageTemp == "false"){
 Serial.println("off");
  Serial.println();
 digitalWrite(Green_LED, LOW);
 }
 }
 if (String(topic) == "Red_LED_CMD") {
 Serial.print("Red_LED: Changing output to ");
 if(messageTemp == "true"){
 Serial.println("on");
 digitalWrite(Red_LED, HIGH);
 }
 else if(messageTemp == "false"){
 Serial.println("off");
 digitalWrite(Red_LED, LOW);
 }
 }
 if (String(topic) == "LED_CMD") {
 Serial.print("LED: Changing output to ");
 if(messageTemp == "true"){
 Serial.println("on");
  ledcAnalogWrite(Lum);
 }
 else if(messageTemp == "false"){
 Serial.println("off");
  ledcAnalogWrite(Lum);
 }
 }
  if (String(topic) == "Buzzer_CMD") {
 Serial.print("Buzzer: Changing output to ");
 if(messageTemp == "true"){
 Serial.println("on");
 digitalWrite(Buzzer, HIGH);
 delay(1000);
 digitalWrite(Buzzer, LOW); 
 }
 else if(messageTemp == "false"){
 Serial.println("off");
 digitalWrite(Buzzer, LOW);
 }
 }
}

void reconnect() 
{
 // Loop until we're reconnected
    while (!client.connected()) 
    {
    Serial.print("Attempting MQTT connection...");
      if (client.connect("ESP32Client")) 
      {
      Serial.println("connected");
      client.subscribe("Denied");
      client.subscribe("Authorized");
      client.subscribe("Green_LED_CMD");
      client.subscribe("Red_LED_CMD");
      client.subscribe("Buzzer_CMD");
      client.subscribe("LED_CMD");
      } 
    else 
    {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
    }
    }
}
void loop() 
{
     sensor.clearFields();

    Brightness = analogRead(LDR_Sensor);
    Lum = map(Brightness, 0, 3400, 3400, 0);
    ledcAnalogWrite(Lum);
    dtostrf(Brightness, 1, 2, LumString);
    Serial.println("Luminosity to publish: "+String(LumString));
    
   sensor.addField("lum",LumString);
   sensor.toLineProtocol();

    client.publish("Luminosity_Sensor", LumString);
    delay(1000);
     
  if (!client.connected()) 
  {
    reconnect();
  }
  client.loop();
  long now = millis();
  if (now - lastMsg > 1000) 
  {
    lastMsg = now;
  }
  if ( ! rfid.PICC_IsNewCardPresent()) 
  {
    return;
  }
  if ( ! rfid.PICC_ReadCardSerial()) 
  {
    return;
  }
  // Store measured value into point
//  sensor.clearFields();
  Serial.print("UID tag :");
  String content= "";
  byte letter;
  for (int i = 0; i < rfid.uid.size; i++) {  
    readCard[i] = rfid.uid.uidByte[i];   
    Serial.print(readCard[i], HEX);
     
 
    }
//     int rfid_value = atoi(readCard);
      
   Serial.println();
   char msg[50];
   msg[0]='\0';
   array_to_string(readCard,4,msg);
   client.publish(pubtopic1,msg);
   rfid.PICC_HaltA(); 
      
   Serial.println("RFID: "+String(msg));
   sensor.addField("rfid",msg);
   Serial.println(sensor.toLineProtocol());
   
   client.setCallback(callback);
   delay(500);
   
  
  // Report RSSI of currently connected network
  
  // sensor.addField("dist", dist); 
  // Print what are we exactly writing
 
  // If no Wifi signal, try to reconnect it
  if ((WiFi.RSSI() == 0) && (wifiMulti.run() != WL_CONNECTED))
    Serial.println("Wifi connection lost");
  // Write point
  if (!client_db.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(client_db.getLastErrorMessage());
  }

  //Wait 10s
  Serial.println("Wait 1s");
  delay(1000);
}
