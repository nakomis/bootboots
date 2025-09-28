#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"
#include "MessageQueue.h"
#include "AWSAuth.h"
#include "SDLogger.h"

// AWS Configuration
const char* AWS_REGION = "us-east-1";
const char* AWS_ROLE_ARN = "arn:aws:iam::123456789012:role/ESP32CatCamRole"; // Replace with your role
const char* API_GATEWAY_HOST = "your-api-id.execute-api.us-east-1.amazonaws.com"; // Replace
const char* API_GATEWAY_ENDPOINT = "/prod/images"; // Replace with your endpoint

// SNS Configuration
const char* SNS_TOPIC_ARN = "arn:aws:sns:us-east-1:123456789012:catcam-alerts"; // Replace with your SNS topic ARN

// Lambda Configuration  
const char* LAMBDA_FUNCTION_NAME = "catcam-image-processor"; // Replace with your Lambda function name

// Global instances
MessageQueue messageQueue;
AWSAuth awsAuth(AWS_REGION);

// Credentials refresh tracking
unsigned long lastCredentialCheck = 0;
const unsigned long CREDENTIAL_CHECK_INTERVAL = 300000; // Check every 5 minutes

// Function declarations
void connectToWiFi();
void checkAndRefreshCredentials();
int uploadImage(const String& filename, const uint8_t* imageData, size_t imageSize);
bool uploadToAPIGateway(const String& filename, const uint8_t* imageData, size_t imageSize);
bool uploadImageToPresignedUrl(const String& presignedUrl, const uint8_t* imageData, size_t imageSize);
void takeAndUploadPhoto();
bool sendSNSNotification(const String& message, const String& subject = "CatCam Alert");
bool invokeLambdaFunction(const String& functionPayload, String& lambdaResponse);
void handleCatDetection(const String& catName, float confidence, const String& imagePath);
void testAWSServices();

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize SDLogger first
    if (!SDLogger::getInstance().init()) {
        Serial.println("Failed to initialize SDLogger - using Serial only");
    }
    
    SDLogger::getInstance().infof("%s", BANNER);
    SDLogger::getInstance().infof("CatCam with AWS Authentication Starting...");
    
    // Connect to WiFi
    connectToWiFi();
    
    // Initialize AWS Authentication
    if (!awsAuth.initialize(AWS_CERT_CA, AWS_CERT_CRT, AWS_CERT_PRIVATE)) {
        SDLogger::getInstance().errorf("Failed to initialize AWS Auth - falling back to MQTT");
        return;
    }
    
    // Get initial AWS credentials
    SDLogger::getInstance().infof("Obtaining AWS credentials...");
    if (awsAuth.getCredentials(AWS_ROLE_ARN, "CatCamSession")) {
        SDLogger::getInstance().infof("AWS authentication successful!");
        SDLogger::getInstance().infof("Using API Gateway for image uploads");
        
        // Test AWS services integration
        testAWSServices();
    } else {
        SDLogger::getInstance().errorf("AWS authentication failed - falling back to MQTT");
    }
}

void loop() {
    // Check and refresh AWS credentials periodically
    if (millis() - lastCredentialCheck > CREDENTIAL_CHECK_INTERVAL) {
        checkAndRefreshCredentials();
        lastCredentialCheck = millis();
    }
    
    // Example: Simulate cat detection every 5 minutes for testing
    static unsigned long lastDetectionTest = 0;
    if (millis() - lastDetectionTest > 300000) { // 5 minutes
        // Simulate a cat detection
        handleCatDetection("Boots", 0.95, "/sd/images/boots_" + String(millis()) + ".jpg");
        lastDetectionTest = millis();
    }
    
    // Your main application logic here
    // For example, taking photos and uploading them
    
    delay(10000); // Main loop delay
}

void connectToWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    SDLogger::getInstance().infof("Connecting to WiFi");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(1000);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        SDLogger::getInstance().infof("WiFi connected!");
        SDLogger::getInstance().infof("IP address: %s", WiFi.localIP().toString().c_str());
    } else {
        SDLogger::getInstance().errorf("WiFi connection failed!");
    }
}

void checkAndRefreshCredentials() {
    SDLogger::getInstance().infof("Checking AWS credentials...");
    
    if (!awsAuth.areCredentialsValid()) {
        SDLogger::getInstance().infof("Credentials expired or invalid, refreshing...");
        
        if (awsAuth.refreshCredentialsIfNeeded(AWS_ROLE_ARN, "CatCamSession")) {
            SDLogger::getInstance().infof("Credentials refreshed successfully");
        } else {
            SDLogger::getInstance().errorf("Failed to refresh credentials");
        }
    } else {
        SDLogger::getInstance().infof("Credentials still valid");
    }
}

