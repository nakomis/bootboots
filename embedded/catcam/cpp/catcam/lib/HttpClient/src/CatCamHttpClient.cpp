#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "HttpClient.h"
#include "../../SDLogger/src/SDLogger.h"
#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp32-hal-psram.h>

// Initialize static member
const char* CatCam::CatCamHttpClient::AMAZON_ROOT_CA = nullptr;

namespace CatCam {

WiFiClientSecure client;

// Initialize PSRAM storage for the certificate
void CatCamHttpClient::init() {
    if (psramFound() && AMAZON_ROOT_CA == nullptr) {
        // Allocate space in PSRAM for the certificate
        char* cert = (char*)ps_malloc(2048);
        if (cert) {
            strcpy_P(cert, 
                "-----BEGIN CERTIFICATE-----\n"
                "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
                "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
                "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
                "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
                "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
                "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
                "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
                "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
                "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
                "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
                "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
                "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
                "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
                "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
                "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
                "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
                "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
                "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
                "-----END CERTIFICATE-----"
            );
            AMAZON_ROOT_CA = cert;
            SDLogger::getInstance().info("Amazon Root CA certificate stored in PSRAM");
        } else {
            SDLogger::getInstance().error("Failed to allocate PSRAM for certificate");
        }
    } else if (!psramFound()) {
        SDLogger::getInstance().warn("PSRAM not available, using regular memory for certificate");
    }
}

CatCamHttpClient::CatCamHttpClient() {
    // Constructor implementation
}

String CatCamHttpClient::postImage(NamedImage* namedImage, const char* url, const char* apiKey) {
    if (!namedImage || !namedImage->image || namedImage->size == 0) {
        return "{\"error\": \"Invalid image data\"}";
    }

    // Store original size (image should already be in PSRAM from Camera)
    size_t originalImageSize = namedImage->size;

    String response = "";
    bool success = false;
    WiFiClientSecure client;

    try {
        // Parse URL
        String urlStr = String(url);
        String host = "";
        String path = "/";
        int port = 80;
        bool isHttps = urlStr.startsWith("https://");
        
        // Remove protocol
        int protocolEnd = urlStr.indexOf("://");
        if (protocolEnd > 0) {
            urlStr = urlStr.substring(protocolEnd + 3);
        }
        
        // Get host and path
        int pathStart = urlStr.indexOf('/');
        if (pathStart >= 0) {
            host = urlStr.substring(0, pathStart);
            path = urlStr.substring(pathStart);
        } else {
            host = urlStr;
        }
        
        // Get port if specified
        int portStart = host.indexOf(':');
        if (portStart >= 0) {
            port = host.substring(portStart + 1).toInt();
            host = host.substring(0, portStart);
        } else if (isHttps) {
            port = 443; // Default HTTPS port
        }
        
        SDLogger::getInstance().infof("Connecting to %s:%d%s", host.c_str(), port, path.c_str());
        
        // Configure client with timeout
        client.setTimeout(20000);
        
        // Set the root CA certificate for validation
        client.setCACert(AMAZON_ROOT_CA);
        
        // Connect to server with retry logic
        int maxRetries = 3;
        for(int i = 0; i < maxRetries; i++) {
            if (client.connect(host.c_str(), port)) {
                SDLogger::getInstance().info("Connected to server");
                break;
            }
            SDLogger::getInstance().warnf("Connection attempt %d/%d failed, retrying...", i+1, maxRetries);
            delay(1000);
            
            if (i == maxRetries - 1) {
                throw std::runtime_error("Connection failed after retries");
            }
        }
        
        // Send HTTP headers
        client.print("POST ");
        client.print(path);
        client.println(" HTTP/1.1");
        client.print("Host: ");
        client.println(host);
        client.println("Connection: close");
        client.print("X-API-Key: ");
        client.println(apiKey);
        client.println("Content-Type: image/jpeg");
        client.print("Content-Length: ");
        client.println(originalImageSize);
        client.println();
        
        // Send image data in chunks (image is already in PSRAM from Camera)
        size_t bytesRemaining = originalImageSize;
        uint8_t* dataPtr = (uint8_t*)namedImage->image;
        
        while (bytesRemaining > 0) {
            size_t chunk = (bytesRemaining < 512) ? bytesRemaining : 512;
            size_t bytesWritten = client.write(dataPtr, chunk);
            
            if (bytesWritten != chunk) {
                throw std::runtime_error("Error writing image data");
            }
            
            dataPtr += bytesWritten;
            bytesRemaining -= bytesWritten;
            yield(); // Prevent WDT reset
        }
        
        // Read response with timeout
        unsigned long timeout = millis();
        while (client.connected() && !client.available()) {
            if (millis() - timeout > 20000) {
                throw std::runtime_error("Server response timeout");
            }
            delay(10);
        }
        
        // Read HTTP status line
        String status = client.readStringUntil('\n');
        SDLogger::getInstance().infof("Status: %d", status);
        
        // Skip headers
        while (client.available()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break; // End of headers
            }
        }
        
        // Read response body
        while (client.available()) {
            response += client.readStringUntil('\n');
        }
        
        client.stop();
        success = true;
        
    } catch (const std::exception& e) {
        SDLogger::getInstance().errorf("Error in postImage: %s", e.what());
        response = "{\"error\": \"" + String(e.what()) + "\"}";
    } catch (...) {
        SDLogger::getInstance().error("Unknown error in postImage");
        response = "{\"error\": \"Unknown error\"}";
    }

    // Always clean up
    if (client.connected()) {
        client.stop();
    }
    
    return success ? response : "{\"error\": \"Request failed\"}";
}

} // namespace CatCam
