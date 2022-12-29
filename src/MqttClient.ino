/**************************************************************
 *
 * For this example, you need to install PubSubClient library:
 *   https://github.com/knolleary/pubsubclient
 *   or from http://librarymanager/all#PubSubClient
 *
 * TinyGSM Getting Started guide:
 *   https://tiny.cc/tinygsm-readme
 *
 * For more MQTT examples, see PubSubClient library
 *
 **************************************************************
 * Use Mosquitto client tools to work with MQTT
 *   Ubuntu/Linux: sudo apt-get install mosquitto-clients
 *   Windows:      https://mosquitto.org/download/
 *
 * Subscribe for messages:
 *   mosquitto_sub -h test.mosquitto.org -t GsmClientTest/init -t GsmClientTest/ledStatus -q 1
 * Toggle led:
 *   mosquitto_pub -h test.mosquitto.org -t GsmClientTest/led -q 1 -m "toggle"
 *
 * You can use Node-RED for wiring together MQTT-enabled devices
 *   https://nodered.org/
 * Also, take a look at these additional Node-RED modules:
 *   node-red-contrib-blynk-ws
 *   node-red-dashboard
 *
 **************************************************************/

// Please select the corresponding model

//  #define SIM800L_IP5306_VERSION_20190610
// #define SIM800L_AXP192_VERSION_20200327
// #define SIM800C_AXP192_VERSION_20200609
#define SIM800L_IP5306_VERSION_20200811

#include "utilities.h"

// Select your modem:
#define TINY_GSM_MODEM_SIM800

// Set serial for debug console (to the Serial Monitor, default speed 115200)
#define SerialMon Serial

// Set serial for AT commands (to the module)
// Use Hardware Serial on Mega, Leonardo, Micro
#define SerialAT Serial1

// See all AT commands, if wanted
//#define DUMP_AT_COMMANDS

// Define the serial console for debug prints, if needed
#define TINY_GSM_DEBUG SerialMon

// Add a reception delay - may be needed for a fast processor at a slow baud rate
// #define TINY_GSM_YIELD() { delay(2); }

// Define how you're planning to connect to the internet
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

// set GSM PIN, if any
#define GSM_PIN ""

// Your GPRS credentials, if any
const char apn[] = "internet.cxn";
const char gprsUser[] = "";
const char gprsPass[] = "";

// MQTT details
const char *broker = "mqtt.dynamicdevices.co.uk";

const char *topicPrefix = "GsmClientTest";

const char *topicFragmentLed = "led";
const char *topicFragmentInit = "init";
const char *topicFragmentTele = "tele";
const char *topicFragmentLedStatus = "ledStatus";

const char *payloadTele = "This is a payload of 32 bytes...";
const char *payloadInit = "GsmClientTest started";

String _imei = "unknown";

#include <TinyGsmClient.h>
#include <PubSubClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif
TinyGsmClient client(modem);
PubSubClient mqtt(client);

// How often do we send MQTT KeepAlive messages?
// By default this is 15s with PubSubClient but this is quite
// often for a cellular connection so reduce it to 120s

//#define MQTT_KEEPALIVE_SECS 120

// If we want to test out regular publishing of an MQTT
// payload then define this and we will publish to
// `topicTele` with payload `payloadTele`

//#define MQTT_PUBLISH_INTERVAL_SECS 10*60

// Log metrics for how many bytes PubSubClient has sent and
// received to/from the underlying stream layer periodically
#define MQTT_LOG_METRICS_SECS 60

int ledStatus = LOW;

uint32_t lastReconnectAttempt = 0;
uint32_t lastMetricsOutput = 0;
#ifdef MQTT_PUBLISH_INTERVAL_SECS
uint32_t lastTelePublish = 0;
#endif

String getFullTopic(const char *topixPrefix, const char *topicFragment)
{
    String fullTopic = String(topicPrefix); 
    fullTopic += "/";
    fullTopic += _imei;
    fullTopic += "/";
    fullTopic += topicFragment;
    return fullTopic;
}

void mqttCallback(char *topic, byte *payload, unsigned int len)
{
    SerialMon.print("Message arrived [");
    SerialMon.print(topic);
    SerialMon.print("]: ");
    SerialMon.write(payload, len);
    SerialMon.println();

    // Only proceed if incoming message's topic matches
    if (String(topic) == getFullTopic(topicPrefix, topicFragmentLed)) {
        ledStatus = !ledStatus;
        digitalWrite(LED_GPIO, ledStatus);
        mqtt.publish(getFullTopic(topicPrefix, topicFragmentLedStatus).c_str(), ledStatus ? "1" : "0");
    }
}

boolean mqttConnect()
{
    SerialMon.print("Connecting to ");
    SerialMon.print(broker);

    // Connect to MQTT Broker
    boolean status = mqtt.connect( String("GsmClientTest" + _imei).c_str());

    // Or, if you want to authenticate MQTT:
    //boolean status = mqtt.connect("GsmClientName", "mqtt_user", "mqtt_pass");

    if (status == false) {
        SerialMon.println(" fail");
        return false;
    }
    SerialMon.println(" success");
    SerialMon.println((String)"Publishing: " + getFullTopic(topicPrefix, topicFragmentInit).c_str() + ": " + payloadInit);
    mqtt.publish(getFullTopic(topicPrefix, topicFragmentInit).c_str(), payloadInit);
    mqtt.subscribe(getFullTopic(topicPrefix, topicFragmentLed).c_str());
    return mqtt.connected();
}

