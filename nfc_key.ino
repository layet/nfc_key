/*
 Basic MQTT example

 This sketch demonstrates the basic capabilities of the library.
 It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic"
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary

 It will reconnect to the server if the connection is lost using a blocking
 reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
 achieve the same result without blocking the main loop.
 
*/

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <PubSubClient.h>
#include <ESP8266httpUpdate.h>

PN532_SPI pn532spi(SPI, 4);
PN532 nfc(pn532spi);
WiFiClient espClient;
PubSubClient client(espClient);
byte inState;

const char* ssid = "*";
const char* password = "*";
const char* mqtt_server = "i-alice.ru";
const char* mqtt_alive_topic = "/alexhome/garage/nfckey/alive";
const char* mqtt_subscribe_topic = "/alexhome/garage/nfckey/+";
const char* mqtt_key_topic = "/alexhome/garage/nfckey/key";
const char* mqtt_in_topic = "/alexhome/garage/nfckey/in";
const char* mqtt_led_topic = "/alexhome/garage/nfckey/led";
const char* update_url = "http://i-alice.ru/ota/index.php";
const char* update_class = "nfc_key-0.0.1";

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if (String(topic) == String(mqtt_led_topic)) if ((char)payload[0] == '1') { digitalWrite(16, HIGH); } else { digitalWrite(16, LOW); }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("NFCKey", mqtt_alive_topic, 0, 0, "0")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.subscribe(mqtt_subscribe_topic);
      client.publish(mqtt_alive_topic, "1");
      client.publish(mqtt_in_topic, String(inState).c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("STA-MAC: ");
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

void updateProc() {
  if (WiFi.status() == WL_CONNECTED) {
    t_httpUpdate_return ret = ESPhttpUpdate.update(update_url, update_class);
    switch(ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
            Serial.println("");
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[update] Update no Update.");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[update] Update ok."); // may not called we reboot the ESP
            break;
    }
  }
}

void setup()
{
  pinMode(5, INPUT_PULLUP);
  pinMode(16, OUTPUT);
  digitalWrite(16, HIGH);
  
  Serial.begin(9600);

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }
  
  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // Set the max number of retry attempts to read from a card
  // This prevents us from waiting forever for a card, which is
  // the default behaviour of the PN532.
  nfc.setPassiveActivationRetries(10);
  
  // configure board to read RFID tags
  nfc.SAMConfig();
    
  Serial.println("Waiting for an ISO14443A card");
  
  setup_wifi();
  updateProc();
  
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  inState = digitalRead(5);
}

void loop()
{
  boolean success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  char uid_str[15];
  
  if (!client.connected()) {
    reconnect();
  }
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);
  
  if (success) {
    Serial.println("Found a card!");
    Serial.print("UID Length: ");Serial.print(uidLength, DEC);Serial.println(" bytes");
    Serial.print("UID Value: ");
    for (uint8_t i=0; i < uidLength; i++) 
    {
      Serial.print(" 0x");Serial.print(uid[i], HEX); 
    }
    Serial.println("");
    
    while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength)) {}
    if (uidLength == 4) { 
      sprintf(uid_str, "%02x:%02x:%02x:%02x", uid[0], uid[1], uid[2], uid[3]);
      client.publish(mqtt_key_topic, uid_str, 11);
    }
    if (uidLength == 7) { 
      sprintf(uid_str, "%02x:%02x:%02x:%02x:%02x:%02x:%02x", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
      client.publish(mqtt_key_topic, uid_str, 20);
    }
  }

  if (digitalRead(5) != inState) {
    inState = digitalRead(5);
    client.publish(mqtt_in_topic, String(inState).c_str());
  }
  
  client.loop();
}