// Enhanced image upload function that tries API Gateway first, then falls back to MQTT
int uploadImage(const String& filename, const uint8_t* imageData, size_t imageSize) {
    SDLogger::getInstance().infof("Uploading image: %s (Size: %d bytes)", filename.c_str(), imageSize);
    
    // Try API Gateway upload first if we have valid AWS credentials
    if (awsAuth.areCredentialsValid()) {
        SDLogger::getInstance().infof("Attempting API Gateway upload...");
        
        if (uploadToAPIGateway(filename, imageData, imageSize)) {
            SDLogger::getInstance().infof("Successfully uploaded via API Gateway");
            return 0; // Success
        } else {
            SDLogger::getInstance().warnf("API Gateway upload failed, falling back to MQTT");
        }
    }
    
    // Fall back to MQTT if API Gateway is not available or failed
    SDLogger::getInstance().infof("Using MQTT fallback...");
    
    // For MQTT fallback, you would need to create a NamedImage object
    // This is just a placeholder - implement according to your NamedImage class
    SDLogger::getInstance().warnf("MQTT fallback not fully implemented in this example");
    return -1; // Indicate fallback needed
}

bool uploadToAPIGateway(const String& filename, const uint8_t* imageData, size_t imageSize) {
    // Create JSON payload with image metadata and data
    DynamicJsonDocument doc(imageSize + 2000);
    doc["filename"] = filename;
    doc["timestamp"] = millis();
    doc["size"] = imageSize;
    doc["contentType"] = "image/jpeg";
    doc["deviceId"] = WiFi.macAddress();
    
    // For large images, you might want to upload to S3 directly
    // and just send metadata to API Gateway. For this example,
    // we'll send metadata and request a presigned URL
    
    // Instead of full image, send metadata and request a presigned URL
    doc["imageData"] = ""; // Would contain base64 encoded image or presigned URL request
    
    String payload;
    serializeJson(doc, payload);
    
    // Create SigV4 signed headers
    SigV4Headers sigv4Headers = awsAuth.createSigV4Headers(
        "POST", 
        API_GATEWAY_ENDPOINT, 
        API_GATEWAY_HOST, 
        payload,
        "application/json"
    );
    
    if (!sigv4Headers.isValid) {
        SDLogger::getInstance().errorf("Failed to create SigV4 headers");
        return false;
    }
    
    // Make the authenticated API request
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // In production, use proper CA certificates
    
    String url = "https://" + String(API_GATEWAY_HOST) + API_GATEWAY_ENDPOINT;
    http.begin(client, url);
    
    // Set all required headers for AWS API Gateway
    http.addHeader("Authorization", sigv4Headers.authorization);
    http.addHeader("X-Amz-Date", sigv4Headers.date);
    http.addHeader("X-Amz-Security-Token", awsAuth.getCurrentCredentials().sessionToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Host", API_GATEWAY_HOST);
    http.addHeader("User-Agent", "ESP32-CatCam/1.0");
    
    SDLogger::getInstance().infof("Making authenticated API request...");
    SDLogger::getInstance().infof("URL: %s", url.c_str());
    
    int httpResponseCode = http.POST(payload);
    
    bool success = false;
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
        String response = http.getString();
        SDLogger::getInstance().infof("Upload successful! Response: %s", response.c_str());
        success = true;
        
        // Parse response for any additional instructions (like presigned URLs)
        DynamicJsonDocument responseDoc(1024);
        deserializeJson(responseDoc, response);
        
        if (responseDoc.containsKey("uploadUrl")) {
            // If API Gateway returned a presigned URL, use it to upload the actual image
            String uploadUrl = responseDoc["uploadUrl"];
            success = uploadImageToPresignedUrl(uploadUrl, imageData, imageSize);
        }
        
    } else {
        SDLogger::getInstance().errorf("Upload failed. HTTP Code: %d", httpResponseCode);
        String response = http.getString();
        SDLogger::getInstance().errorf("Error response: %s", response.c_str());
    }
    
    http.end();
    return success;
}

bool uploadImageToPresignedUrl(const String& presignedUrl, const uint8_t* imageData, size_t imageSize) {
    SDLogger::getInstance().infof("Uploading image to presigned URL...");
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Configure proper certificates in production
    
    http.begin(client, presignedUrl);
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("Content-Length", String(imageSize));
    
    // Convert image data to String for HTTP PUT
    String imageDataStr = "";
    for (size_t i = 0; i < imageSize; i++) {
        imageDataStr += (char)imageData[i];
    }
    
    int httpResponseCode = http.PUT(imageDataStr);
    
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
        SDLogger::getInstance().infof("Image uploaded successfully to S3!");
        http.end();
        return true;
    } else {
        SDLogger::getInstance().errorf("S3 upload failed. HTTP Code: %d", httpResponseCode);
        SDLogger::getInstance().errorf("Response: %s", http.getString().c_str());
        http.end();
        return false;
    }
}

