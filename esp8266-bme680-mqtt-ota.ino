/*
Falko Höltzer
Date created: 16.02.23
Version: V1.2
IDE: Arduino IDE 1.8.16

Required libraries 
 - BSEC Software Library by Bosch Sensortec Enviroment Cluster V1.6.1480 - not 1.7.1492
 - PubSubClient by Nick ‘O Leary V2.8.0
 - ArduinoOTA by Juraj Andrassy V1.0.7  
Borad Wirering:
BME680      WeMos D1 Mini 
VCC         3.3V
GND         GND
SCL         D1 / GPIO5
SDA         D2 / GPIO4
SDO         GND
*/

#include <EEPROM.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include "bsec.h"
// Create an object of the class Bsec
Bsec bme680;
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
uint16_t stateUpdateCounter = 0;
String output;

//Write sensor calibration to file
const uint8_t bsec_config_iaq[] = {
#include "config/generic_33v_300s_4d/bsec_iaq.txt"
};
#define STATE_SAVE_PERIOD	UINT32_C(360 * 60 * 1000) // 360 minutes - 4 times a day

// Helfer functions for calibration
void checkIaqSensorStatus(void);
void errLeds(void);
void loadState(void);
void updateState(void);

// 
#define wifi_ssid "YOUR-WLAN-SID"
#define wifi_password "YOUR-WLAN-PWD"
#define mqtt_server "IP-ADRESS-MQTT-SERVER" //Format xxx.xxx.xxx.xxx
#define mqtt_user ""         
#define mqtt_password ""
#define mqtt_port 1883
#define ESPHostname "NAME-FOR_OTA"
#define ESPPwd "PWD-FOR_OTA" 
String clientId = "NAME-FOR-MQTT-CLIENT"; //MQTT reconnect

#define temp_topic "esp8266/bme680/temperature"
#define hum_topic "esp8266/bme680/humidity"
#define druck_topic "esp8266/bme680/pressure"
#define iaq_topic "esp8266/bme680/gas"
#define iaq_ac_topic "esp8266/bme680/iaqAc"
#define eco2_topic "esp8266/bme680/co2"
#define voc_topic "esp8266/bme680/voc"
#define status_topic "esp8266/bme680/status"
#define error_topic "esp8266/bme680/error"
#define intopic "Sen-Schlaf-"

float temp = 0.0;
float hum = 0.0;
int druck = 0.0;
int IAQ = 10;
int IAQac = 0;
int eCO2 = 0;
int voc = 0;
int error_Status = 0;
const String error_Message="BME680 Sensor not found :-(";
const int sensorTakt = 3000; //read sensor every 3 seconds
long lastMsg = 0;
char msg[50];

WiFiClient espClient;  
PubSubClient client(espClient);

void setup() {
  EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1); // 1st address for the length
  Serial.begin(115200);
  setup_wifi();
  ArduinoOTA.setHostname(ESPHostname);
  ArduinoOTA.setPassword(ESPPwd);
  ArduinoOTA.begin();
  client.setServer(mqtt_server, mqtt_port); 
  client.setCallback(callback); 
  
  Wire.begin(4,5);
  bme680.begin(BME680_I2C_ADDR_PRIMARY, Wire);
  Serial.println(output);
  checkIaqSensorStatus();
  bme680.setConfig(bsec_config_iaq);
  checkIaqSensorStatus();

  loadState();
    
  bsec_virtual_sensor_t sensorList[7] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };
  bme680.updateSubscription(sensorList, 7, BSEC_SAMPLE_RATE_LP); //3 sek, Strom <1mA
  //bme680.updateSubscription(sensorList, 7, BSEC_SAMPLE_RATE_ULP); //300 sek, Strom <0.1mA, Batterienutzung  
  checkIaqSensorStatus();
  // header
  output = "Timestamp [ms], raw temperature [°C], pressure [hPa], raw relative humidity [%], gas [Ohm], IAQ, IAQ accuracy, temperature [°C], relative humidity [%]";
  Serial.println(output);
}

void loop() {
   if (!client.connected()) {
    reconnect();
  }
  client.loop();
  ArduinoOTA.handle(); 
  long now = millis();
  if (now - lastMsg > sensorTakt) {   
    lastMsg = now;
    getBME680Values();
  }
}

