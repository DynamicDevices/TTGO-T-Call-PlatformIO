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

#define CORE_DEBUG_LEVEL 5

#ifdef TINY_GSM_MODEM_SIM800
#include "utilities.h"
#endif

// Should we advertise with Bluetooth using OpenHaystack (Apple iTag implementation) ?
#define SUPPORT_OPENHAYSTACK 

// Should we test the modem?
#define SUPPORT_MODEM

// TinyGSM modem now set in platform.ini
//#define TINY_GSM_MODEM_SIM800

// Set serial for debug console (to the Serial Monitor, default speed 115200)
#define SerialMon Serial

// Set serial for AT commands (to the module)
#define SerialAT Serial0

// See all AT commands, if wanted
//#define DUMP_AT_COMMANDS

// Define the serial console for debug prints, if needed
#define TINY_GSM_DEBUG SerialMon

// Add a reception delay - may be needed for a fast processor at a slow baud rate
// #define TINY_GSM_YIELD() { delay(2); }

// Define how you're planning to connect to the internet
#define TINY_GSM_USE_GPRS true
#define TINY_GSM_USE_WIFI false

// Q. I think the standard TCP timeout is 15 minutes so lets set TCP KeepAlive to 10 minutes?
//
// Override TCP Keepalive to 10 minutes (only in customised SIM800L module for now)
#define TINY_GSM_TCP_KEEPALIVE_SECS 600

// set GSM PIN, if any
#define GSM_PIN ""

// Your GPRS credentials, if any
const char apn[] = "internet.cxn";
const char gprsUser[] = "";
const char gprsPass[] = "";

// MQTT details
const char *broker = "mqtt.dynamicdevices.co.uk";

const char *topicPrefix = "GsmClientTest";

const char *topicFragmentCmnd = "cmnd";
const char *topicFragmentStatus = "status";
const char *topicFragmentLWTStatus = "lwtstatus";
const char *topicFragmentTele = "tele";

const char *payloadOnline = "ONLINE";
const char *payloadOffline = "OFFLINE";
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

// Define maximum KeepAlive which is circa 18 hours
// (This is because we will use TCP KeepAlives to keep the connection up and monitored)
#define MQTT_KEEPALIVE_SECS 65535

// Number of times we'll try to reconnect MQTT before dropping
// back and testing cellular
#define MQTT_MAX_CONNECTION_ATTEMPTS 3

// If we want to test out regular publishing of an MQTT
// payload then define this and we will publish to
// `topicTele` with payload `payloadTele`

#define MQTT_PUBLISH_INTERVAL_SECS 60*60

// Log metrics for how many bytes PubSubClient has sent and
// received to/from the underlying stream layer periodically
#define MQTT_LOG_METRICS_SECS 60

int ledStatus = LOW;

uint32_t _lastReconnectAttempt = 0;
uint32_t _lastMetricsOutput = 0;
uint32_t _lastCellularCall = 0;
uint32_t _networkWaitStartTimeMs = 0;
uint32_t _gprsWaitStartTimeMs = 0;

#ifdef MQTT_PUBLISH_INTERVAL_SECS
uint32_t _lastTelePublish = 0;
#endif

// Wait for network for up to 3 minutes before reinitialising modem
#define MAX_NETWORK_WAIT_SECS 180

// Wait for up to 1 minute for GPRS PDP to come up (or LTE)
#define MAX_GPRS_WAIT_SECS 60

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
    if (String(topic) == getFullTopic(topicPrefix, topicFragmentCmnd)) {
        // Do stuff here!
        if(String((char *)payload) == String("reset")) {
            // Then return the success / failure
            mqtt.publish(getFullTopic(topicPrefix, topicFragmentStatus).c_str(), "OK");

            // Delay a bit progressing MQTT state machine to try to get the response out
            int count = 0;
            while(count++ < 50) {
                mqtt.loop();
               delay(100);
            }

            ESP.restart();
        } else {
            // Then return the success / failure
            mqtt.publish(getFullTopic(topicPrefix, topicFragmentStatus).c_str(), "UNHANDLED");
        }
    }
}