// Example function to simulate taking and uploading a photo
void takeAndUploadPhoto() {
    SDLogger::getInstance().infof("Taking photo...");
    
    // Simulate image capture (replace with actual camera code)
    uint8_t dummyImage[] = {0xFF, 0xD8, 0xFF, 0xE0}; // JPEG header bytes
    size_t imageSize = sizeof(dummyImage);
    
    String filename = "catcam_" + String(millis()) + ".jpg";
    
    // Upload using our enhanced function that tries API Gateway first
    int result = uploadImage(filename, dummyImage, imageSize);
    
    if (result == 0) {
        SDLogger::getInstance().infof("Photo uploaded successfully!");
    } else {
        SDLogger::getInstance().errorf("Photo upload failed with code: %d", result);
    }
}

// SNS notification function
bool sendSNSNotification(const String& message, const String& subject) {
    SDLogger::getInstance().infof("Sending SNS notification: %s", subject.c_str());
    
    // Ensure credentials are valid
    if (!awsAuth.refreshCredentialsIfNeeded(AWS_ROLE_ARN, "CatCamSession")) {
        SDLogger::getInstance().errorf("Failed to refresh AWS credentials for SNS");
        return false;
    }
    
    // Create SNS endpoint URL
    String snsHost = "sns." + String(AWS_REGION) + ".amazonaws.com";
    String snsEndpoint = "/";
    
    // Create SNS request payload (form-encoded)
    String payload = "Action=Publish";
    payload += "&Version=2010-03-31";
    payload += "&TopicArn=" + awsAuth.urlEncode(SNS_TOPIC_ARN);
    payload += "&Message=" + awsAuth.urlEncode(message);
    payload += "&Subject=" + awsAuth.urlEncode(subject);
    
    // Create SigV4 signed headers for SNS
    SigV4Headers sigv4Headers = awsAuth.createSigV4Headers(
        "POST", 
        snsEndpoint, 
        snsHost, 
        payload,
        "application/x-www-form-urlencoded"
    );
    
    if (!sigv4Headers.isValid) {
        SDLogger::getInstance().errorf("Failed to create SigV4 headers for SNS");
        return false;
    }
    
    // Make the SNS API request
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Configure proper certificates in production
    
    String url = "https://" + snsHost + snsEndpoint;
    http.begin(client, url);
    
    // Set headers for SNS
    http.addHeader("Authorization", sigv4Headers.authorization);
    http.addHeader("X-Amz-Date", sigv4Headers.date);
    http.addHeader("X-Amz-Security-Token", awsAuth.getCurrentCredentials().sessionToken);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Host", snsHost);
    http.addHeader("User-Agent", "ESP32-CatCam/1.0");
    
    SDLogger::getInstance().infof("Making SNS API request...");
    
    int httpResponseCode = http.POST(payload);
    
    bool success = false;
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
        String response = http.getString();
        SDLogger::getInstance().infof("SNS notification sent successfully!");
        SDLogger::getInstance().debugf("SNS Response: %s", response.c_str());
        success = true;
    } else {
        SDLogger::getInstance().errorf("SNS notification failed. HTTP Code: %d", httpResponseCode);
        String response = http.getString();
        SDLogger::getInstance().errorf("SNS Error response: %s", response.c_str());
    }
    
    http.end();
    return success;
}