void getBME680Values() {
  if (bme680.run()) { 
      error_Status = 0;
      client.publish(error_topic, String(error_Status).c_str(), true);
      
      temp = (bme680.temperature);
      Serial.print("Temp: "); Serial.print(String(temp).c_str()); Serial.print(" C\t");
      client.publish(temp_topic, String(temp).c_str(), true);

      hum = (bme680.humidity);
      Serial.print("Hum: "); Serial.print(String(hum).c_str()); Serial.print(" %\t");
      client.publish(hum_topic, String(hum).c_str(), true);

      druck = (bme680.pressure)/100;
      Serial.print("Druck: "); Serial.print(String(druck).c_str()); Serial.print(" hPa\t");
      client.publish(druck_topic, String(druck).c_str(), true);

      IAQ = (bme680.iaq);
      Serial.print("IAQ: "); Serial.print(String(IAQ).c_str()); Serial.print("\t");
      client.publish(iaq_topic, String(IAQ).c_str(), true);

      IAQac = (bme680.iaqAccuracy);
      Serial.print("IAQ-ac: "); Serial.print(String(IAQac).c_str()); Serial.print("\t");
      client.publish(iaq_ac_topic, String(IAQac).c_str(), true);

      eCO2 = (bme680.co2Equivalent);
      Serial.print("CO2: "); Serial.print(String(eCO2).c_str()); Serial.print("\t");
      client.publish(eco2_topic, String(eCO2).c_str(), true);

      voc = (bme680.breathVocEquivalent);
      Serial.print("VOC: "); Serial.print(String(voc).c_str()); Serial.println("\t");
      client.publish(voc_topic, String(voc).c_str(), true);
    } 
    else if (bme680.bme680Status != BME680_OK) {
        error_Status = 1;
        Serial.println(String(error_Message).c_str());
        client.publish(status_topic, String(error_Message).c_str(), true);
        client.publish(error_topic, String(error_Status).c_str(), true);        
        delay(1000);
    }
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
  if (String(topic) == intopic) {
    // Room for Code on an intopic
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(status_topic, ESPHostname);
      // ... and resubscribe
      client.subscribe(intopic);
  } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// Helper function definitions
void checkIaqSensorStatus(void)
{
  if (bme680.status != BSEC_OK) {
    if (bme680.status < BSEC_OK) {
      output = "BSEC error code : " + String(bme680.status);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BSEC warning code : " + String(bme680.status);
      Serial.println(output);
    }
  }

  if (bme680.bme680Status != BME680_OK) {
    if (bme680.bme680Status < BME680_OK) {
      output = "BME680 error code : " + String(bme680.bme680Status);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BME680 warning code : " + String(bme680.bme680Status);
      Serial.println(output);
    }
  }
  bme680.status = BSEC_OK;
}

void errLeds(void)
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
}

void loadState(void)
{
  if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE) {
    // Existing state in EEPROM
    Serial.println("Reading state from EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
      bsecState[i] = EEPROM.read(i + 1);
      Serial.println(bsecState[i], HEX);
    }

    bme680.setState(bsecState);
    checkIaqSensorStatus();
  } else {
    // Erase the EEPROM with zeroes
    Serial.println("Erasing EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++)
      EEPROM.write(i, 0);

    EEPROM.commit();
  }
}

void updateState(void)
{
  bool update = false;
  /* Set a trigger to save the state. Here, the state is saved every STATE_SAVE_PERIOD with the first state being saved once the algorithm achieves full calibration, i.e. iaqAccuracy = 3 */
  if (stateUpdateCounter == 0) {
    if (bme680.iaqAccuracy >= 3) {
      update = true;
      stateUpdateCounter++;
    }
  } else {
    /* Update every STATE_SAVE_PERIOD milliseconds */
    if ((stateUpdateCounter * STATE_SAVE_PERIOD) < millis()) {
      update = true;
      stateUpdateCounter++;
    }
  }

  if (update) {
    bme680.getState(bsecState);
    checkIaqSensorStatus();

    Serial.println("Writing state to EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE ; i++) {
      EEPROM.write(i + 1, bsecState[i]);
      Serial.println(bsecState[i], HEX);
    }

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
  }
}
