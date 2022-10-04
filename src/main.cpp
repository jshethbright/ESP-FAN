#include "Arduino.h"
#include "ELECHOUSE_CC1101_SRC_DRV.h"
#include "RCSwitch.h"
#include "PubSubClient.h"
#include "ESP8266WiFi.h"
#include "Dictionary.h"
#include "Secrets.h"

//int RX_PIN; // int for Receive RX_PIN.
int RX_PIN;
int TX_PIN;

int PROTOCOL = 11;
int PULSE_LENGTH = 400;

int button = 0;

float FREQUENCY = 304.0;
int RF_REPEAT = 3;

const char* SSID = SECRET_SSID;
const char* PASSWORD = SECRET_PASSWORD;
const char* MQTT_BROKER = SECRET_MQTT_BROKER_IP;

const char* MQTT_USERNAME = SECRET_MQTT_USERNAME;
const char* MQTT_PASSWORD = SECRET_MQTT_PASSWORD;

String FAN_CODE = "1100";
String PREFIX_BITS = "111111000110";
String SPECIAL_COLOR_CONTROL_BITS = "0110";
String SPECIAL_OTHER_CONTROL_BITS = "0111";
String ITEM_FAN_CONTROL_BITS = "01";
String ITEM_LIGHT_CONTROL_BITS = "00";
String ITEM_OTHER_CONTROL_BITS = "11";

String LIGHT_ON_BITS = "10";
String LIGHT_OFF_BITS = "01";

String CHANGE_COLOR_BITS = "01";

String FAN_OFF_BITS = "11";
String FAN_LOW_BITS = "10";
String FAN_MID_BITS = "01";
String FAN_HIGH_BITS = "00";

Dictionary *FAN_SPEED_DICT = new Dictionary();
Dictionary *LIGHT_STATE_DICT = new Dictionary();

long prevMessage;


int FAN_ID_START = 12;
int FAN_ID_END = 16;

int ITEM_IND_START = 20;
int ITEM_IND_END = 22;

int STATE_IND_START = 22;
int STATE_IND_END = 24;


WiFiClient espClient;
PubSubClient client(espClient);
RCSwitch mySwitch = RCSwitch();

void setup_wifi() 
{

    delay(10);

    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    randomSeed(micros());

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}



void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Create a client ID
        String clientId = "ESP-FAN-01";
        // Attempt to connect
        if (client.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD))
        {
            Serial.println("connected");
            client.subscribe("fan/#");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}



static char *dec2binWzerofill(unsigned long Dec, unsigned int bitLength)
{
    static char bin[64];
    unsigned int i = 0;

    while (Dec > 0)
    {
        bin[32 + i++] = ((Dec & 1) > 0) ? '1' : '0';
        Dec = Dec >> 1;
    }

    for (unsigned int j = 0; j < bitLength; j++)
    {
        if (j >= bitLength - i)
        {
            bin[j] = bin[31 + i - (j - (bitLength - i))];
        }
        else
        {
            bin[j] = '0';
        }
    }
    bin[bitLength] = '\0';

    return bin;
}

const char* constructSendBits(String prefixBits, String fanCodeBits, String specialControlBits, String itemBits, String stateBits) {
    String out = prefixBits;
    
    out = out + fanCodeBits;
    out = out + specialControlBits;
    out = out + itemBits;
    out = out + stateBits;
    
    return out.c_str();
}

String getFanID(const char* bits, int fanIndStart, int fanIndEnd) {
    String stringBits = String(bits);
    return stringBits.substring(fanIndStart, fanIndEnd);
}

String getSpeedInfo(const char* bits, int stateIndStart, int stateIndEnd) {
    String stringBits = String(bits);

    String info = stringBits.substring(stateIndStart, stateIndEnd);
    return FAN_SPEED_DICT->search(info);
}

String getItemInfo(const char* bits, int itemIndStart, int itemIndEnd) {
    String stringBits = String(bits);
    return stringBits.substring(itemIndStart, itemIndEnd);
}

