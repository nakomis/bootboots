#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include "MessageQueue.h"

const char *MQTT_SERVER = "10.0.0.177";
const String TOPIC = "images";

WiFiClient espClient;
MqttClient client(espClient);

MessageQueue::MessageQueue() {}

int MessageQueue::postImage(const String& filename, const uint8_t* message, size_t size) {
    if (!client.connected())
    {
        Serial.println("Connecting to MQTT server...");
        if (!client.connect(MQTT_SERVER, 1883))
        {
            Serial.println("Failed to connect to MQTT server");
            return -1;
        }
        Serial.println("Connected to MQTT server");
    }

    if (client.connected())
    {
        Serial.println("Posting image to topic: " + String(TOPIC));
        Serial.printf("Filename: %s, Size: %zu bytes\n", filename.c_str(), size);
        uint8_t filenameArray [20];
        
        Serial.println("Preparing message with filename: " + filename);
        snprintf(reinterpret_cast<char*>(filenameArray), 20, "%s", filename.c_str());
        Serial.printf("Message size: %zu bytes\n", size);
        Serial.printf("Total message size: %zu bytes\n", size + 20);
        Serial.println("Beginning MQTT message...");
        Serial.printf("Filename array size: %zu bytes\n", sizeof(filenameArray));
        const char *topic = TOPIC.c_str();
        client.beginMessage(topic, sizeof(filenameArray) + size, false);
        Serial.printf("Writing message of size %zu bytes...\n", size + 20);
        Serial.printf("Filename array size: %zu bytes\n", sizeof(filenameArray));
        for (int i = 0; i < sizeof(filenameArray); i++) {
            Serial.printf("filenameArray[%d]: %d\n", i, filenameArray[i]);
        }
        Serial.println("Writing filename array to MQTT message...");
        size_t messageSize = 20;
        const uint8_t *buf = filenameArray;
        size_t bytesSent = client.write(buf, messageSize);
        Serial.printf("Bytes sent for filename array: %zu\n", bytesSent);
        bytesSent = client.write(message, size);
        Serial.printf("Bytes sent for image message: %zu\n", bytesSent);
        Serial.println("Ending MQTT message...");
        int result = client.endMessage();
        Serial.printf("Message posted with result: %d\n", result);
        return result;
    }
    else
    {
        Serial.println("Client not connected, cannot post message.");
        return -1;
    }
}
