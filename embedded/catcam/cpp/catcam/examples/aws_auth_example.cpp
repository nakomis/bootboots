#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"
#include "AWSAuth.h"
#include "SDLogger.h"

// AWS Configuration
const char* AWS_REGION = "us-east-1";
const char* AWS_ROLE_ARN = "arn:aws:iam::123456789012:role/ESP32IoTRole"; // Replace with your role ARN
const char* API_GATEWAY_HOST = "your-api-id.execute-api.us-east-1.amazonaws.com"; // Replace with your API Gateway host
const char* API_GATEWAY_ENDPOINT = "/prod/upload"; // Replace with your endpoint

AWSAuth awsAuth(AWS_REGION);

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize SDLogger first
    if (!SDLogger::getInstance().init()) {
        Serial.println("Failed to initialize SDLogger - using Serial only");
    }
    
    SDLogger::getInstance().infof("Starting AWS Authentication Example");
    
    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        SDLogger::getInstance().infof("Connecting to WiFi...");
    }
    SDLogger::getInstance().infof("WiFi connected!");
    
    // Initialize AWS Auth with certificates from secrets.h
    if (!awsAuth.initialize(AWS_CERT_CA, AWS_CERT_CRT, AWS_CERT_PRIVATE)) {
        SDLogger::getInstance().errorf("Failed to initialize AWS Auth");
        return;
    }
    
    // Get AWS credentials using IoT certificates
    SDLogger::getInstance().infof("Exchanging certificates for AWS credentials...");
    if (!awsAuth.getCredentials(AWS_ROLE_ARN, "ESP32CatCamSession")) {
        SDLogger::getInstance().errorf("Failed to get AWS credentials");
        return;
    }
    
    SDLogger::getInstance().infof("AWS credentials obtained successfully!");
    
    // Test API Gateway request with SigV4 signing
    testAPIGatewayRequest();
}

void loop() {
    // Refresh credentials if needed (they expire after 1 hour)
    if (!awsAuth.areCredentialsValid()) {
        SDLogger::getInstance().infof("Refreshing AWS credentials...");
        awsAuth.refreshCredentialsIfNeeded(AWS_ROLE_ARN, "ESP32CatCamSession");
    }
    
    // Your main application logic here
    delay(60000); // Check every minute
}

void testAPIGatewayRequest() {
    SDLogger::getInstance().infof("Testing API Gateway request with SigV4 signing...");
    
    // Create test payload
    String payload = "{\"message\":\"Hello from ESP32\",\"timestamp\":\"" + String(millis()) + "\"}";
    
    // Create SigV4 signed headers
    SigV4Headers sigv4Headers = awsAuth.createSigV4Headers("POST", API_GATEWAY_ENDPOINT, 
                                                           API_GATEWAY_HOST, payload, 
                                                           "application/json");
    
    if (!sigv4Headers.isValid) {
        SDLogger::getInstance().errorf("Failed to create SigV4 headers");
        return;
    }
    
    // Make the API request
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // For testing - in production, use proper CA certificates
    
    String url = "https://" + String(API_GATEWAY_HOST) + API_GATEWAY_ENDPOINT;
    http.begin(client, url);
    
    // Set required headers
    http.addHeader("Authorization", sigv4Headers.authorization);
    http.addHeader("X-Amz-Date", sigv4Headers.date);
    http.addHeader("X-Amz-Security-Token", awsAuth.getCurrentCredentials().sessionToken);
    http.addHeader("Content-Type", sigv4Headers.contentType);
    http.addHeader("Host", sigv4Headers.host);
    
    SDLogger::getInstance().infof("Making API request...");
    SDLogger::getInstance().infof("URL: %s", url.c_str());
    SDLogger::getInstance().infof("Authorization: %s...", sigv4Headers.authorization.substring(0, 50).c_str());
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        SDLogger::getInstance().infof("HTTP Response Code: %d", httpResponseCode);
        SDLogger::getInstance().infof("Response: %s", response.c_str());
    } else {
        SDLogger::getInstance().errorf("HTTP Request failed. Error: %d", httpResponseCode);
    }
    
    http.end();
}

// Example function to upload image data to API Gateway
void uploadImageToAPIGateway(const uint8_t* imageData, size_t imageSize, const String& filename) {
    SDLogger::getInstance().infof("Uploading image to API Gateway: %s", filename.c_str());
    
    // Ensure credentials are valid
    if (!awsAuth.refreshCredentialsIfNeeded(AWS_ROLE_ARN, "ESP32CatCamSession")) {
        SDLogger::getInstance().errorf("Failed to refresh AWS credentials");
        return;
    }
    
    // Create JSON payload with base64 encoded image
    DynamicJsonDocument doc(imageSize + 1000);
    doc["filename"] = filename;
    doc["timestamp"] = millis();
    doc["contentType"] = "image/jpeg";
    
    // Base64 encode image data (simplified - you might want to use a proper base64 encoder)
    String encodedImage = ""; // You'd implement proper base64 encoding here
    doc["imageData"] = encodedImage;
    
    String payload;
    serializeJson(doc, payload);
    
    // Create SigV4 signed headers
    SigV4Headers sigv4Headers = awsAuth.createSigV4Headers("POST", API_GATEWAY_ENDPOINT, 
                                                           API_GATEWAY_HOST, payload);
    
    if (!sigv4Headers.isValid) {
        SDLogger::getInstance().errorf("Failed to create SigV4 headers for image upload");
        return;
    }
    
    // Make the API request
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Configure proper certificates in production
    
    String url = "https://" + String(API_GATEWAY_HOST) + API_GATEWAY_ENDPOINT;
    http.begin(client, url);
    
    // Set headers
    http.addHeader("Authorization", sigv4Headers.authorization);
    http.addHeader("X-Amz-Date", sigv4Headers.date);
    http.addHeader("X-Amz-Security-Token", awsAuth.getCurrentCredentials().sessionToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Host", API_GATEWAY_HOST);
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode == 200) {
        SDLogger::getInstance().infof("Image uploaded successfully!");
        String response = http.getString();
        SDLogger::getInstance().infof("Response: %s", response.c_str());
    } else {
        SDLogger::getInstance().errorf("Image upload failed. HTTP Code: %d", httpResponseCode);
        SDLogger::getInstance().errorf("Response: %s", http.getString().c_str());
    }
    
    http.end();
}