String getLightInfo(const char* bits, int stateIndStart, int stateIndEnd) {
    String stringBits = String(bits);

    String info = stringBits.substring(stateIndStart, stateIndEnd);
    return LIGHT_STATE_DICT->search(info);
}



struct infoStruct {
    String fanId;
    String endTopic;
    String state;
};

infoStruct deconReceiveBits(const char* bits) {
    String endTopic;
    String state;
    String fanId = getFanID(bits, FAN_ID_START, FAN_ID_END);
    String itemBits = getItemInfo(bits, ITEM_IND_START, ITEM_IND_END);
    if (itemBits.equals(String(ITEM_LIGHT_CONTROL_BITS))) {
        endTopic = "light";
        state = getLightInfo(bits, STATE_IND_START, STATE_IND_END);
    } else if (itemBits.equals(String(ITEM_FAN_CONTROL_BITS))) {
        endTopic = "speed";
        state = getSpeedInfo(bits, STATE_IND_START, STATE_IND_END);
    }

    infoStruct out;
    out.fanId = fanId;
    out.endTopic = endTopic;
    out.state = state;
    return out;
}

void doPublishState(infoStruct msgInfo) {
    String currTopic = "fan/" + msgInfo.fanId + "/" + msgInfo.endTopic;

    currTopic.replace("set", "");
    currTopic.toLowerCase();
    const char* publishTopic = currTopic.c_str();
    client.publish(publishTopic, msgInfo.state.c_str(), true);
}


void receiveBits() {
    
    if (mySwitch.available()) {
        long value =  mySwitch.getReceivedValue();        // save received Value
        if (value != prevMessage) {
            int prot = mySwitch.getReceivedProtocol();     // save received Protocol
            int bitlen = mySwitch.getReceivedBitlength();     // save received Bitlength

            char* bits = dec2binWzerofill(value, bitlen);
            Serial.println(bits);
            infoStruct msgInfo = deconReceiveBits(bits);
            doPublishState(msgInfo);

            prevMessage = value;
        }
        mySwitch.resetAvailable();
    }
}




void transmitBits(const char* sendBits) {
    ELECHOUSE_cc1101.SetTx();           // set Transmit on

    mySwitch.enableTransmit(TX_PIN);
    mySwitch.setRepeatTransmit(RF_REPEAT);
    mySwitch.setProtocol(PROTOCOL);
    mySwitch.setPulseLength(PULSE_LENGTH);

    mySwitch.send(sendBits);      // send 24 bit code
    
    mySwitch.disableTransmit();   
    ELECHOUSE_cc1101.setSidle();
    delay(100);
    ELECHOUSE_cc1101.SetRx(FREQUENCY);
    mySwitch.enableReceive(RX_PIN);
}

void setFanState(String fanCode, int speed) {
    String prefixBits = PREFIX_BITS;
    String fanCodeBits = fanCode;
    String specialControlBits = SPECIAL_OTHER_CONTROL_BITS;
    String itemBits = ITEM_FAN_CONTROL_BITS;
    String stateBits = FAN_OFF_BITS;

    stateBits = FAN_SPEED_DICT->key(speed);
    

    const char* message = constructSendBits(prefixBits, fanCodeBits, specialControlBits, itemBits, stateBits);
    transmitBits(message);
}

void setLightState(String fanCode, int state) {
    String prefixBits = PREFIX_BITS;
    String fanCodeBits = fanCode;
    String specialControlBits = SPECIAL_OTHER_CONTROL_BITS;
    String itemBits = ITEM_LIGHT_CONTROL_BITS;
    String stateBits = LIGHT_OFF_BITS;
    if (state == 1) {
        stateBits = LIGHT_ON_BITS;
    } else {
        stateBits = LIGHT_OFF_BITS;
    }
    const char* message = constructSendBits(prefixBits, fanCodeBits, specialControlBits, itemBits, stateBits);
    transmitBits(message);
}

