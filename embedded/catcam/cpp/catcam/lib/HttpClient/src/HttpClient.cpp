#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp32-hal-psram.h>

// Initialize static member
const char* CatCam::HttpClient::AMAZON_ROOT_CA = nullptr;

namespace CatCam {

WiFiClientSecure client;

// PSRAM helper methods
uint8_t* HttpClient::copyToPSRAM(const uint8_t* src, size_t size) {
    if (!src || size == 0) {
        Serial.println("Invalid source or size for PSRAM copy");
        return nullptr;
    }

    uint8_t* buffer = (psramFound()) ? 
        (uint8_t*)ps_malloc(size) : 
        new (std::nothrow) uint8_t[size];
    
    if (buffer) {
        memcpy(buffer, src, size);
        Serial.printf("Copied %d bytes to %s at %p\n", 
            size, psramFound() ? "PSRAM" : "heap", buffer);
    } else {
        Serial.println("Failed to allocate memory for image copy");
    }
    return buffer;
}

void HttpClient::freePSRAM(uint8_t* ptr) {
    if (!ptr) return;
    
    if (psramFound()) {
        free(ptr);
    } else {
        delete[] ptr;
    }
}

// Initialize PSRAM storage for the certificate
void HttpClient::init() {
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
            Serial.println("Amazon Root CA certificate stored in PSRAM");
        } else {
            Serial.println("Failed to allocate PSRAM for certificate");
        }
    } else if (!psramFound()) {
        Serial.println("PSRAM not available, using regular memory for certificate");
    }
}

HttpClient::HttpClient() {
    // Constructor implementation
}

String HttpClient::postImage(NamedImage* namedImage, const char* url, const char* apiKey) {
    if (!namedImage || !namedImage->image || namedImage->size == 0) {
        return "{\"error\": \"Invalid image data\"}";
    }

    // Store original size before we modify the NamedImage structure
    size_t originalImageSize = namedImage->size;

    // Copy image to PSRAM first
    uint8_t* imageCopy = copyToPSRAM((const uint8_t*)namedImage->image, originalImageSize);
    if (!imageCopy) {
        return "{\"error\": \"Failed to allocate PSRAM for image\"}";
    }

    // Free the camera buffer immediately to save memory
    free(namedImage->image);
    namedImage->image = nullptr;
    namedImage->size = 0;

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
        
        Serial.printf("Connecting to %s:%d%s\n", host.c_str(), port, path.c_str());
        
        // Configure client with timeout
        client.setTimeout(20000);
        
        // Set the root CA certificate for validation
        client.setCACert(AMAZON_ROOT_CA);
        
        // Connect to server with retry logic
        int maxRetries = 3;
        for(int i = 0; i < maxRetries; i++) {
            if (client.connect(host.c_str(), port)) {
                Serial.println("Connected to server");
                break;
            }
            Serial.printf("Connection attempt %d/%d failed, retrying...\n", i+1, maxRetries);
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
        client.println(originalImageSize);  // Use the stored original size
        client.println();
        
        // Send image data in chunks from PSRAM
        size_t bytesRemaining = originalImageSize;  // Use the stored original size
        uint8_t* dataPtr = imageCopy;
        
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
        Serial.print("Status: ");
        Serial.println(status);
        
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
        Serial.print("Error in postImage: ");
        Serial.println(e.what());
        response = "{\"error\": \"" + String(e.what()) + "\"}";
    } catch (...) {
        Serial.println("Unknown error in postImage");
        response = "{\"error\": \"Unknown error\"}";
    }

    // Always clean up
    if (client.connected()) {
        client.stop();
    }
    
    // Free the PSRAM copy
    freePSRAM(imageCopy);
    
    return success ? response : "{\"error\": \"Request failed\"}";
}

} // namespace CatCam