void setup()
{
    // Set console baud rate
    SerialMon.begin(115200);

    delay(10);

    setupModem();

    SerialMon.println("Wait...");

    // Set GSM module baud rate and UART pins
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

    delay(6000);

    // Restart takes quite some time
    // To skip it, call init() instead of restart()
    SerialMon.println("Initializing modem...");
    modem.restart();
    // modem.init();

    String modemInfo = modem.getModemInfo();
    SerialMon.print("Modem Info: ");
    SerialMon.println(modemInfo);

#if TINY_GSM_USE_GPRS
    // Unlock your SIM card with a PIN if needed
    if ( GSM_PIN && modem.getSimStatus() != 3 ) {
        modem.simUnlock(GSM_PIN);
    }
#endif

    _imei = modem.getIMEI();
    SerialMon.println((String)"IMEI: " + _imei);

    SerialMon.print("Waiting for network...");
    if (!modem.waitForNetwork()) {
        SerialMon.println(" fail");
        delay(10000);
        return;
    }
    SerialMon.println(" success");

    if (modem.isNetworkConnected()) {
        SerialMon.println("Network connected");
    }

    // GPRS connection parameters are usually set after network registration
    SerialMon.print(F("Connecting to "));
    SerialMon.print(apn);
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
        SerialMon.println(" - failed");
        delay(10000);
        return;
    }
    SerialMon.println(" - success");

    if (modem.isGprsConnected()) {
        SerialMon.println("GPRS connected");
    }

    SerialMon.println("Local IP; " + modem.getLocalIP());

    // MQTT Broker setup
    mqtt.setServer(broker, 1883);
    mqtt.setCallback(mqttCallback);
#ifdef MQTT_KEEPALIVE_SECS
    // By default PubSubClient sends keepalives at 15s intervals
    SerialMon.println((String)"Set MQTT KeepAlive to " + MQTT_KEEPALIVE_SECS + " seconds");

    mqtt.setKeepAlive(MQTT_KEEPALIVE_SECS);
#endif
}

enum ConnectState
{
    UNKNOWN,
    ACQUIRE_NETWORK,
    ACQUIRE_DATA,
    ACQUIRE_MQTT,
    CONNECTED
};

ConnectState _currentState = UNKNOWN;

void loop()
{
    uint32_t t;

loopstart:

    if(_currentState == UNKNOWN || _currentState == ACQUIRE_NETWORK) {
        if(!modem.isNetworkConnected() ) {
            SerialMon.println("=== NETWORK NOT CONNECTED ===");
            delay(1000);
            _currentState = ACQUIRE_NETWORK;
            goto loopstart;
        }
    }

    if(_currentState == ACQUIRE_DATA) {
        if(!modem.isGprsConnected()) {
            SerialMon.println("=== GPRS NOT CONNECTED ===");
            delay(1000);
            _currentState = ACQUIRE_DATA;
            goto loopstart;
        }
    }

    // Currently we ALWAYS check each loop that MQTT is connected
    if(_currentState == ACQUIRE_MQTT || _currentState == CONNECTED) {
        if (!mqtt.connected()) {
            _currentState = ACQUIRE_MQTT;
            SerialMon.println("=== MQTT NOT CONNECTED ===");
            // Reconnect every 10 seconds
            t = millis();
            if (t - lastReconnectAttempt > 10000L) {
                lastReconnectAttempt = t;
                if (mqttConnect()) {
                    lastReconnectAttempt = 0;
                }
                else {
                    // Go back and check network
                    _currentState = UNKNOWN;
                }
            }
            delay(1000);
            goto loopstart;
        }
    }

    _currentState = CONNECTED;

#ifdef MQTT_PUBLISH_INTERVAL_SECS
    t = millis();
    if(t - lastTelePublish > MQTT_PUBLISH_INTERVAL_SECS * 1000) {
        lastTelePublish = t;
        SerialMon.println((String)"Publishing: " + getFullTopic(topicPrefix, topicFragmentTele).c_str() + ": " + payloadTele);
        mqtt.publish(getFullTopic(topicPrefix, topicFragmentTele).c_str(), payloadTele);
    }
#endif

    t = millis();
    if(t - lastMetricsOutput > MQTT_LOG_METRICS_SECS * 1000) {
        lastMetricsOutput = t;
        uint32_t txCount = mqtt.getTxCount();
        uint32_t rxCount = mqtt.getRxCount();
        SerialMon.println((String)"##### PubSubClient Tx Bytes: " + txCount + ", Rx Bytes: " + rxCount);
        Serial.println("[APP] Free memory: " + String(esp_get_free_heap_size()) + " bytes");
    }
    
    mqtt.loop();
}