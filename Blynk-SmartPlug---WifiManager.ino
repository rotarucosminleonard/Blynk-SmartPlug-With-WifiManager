#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson


#define AccessPointName "Blynk-Plug V3.02"
#define password "mypassword"


#define BLYNK_PRINT Serial
#include <BlynkSimpleEsp8266.h>

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "8080";
char blynk_token[33] = "YOUR_BLYNK_TOKEN";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


// Blynk 

#include "DHTesp.h"   // https://github.com/beegee-tokyo/DHTesp
DHTesp dht;
float t;
float h;

#include "SSD1306.h" // alias for #include "SSD1306Wire.h" https://github.com/ThingPulse/esp8266-oled-ssd1306
SSD1306  display(0x3C, D1, D2); // SDA, SCL , GPIO5 and GPIO4
String display_temp;
String display_humid;

#define DHTPIN 10          // GPIO10 - SD3
//#define mwavepin D0 // microwave presence sensor GPIO16
#define red   D5 // pin for red LED collour - GPIO14
#define green D6 // pin for green LED collour - GPIO12
#define blue  D7 // pin for blue LED collour - GPIO13

BlynkTimer timer;
bool result;

int connectionattempts;
bool connection;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  //clean FS, for testing
  //SPIFFS.format();

  //WiFi.mode(WIFI_AP_STA);
  //read configuration from FS json
  Serial.println("mounting FS...");
  display.init();
  display.clear();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "*** Config Mode ***");
  display.drawString(64, 20, "AP IP : 192.168.4.1");
  display.drawString(64, 30, "SSID: " + String(AccessPointName));
  display.drawString(64, 40, "AP pw: " + String(password));
  display.display();
  
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(blynk_token, json["blynk_token"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
    display.init();
    display.clear();
    display.flipScreenVertically();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "*** Failed to mount FS ***");
    display.display();
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 33);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_blynk_token);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(AccessPointName, password)) {
    Serial.println("failed to connect and hit timeout");
    display.init();
    display.clear();
    display.flipScreenVertically();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "***failed to connect ***"); 
    display.drawString(64, 10, "***and hit timeout ***");
    display.display();
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
    display.init();
    display.clear();
    display.flipScreenVertically();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "Connected to Wifi!"); 
    display.drawString(64, 10, "Saving Config");
    //display.display();

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(blynk_token, custom_blynk_token.getValue());
  


  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["blynk_token"] = blynk_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  display.drawString(64, 10, "Connecting to Blynk..");
  display.display();
  Blynk.config(blynk_token, mqtt_server, atoi(mqtt_port));
  //Blynk.config(blynk_token);
  Blynk.connect();

  
  dht.setup(DHTPIN, DHTesp::DHT22); // Connect DHT sensor to GPIO 10
  delay(dht.getMinimumSamplingPeriod());
  h = dht.getHumidity();
  t = dht.getTemperature();
  if (isnan(h) || isnan(t)) {
   Serial.println("Failed to read from DHT sensor!");
    return;
  }
  yield();
  display.init();
  display.clear();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.display();
  yield(); 
  // Setup a function to be called every second
  timer.setInterval(5000L, sendTemp);
//  timer.setInterval(1000L, sendPresence);
  timer.setInterval(60000L, chkWifiSignal);
  timer.setInterval(30000L, connectionstatus);
  
  //pinMode(mwavepin, INPUT);
  pinMode(red, OUTPUT);    // digital output for the RED colour of a RGB LED strip
  pinMode(green, OUTPUT); // digital output for the GREEN colour of a RGB LED strip
  pinMode(blue, OUTPUT);  // digital output for the BLUE colour of a RGB LED strip
  pinMode (DHTPIN, INPUT); // digital input for the DHT22 sensor
  yield();
}

void loop() {
  // put your main code here, to run repeatedly:

  Blynk.run();
  timer.run();
  result = Blynk.connected();
}



void displayData() {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);

    if (result == 0){
      display.drawString(64, 0, "*** Disconnected!! ***");   
      
    }
    else {
      display.drawString(64, 0, "Bedroom Temp/Humidty");   
    }
    display.setFont(ArialMT_Plain_24);
    display.drawString(66, 13, display_temp + "°C");
    display.drawString(66, 40, display_humid + "%");
    display.display();

}

void sendTemp()
{
  delay(dht.getMinimumSamplingPeriod());
  h = dht.getHumidity();
  t = dht.getTemperature(); // or dht.readTemperature(true) for Fahrenheit

  display_temp = t;
  display_humid = h;
 // Serial.println(display_temp);
 // Serial.println(display_humid);
  
  // You can send any value at any time.
  // Please don't send more that 10 values per second.
  Blynk.virtualWrite(V1, h);
  Blynk.virtualWrite(V2, t);
  displayData();  
}

void chkWifiSignal()
{
  int WifiSignal = WiFi.RSSI() ;
  int signallStrength = (WifiSignal / 100)* 100;
  Blynk.virtualWrite(V3, signallStrength);
}

void connectionstatus()
{
  connection = Blynk.connected();
  if (connection == 0)
  {
      connectionattempts ++;
      Serial.println();
      Serial.print("connectionattempts");
      Serial.print(connectionattempts);
      Serial.println();
      display.init();
      display.clear();
      display.flipScreenVertically();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_10);
      display.drawString(64, 0, " CONNECTING ...");   
      display.display();
      yield();
  }
  else 
  {
    connectionattempts = 0;
  }
  
//  if (connectionattempts == 5)
//  {
//      ESP.restart();  
//  }

    if (connectionattempts == 5) {
      //WiFi.persistent(false);      
      WiFi.disconnect();          
      //WiFi.persistent(true);
      WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
      WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
      WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 33);
    
      //WiFiManager
      //Local intialization. Once its business is done, there is no need to keep it around
      WiFiManager wifiManager;
      //wifiManager.setBreakAfterConfig(true);
      //set config save notify callback
      //wifiManager.setSaveConfigCallback(saveConfigCallback);
    
      //set static ip
      //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
      
      //add all your parameters here
      wifiManager.addParameter(&custom_mqtt_server);
      wifiManager.addParameter(&custom_mqtt_port);
      wifiManager.addParameter(&custom_blynk_token);
      wifiManager.startConfigPortal(AccessPointName, password);
      delay(3000);
        //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(blynk_token, custom_blynk_token.getValue());
  


  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["blynk_token"] = blynk_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

      //reset and try again, or maybe put it to deep sleep
      ESP.reset();  
      delay(5000);

    //reset settings - for testing
    //wifiManager.resetSettings();

    //sets timeout until configuration portal gets turned off
    //useful to make it all retry or go to sleep
    //in seconds
    //wifiManager.setTimeout(120);

    //it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration

    //WITHOUT THIS THE AP DOES NOT SEEM TO WORK PROPERLY WITH SDK 1.5 , update to at least 1.5.1
    //WiFi.mode(WIFI_STA);
    

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  }
}


