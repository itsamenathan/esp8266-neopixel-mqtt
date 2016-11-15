#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

#define PIN D2

//WiFiManager
//Local intialization. Once its business is done, there is no need to keep it around
WiFiManager wifiManager;

//Mqtt connection
WiFiClient wclient;
PubSubClient client(wclient);

//NeoPixel
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, PIN, NEO_GRB + NEO_KHZ800);

String scheduledMessages[8][2];
int numScheduledMessages = 0;


//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6];
const char *mqttTopic = "frcv/test/light/status";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting light...");

  // User button on nodemcu
  pinMode(D0, INPUT);

  //read configuration from FS json
  Serial.println("mounting FS...");

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

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  char temp_server[] = "broker.hivemq.com";
  char temp_port[] = "1883";
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", temp_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", temp_port, 6);
  
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  //WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("Goomba")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected from setup");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());


  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;

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

  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(callback);
  scheduleMessage("/device", "{\"status\":\"booted\"}");
  strip.begin();
  strip.setPixelColor(0, strip.Color(0, 0, 0));
}

void loop() {
  // is configuration portal requested?
  //if ( digitalRead(D0) == LOW ) {
  //  wifiManager.startConfigPortal("Goomba");
  //  Serial.println("connected from loop");
  //}

  strip.show(); // Initialize all pixels to 'off' 

  connectToMQTT();

  sendMessages();
  // Wait 1 second before continuing
  yield();
  client.loop();

}

void connectToMQTT() {
  if (!client.connected()) {
      if (client.connect("gymLightClient4")) {
        Serial.println("MQTT connected.");
        client.subscribe("frcv/test/light");
      } else {
        Serial.print("MQTT failed to connect: error code ");
        Serial.println(client.state());
      }
  }
}

byte getVal(char c)
{
   if(c >= '0' && c <= '9')
     return (byte)(c - '0');
   else
     return (byte)(c-'A'+10);
}


void callback(char* topic, byte* payload, unsigned int length) {
    const char* color = (const char*)payload;
    String colorstr = String(color);
    String red = colorstr.substring(1,3);
    String green = colorstr.substring(3,5);
    String blue = colorstr.substring(5,7);
    Serial.print(red);
    Serial.print(green);
    Serial.println(blue);

    byte red_b = 16*getVal(color[1]) + getVal(color[2]);
    byte green_b = 16*getVal(color[3]) + getVal(color[4]);
    byte blue_b = 16*getVal(color[5]) + getVal(color[6]);
    Serial.print(red_b);
    Serial.print(green_b);
    Serial.println(blue_b);

    strip.setPixelColor(0, strip.Color(red_b, green_b, blue_b));

    strip.show();
}

void scheduleMessage(String subTopic, String message) {
  if (numScheduledMessages <= 8) {
    ++numScheduledMessages;
    scheduledMessages[numScheduledMessages-1][0] = message;
    scheduledMessages[numScheduledMessages-1][1] = subTopic;
  }
}

void sendMessages() {
  for (int i=1; i<=numScheduledMessages; ++i) {
    connectToMQTT();
    String message = mqttTopic+scheduledMessages[i-1][1];
    String topic = scheduledMessages[i-1][0].c_str();
    client.publish(message.c_str(), topic.c_str());
  }
  numScheduledMessages = 0;
}