void setRotateColorState(String fanCode) {
    String prefixBits = PREFIX_BITS;
    String fanCodeBits = FAN_CODE;
    String specialControlBits = SPECIAL_COLOR_CONTROL_BITS;
    String itemBits = ITEM_OTHER_CONTROL_BITS;
    String stateBits = CHANGE_COLOR_BITS;

    const char* message = constructSendBits(prefixBits, fanCodeBits, specialControlBits, itemBits, stateBits);
    transmitBits(message);
}

infoStruct getSendMessageInfo(char *topic, byte *payload) {
    String topicStr = String(topic);

    String state = String((char)payload[0]);

    int fanIdStart = topicStr.indexOf("/") + 1;
    int fanIdEnd = topicStr.lastIndexOf("/");

    String fanId = topicStr.substring(fanIdStart, fanIdEnd);

    String endTopic = topicStr.substring(fanIdEnd + 1);

    infoStruct out;
    out.fanId = fanId;
    out.endTopic = endTopic;
    out.state = state;

    return out;
}

void executeSend(infoStruct sendMsgInfo) {
    if (sendMsgInfo.endTopic.equals("setLight")) {
        setLightState(sendMsgInfo.fanId, sendMsgInfo.state.toInt());
        doPublishState(sendMsgInfo);
    } else if (sendMsgInfo.endTopic.equals("setSpeed")) {
        setFanState(sendMsgInfo.fanId, sendMsgInfo.state.toInt());
        doPublishState(sendMsgInfo);
    } else if (sendMsgInfo.endTopic.equals("setColor")) {
        setRotateColorState(sendMsgInfo.fanId);
        doPublishState(sendMsgInfo);
    }
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (unsigned int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    infoStruct sendMsgInfo = getSendMessageInfo(topic, payload);
    executeSend(sendMsgInfo);
    
}

void setup() {
    #ifdef ESP32
        RX_PIN = 4;  // for esp32! Receiver on GPIO RX_PIN 4.
        TX_PIN = 2;
    #elif ESP8266
        RX_PIN = 4;  // for esp8266! Receiver on RX_PIN 4 = D2.
        TX_PIN = 5;
    #else
        RX_PIN = 0;  // for Arduino! Receiver on interrupt 0 => that is RX_PIN #2
        TX_PIN = 6;
    #endif

    FAN_SPEED_DICT->insert(FAN_OFF_BITS, "0");
    FAN_SPEED_DICT->insert(FAN_LOW_BITS, "1");
    FAN_SPEED_DICT->insert(FAN_MID_BITS, "2");
    FAN_SPEED_DICT->insert(FAN_HIGH_BITS, "3");

    LIGHT_STATE_DICT->insert(LIGHT_OFF_BITS, "0");
    LIGHT_STATE_DICT->insert(LIGHT_ON_BITS, "1");

    pinMode(button, INPUT_PULLUP);
    Serial.begin(9600);
    setup_wifi();
    client.setServer(MQTT_BROKER, 1883);
    client.setCallback(callback);


    if (ELECHOUSE_cc1101.getCC1101()){       // Check the CC1101 Spi connection.
        Serial.println("Connection OK");
    }else{
        Serial.println("Connection Error");
    }

    ELECHOUSE_cc1101.Init();            // must be set to initialize the cc1101!
    ELECHOUSE_cc1101.setMHZ(FREQUENCY); // Here you can set your basic frequency. The lib calculates the frequency automatically (default = 433.92).The cc1101 can: 300-348 MHZ, 387-464MHZ and 779-928MHZ. Read More info from datasheet.

    mySwitch.enableReceive(RX_PIN);  // Receiver on interrupt 0 => that is RX_PIN #2

    ELECHOUSE_cc1101.SetRx();  // set Receive on
}

void loop()
{
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    receiveBits();
}