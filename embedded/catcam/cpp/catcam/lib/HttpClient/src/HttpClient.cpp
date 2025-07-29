#include <WiFi.h>
#include <WiFiClient.h>
#include "HttpClient.h"

namespace CatCam {

HttpClient::HttpClient() {
    // Constructor implementation
}

String HttpClient::postImage(NamedImage* namedImage, const char* url, const char* apiKey) {
    WiFiClient client;
    String response = "";
    
    Serial.println("Starting HTTP POST to REST API...");
    Serial.printf("URL: %s\n", url);
    Serial.printf("Image size: %zu bytes\n", namedImage->size);
    
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
    
    // Connect to server
    if (!client.connect(host.c_str(), port)) {
        Serial.println("Connection failed");
        return "{\"error\": \"Connection failed\"}";
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
    client.println(namedImage->size);
    client.println();
    
    // Send image data
    client.write(namedImage->image, namedImage->size);
    
    // Read response
    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000) {
            client.stop();
            return "{\"error\": \"Client Timeout\"}";
        }
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
    
    Serial.println("Response received:");
    Serial.println(response);
    
    return response;
}

} // namespace CatCam