boolean mqttConnect()
{
    SerialMon.print("Connecting to ");
    SerialMon.print(broker);

    // Connect to MQTT Broker with LWT
    boolean status = mqtt.connect( 
        String("MqttClient" + _imei).c_str(), // Client ID
        getFullTopic(topicPrefix, topicFragmentLWTStatus).c_str(), // Will Topic
        1, // Will QoS
        true, // Will Retain (subscribers will see this in future even if they don't currently have a subscription)
        payloadOffline // Will Message
    );

    // Or, if you want to authenticate MQTT:
    //boolean status = mqtt.connect("GsmClientName", "mqtt_user", "mqtt_pass");

    if (status == false) {
        SerialMon.println(" fail");
        return false;
    }
    SerialMon.println(" success");

    // Publish ONLINE status and make sure it's retained
    SerialMon.println((String)"Publishing: " + getFullTopic(topicPrefix, topicFragmentLWTStatus).c_str() + ": " + payloadOnline);
    mqtt.publish(getFullTopic(topicPrefix, topicFragmentLWTStatus).c_str(), payloadOnline, true);

    // Suscribe to commands
    mqtt.subscribe(getFullTopic(topicPrefix, topicFragmentCmnd).c_str());
    return mqtt.connected();
}

extern "C" void openhaystack_main(void);

void setup()
{
    // Set console baud rate
    SerialMon.begin(115200);
    
    delay(5000);

    log_i("Starting Up");

#ifdef SUPPORT_OPENHAYSTACK
    // Setup OpenHaystack
    openhaystack_main();
#endif
}

   
void loop()
{
#ifdef SUPPORT_MODEM
    // Call cellular handler every 1s
    uint32_t t = millis();
    if(t - _lastCellularCall > 1000) {
        _lastCellularCall = t;
        handleCellular();
    }
#endif

    // Fire out some metrics for debugging
    if(t - _lastMetricsOutput > MQTT_LOG_METRICS_SECS * 1000) {
        _lastMetricsOutput = t;
#ifdef SUPPORT_MODEM
        uint32_t txCount = mqtt.getTxCount();
        uint32_t rxCount = mqtt.getRxCount();
        SerialMon.println((String)"##### PubSubClient Tx Bytes: " + txCount + ", Rx Bytes: " + rxCount);
#endif
        Serial.println("[APP] Free memory: " + String(esp_get_free_heap_size()) + " bytes");
    }
}

enum ConnectState
{
    UNKNOWN,
    ACQUIRE_NETWORK,
    WAIT_FOR_NETWORK,
    ACQUIRE_GPRS,
    WAIT_FOR_GPRS,
    ACQUIRE_MQTT,
    CONNECTED
};

ConnectState _currentState = UNKNOWN;
uint8_t _connectionAttempt = 0;

