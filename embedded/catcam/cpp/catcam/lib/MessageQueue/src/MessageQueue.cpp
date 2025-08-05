#include <WiFi.h>
#include <ArduinoMqttClient.h>
#include "MessageQueue.h"

const char *MQTT_SERVER = "10.0.0.177";
const String TOPIC = "images";

WiFiClient espClient;
MqttClient mqttClient(espClient);

MessageQueue::MessageQueue() {}

int MessageQueue::postImage(NamedImage* namedImage) {
    if (!mqttClient.connected())
    {
        Serial.println("Connecting to MQTT server...");
        if (!mqttClient.connect(MQTT_SERVER, 1883))
        {
            Serial.println("Failed to connect to MQTT server");
            return -1;
        }
        Serial.println("Connected to MQTT server");
    }

    if (mqttClient.connected())
    {
        Serial.println("Posting image to topic: " + String(TOPIC));
        Serial.printf("Filename: %s, Size: %zu bytes\n", namedImage->filename.c_str(), namedImage->size);
        uint8_t filenameArray [20];
        
        Serial.println("Preparing message with filename: " + namedImage->filename);
        snprintf(reinterpret_cast<char*>(filenameArray), 20, "%s", namedImage->filename.c_str());
        Serial.printf("Message size: %zu bytes\n", namedImage->size);
        Serial.printf("Total message size: %zu bytes\n", namedImage->size + 20);
        Serial.println("Beginning MQTT message...");
        Serial.printf("Filename array size: %zu bytes\n", sizeof(filenameArray));
        const char *topic = TOPIC.c_str();
        mqttClient.beginMessage(topic, sizeof(filenameArray) + namedImage->size, false);
        Serial.printf("Writing message of size %zu bytes...\n", namedImage->size + 20);
        Serial.printf("Filename array size: %zu bytes\n", sizeof(filenameArray));
        for (int i = 0; i < sizeof(filenameArray); i++) {
            Serial.printf("filenameArray[%d]: %d\n", i, filenameArray[i]);
        }
        Serial.println("Writing filename array to MQTT message...");
        size_t messageSize = 20;
        const uint8_t *buf = filenameArray;
        size_t bytesSent = mqttClient.write(buf, messageSize);
        Serial.printf("Bytes sent for filename array: %zu\n", bytesSent);
        bytesSent = mqttClient.write(namedImage->image, namedImage->size);
        Serial.printf("Bytes sent for image message: %zu\n", bytesSent);
        Serial.println("Ending MQTT message...");
        int result = mqttClient.endMessage();
        Serial.printf("Message posted with result: %d\n", result);
        mqttClient.stop(); // Ensure the client is stopped after posting
        return result;
    }
    else
    {
        Serial.println("Client not connected, cannot post message.");
        return -1;
    }
}