// Lambda function invocation
bool invokeLambdaFunction(const String& functionPayload, String& lambdaResponse) {
    SDLogger::getInstance().infof("Invoking Lambda function: %s", LAMBDA_FUNCTION_NAME);
    
    // Ensure credentials are valid
    if (!awsAuth.refreshCredentialsIfNeeded(AWS_ROLE_ARN, "CatCamSession")) {
        SDLogger::getInstance().errorf("Failed to refresh AWS credentials for Lambda");
        return false;
    }
    
    // Create Lambda endpoint URL
    String lambdaHost = "lambda." + String(AWS_REGION) + ".amazonaws.com";
    String lambdaEndpoint = "/2015-03-31/functions/" + String(LAMBDA_FUNCTION_NAME) + "/invocations";
    
    // Create SigV4 signed headers for Lambda
    SigV4Headers sigv4Headers = awsAuth.createSigV4Headers(
        "POST", 
        lambdaEndpoint, 
        lambdaHost, 
        functionPayload,
        "application/json"
    );
    
    if (!sigv4Headers.isValid) {
        SDLogger::getInstance().errorf("Failed to create SigV4 headers for Lambda");
        return false;
    }
    
    // Make the Lambda API request
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure(); // Configure proper certificates in production
    
    String url = "https://" + lambdaHost + lambdaEndpoint;
    http.begin(client, url);
    
    // Set headers for Lambda
    http.addHeader("Authorization", sigv4Headers.authorization);
    http.addHeader("X-Amz-Date", sigv4Headers.date);
    http.addHeader("X-Amz-Security-Token", awsAuth.getCurrentCredentials().sessionToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Host", lambdaHost);
    http.addHeader("User-Agent", "ESP32-CatCam/1.0");
    http.addHeader("X-Amz-Invocation-Type", "RequestResponse"); // Synchronous invocation
    
    SDLogger::getInstance().infof("Making Lambda API request...");
    SDLogger::getInstance().debugf("Lambda URL: %s", url.c_str());
    
    int httpResponseCode = http.POST(functionPayload);
    
    bool success = false;
    if (httpResponseCode >= 200 && httpResponseCode < 300) {
        lambdaResponse = http.getString();
        SDLogger::getInstance().infof("Lambda function invoked successfully!");
        SDLogger::getInstance().debugf("Lambda Response: %s", lambdaResponse.c_str());
        success = true;
    } else {
        lambdaResponse = http.getString();
        SDLogger::getInstance().errorf("Lambda invocation failed. HTTP Code: %d", httpResponseCode);
        SDLogger::getInstance().errorf("Lambda Error response: %s", lambdaResponse.c_str());
    }
    
    http.end();
    return success;
}

// Enhanced detection handler that uses SNS and Lambda
void handleCatDetection(const String& catName, float confidence, const String& imagePath) {
    SDLogger::getInstance().infof("Cat detected: %s (%.1f%% confidence)", catName.c_str(), confidence * 100);
    
    // Send SNS notification for high-confidence detections
    if (confidence > 0.8) {
        String snsMessage = "Cat detected: " + catName + " with " + String(confidence * 100, 1) + "% confidence. Image: " + imagePath;
        String snsSubject = "CatCam Alert: " + catName + " Detected";
        
        if (sendSNSNotification(snsMessage, snsSubject)) {
            SDLogger::getInstance().infof("SNS alert sent for %s detection", catName.c_str());
        } else {
            SDLogger::getInstance().warnf("Failed to send SNS alert for %s detection", catName.c_str());
        }
    }
    
    // Invoke Lambda function for image processing
    DynamicJsonDocument lambdaPayload(1024);
    lambdaPayload["catName"] = catName;
    lambdaPayload["confidence"] = confidence;
    lambdaPayload["imagePath"] = imagePath;
    lambdaPayload["timestamp"] = millis();
    lambdaPayload["deviceId"] = WiFi.macAddress();
    lambdaPayload["location"] = "backyard"; // Configure as needed
    
    String payloadStr;
    serializeJson(lambdaPayload, payloadStr);
    
    String lambdaResponse;
    if (invokeLambdaFunction(payloadStr, lambdaResponse)) {
        SDLogger::getInstance().infof("Lambda processing completed for %s", catName.c_str());
        
        // Parse Lambda response for any actions
        DynamicJsonDocument responseDoc(1024);
        deserializeJson(responseDoc, lambdaResponse);
        
        if (responseDoc.containsKey("action")) {
            String action = responseDoc["action"];
            SDLogger::getInstance().infof("Lambda recommended action: %s", action.c_str());
            
            // Handle different actions
            if (action == "activate_deterrent") {
                SDLogger::getInstance().infof("Lambda recommends activating deterrent for %s", catName.c_str());
                // Here you would trigger your atomizer or other deterrent
            } else if (action == "ignore") {
                SDLogger::getInstance().infof("Lambda recommends ignoring %s detection", catName.c_str());
            } else if (action == "monitor") {
                SDLogger::getInstance().infof("Lambda recommends continued monitoring of %s", catName.c_str());
            }
        }
    } else {
        SDLogger::getInstance().warnf("Failed to process %s detection with Lambda", catName.c_str());
    }
}

// Example function to test SNS and Lambda integration
void testAWSServices() {
    SDLogger::getInstance().infof("Testing AWS services integration...");
    
    // Test SNS notification
    String testMessage = "CatCam system test - device online and functional";
    if (sendSNSNotification(testMessage, "CatCam System Test")) {
        SDLogger::getInstance().infof("SNS test successful");
    } else {
        SDLogger::getInstance().errorf("SNS test failed");
    }
    
    // Test Lambda function
    DynamicJsonDocument testPayload(512);
    testPayload["test"] = true;
    testPayload["message"] = "System health check";
    testPayload["timestamp"] = millis();
    testPayload["deviceId"] = WiFi.macAddress();
    
    String payloadStr;
    serializeJson(testPayload, payloadStr);
    
    String lambdaResponse;
    if (invokeLambdaFunction(payloadStr, lambdaResponse)) {
        SDLogger::getInstance().infof("Lambda test successful");
    } else {
        SDLogger::getInstance().errorf("Lambda test failed");
    }
}