// Called every 1s
void handleCellular()
{
    uint32_t t;

    if(_currentState == UNKNOWN || _currentState == ACQUIRE_NETWORK) {
        // Assume modem will retry to connect
        if(!modem.isNetworkConnected() ) {
            SerialMon.println("=== NETWORK NOT CONNECTED ===");

#ifdef TINY_GSM_MODEM_SIM800
            setupModem();
#endif

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

            _currentState = WAIT_FOR_NETWORK;
            _networkWaitStartTimeMs = millis();
        }        
        else {
            SerialMon.println("=== NETWORK CONNECTED ===");
            _currentState = ACQUIRE_GPRS;
        }
    }

    if(_currentState == WAIT_FOR_NETWORK) {
        if(!modem.isNetworkConnected() ) {
            uint32_t t = millis();
            if( (t-_networkWaitStartTimeMs) > MAX_NETWORK_WAIT_SECS*1000) {
                SerialMon.println("=== TIMEOUT WAITING FOR NETWORK. REINITIALISE MODEM ===");
                _currentState = UNKNOWN;
                return;
            }
            SerialMon.println((String)"=== WAITING FOR NETWORK " + (t-_networkWaitStartTimeMs)/1000 + " secs ===");
            return;

        }
        SerialMon.println("=== NETWORK CONNECTED ===");
        _currentState = ACQUIRE_GPRS;
    }

    if(_currentState == ACQUIRE_GPRS) {
        if(!modem.isGprsConnected()) {
            SerialMon.println("=== GPRS NOT CONNECTED ===");

            // GPRS connection parameters are usually set after network registration
            SerialMon.print(F("Connecting to "));
            SerialMon.print(apn);

            //
            // TODO: This call seems to block for a significant time. Might need to rewrite to make non-blocking?
            //
            if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
                SerialMon.println(" - failed");
                SerialMon.println("=== TIMEOUT WAITING FOR GPRS. REINITIALISE MODEM ===");
                _currentState = UNKNOWN;
                return;
            }
            SerialMon.println(" - success");
            _gprsWaitStartTimeMs = millis();
            _currentState = WAIT_FOR_GPRS;
        } else {
            SerialMon.println("=== GPRS CONNECTED ===");
            _currentState = ACQUIRE_MQTT;
        }
    }

    if(_currentState == WAIT_FOR_GPRS) {
        if (!modem.isGprsConnected()) {
            uint32_t t = millis();
            if( (t-_gprsWaitStartTimeMs) > MAX_GPRS_WAIT_SECS*1000) {
                SerialMon.println("=== TIMEOUT WAITING FOR GPRS. REINITIALISE MODEM ===");
                _currentState = UNKNOWN;
                return;
            }

            SerialMon.println("=== WAITING FOR GPRS CONNECT");
            return;
        }

        SerialMon.println("Local IP; " + modem.getLocalIP());

        _currentState = ACQUIRE_MQTT;
    }

    // Currently we ALWAYS check each loop that MQTT is connected
    if(_currentState == ACQUIRE_MQTT || _currentState == CONNECTED) {
        if (!mqtt.connected()) {
            _currentState = ACQUIRE_MQTT;
            SerialMon.println("=== MQTT NOT CONNECTED ===");

            // MQTT Broker setup
            mqtt.setServer(broker, 1883);
            mqtt.setCallback(mqttCallback);
#ifdef MQTT_KEEPALIVE_SECS
            // By default PubSubClient sends keepalives at 15s intervals
            SerialMon.println((String)"Set MQTT KeepAlive to " + MQTT_KEEPALIVE_SECS + " seconds");

            mqtt.setKeepAlive(MQTT_KEEPALIVE_SECS);
#endif

            // Reconnect every 10 seconds
            t = millis();
            if (t - _lastReconnectAttempt > 10000L) {
                _lastReconnectAttempt = t;
                if (mqttConnect()) {
                    _lastReconnectAttempt = 0;
                    _connectionAttempt = 0;
                    SerialMon.println("=== MQTT CONNECTED ===");
                    _currentState = CONNECTED;
                }
                else {
                    if(_connectionAttempt++ >= MQTT_MAX_CONNECTION_ATTEMPTS) {
                        _connectionAttempt = 0;
                        // Go back and check network
                        _currentState = UNKNOWN;
                    }
                    return;
                }
            }
        }
    }

#ifdef MQTT_PUBLISH_INTERVAL_SECS
    t = millis();
    if(t - _lastTelePublish > MQTT_PUBLISH_INTERVAL_SECS * 1000) {
        _lastTelePublish = t;
        SerialMon.println((String)"Publishing: " + getFullTopic(topicPrefix, topicFragmentTele).c_str() + ": " + payloadTele);
        mqtt.publish(getFullTopic(topicPrefix, topicFragmentTele).c_str(), payloadTele);
    }
#endif

    // Might need to run this more than every second?    
    mqtt.loop();
}