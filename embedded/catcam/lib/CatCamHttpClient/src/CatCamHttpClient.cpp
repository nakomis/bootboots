#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp32-hal-psram.h>
#include "SDLogger.h"
#include "NamedImage.h"
#include "CatCamHttpClient.h"

CatCamHttpClient::CatCamHttpClient()
{

}

String CatCamHttpClient::postImage(NamedImage* namedImage, const char* host, const char* path, AWSAuth* awsAuth, bool trainingMode) {
    if (!namedImage || !namedImage->image || namedImage->size == 0) {
        SDLogger::getInstance().errorf("CatCamHttpClient: Invalid image data");
        return "{\"error\": \"Invalid image data\"}";
    }

    if (!awsAuth || !awsAuth->areCredentialsValid()) {
        SDLogger::getInstance().errorf("CatCamHttpClient: Invalid AWS credentials");
        return "{\"error\": \"Invalid AWS credentials\"}";
    }

    size_t imageSize = namedImage->size;

    // Construct the actual path, appending training mode query param if needed
    String actualPath = String(path);
    if (trainingMode) {
        actualPath += "?mode=training";
    }

    SDLogger::getInstance().infof("CatCamHttpClient: Posting image (%d bytes) to https://%s%s", imageSize, host, actualPath.c_str());

    // Create SigV4 signed headers for the image POST
    // For binary payloads, we calculate the actual SHA256 hash
    String contentType = "image/jpeg";

    // Create the SigV4 headers with the actual binary payload hash
    SigV4Headers headers = awsAuth->createSigV4HeadersForBinary("POST", actualPath.c_str(), host,
        namedImage->image, imageSize,
        contentType);

    if (!headers.isValid) {
        SDLogger::getInstance().errorf("CatCamHttpClient: Failed to create SigV4 headers");
        return "{\"error\": \"Failed to create SigV4 headers\"}";
    }

    WiFiClientSecure client;
    client.setInsecure();  // For testing - in production, set proper CA cert
    client.setTimeout(TIMEOUT_MS / 1000);

    // Connect to server
    SDLogger::getInstance().debugf("CatCamHttpClient: Connecting to %s:443", host);
    if (!client.connect(host, 443)) {
        SDLogger::getInstance().errorf("CatCamHttpClient: Connection failed");
        return "{\"error\": \"Connection failed\"}";
    }
    SDLogger::getInstance().debugf("CatCamHttpClient: Connected");

    // Send HTTP request manually to handle binary payload with SigV4
    client.print("POST ");
    client.print(actualPath);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.print("Content-Type: ");
    client.println(contentType);
    client.print("Content-Length: ");
    client.println(imageSize);
    client.print("X-Amz-Date: ");
    client.println(headers.date);
    client.print("X-Amz-Security-Token: ");
    client.println(headers.securityToken);
    client.print("Authorization: ");
    client.println(headers.authorization);
    // Include the actual payload hash (required for API Gateway)
    client.print("X-Amz-Content-Sha256: ");
    client.println(headers.payloadHash);
    client.println();

    // Send image data in chunks
    size_t bytesRemaining = imageSize;
    uint8_t* dataPtr = namedImage->image;
    size_t chunkSize = 1024;

    SDLogger::getInstance().debugf("CatCamHttpClient: Sending %d bytes", imageSize);

    while (bytesRemaining > 0) {
        size_t chunk = (bytesRemaining < chunkSize) ? bytesRemaining : chunkSize;
        size_t bytesWritten = client.write(dataPtr, chunk);

        if (bytesWritten != chunk) {
            SDLogger::getInstance().errorf("CatCamHttpClient: Error writing data (wrote %d of %d)", bytesWritten, chunk);
            client.stop();
            return "{\"error\": \"Error writing image data\"}";
        }

        dataPtr += bytesWritten;
        bytesRemaining -= bytesWritten;
        yield(); // Prevent WDT reset
    }

    SDLogger::getInstance().debugf("CatCamHttpClient: Data sent, waiting for response");

    // Read response with timeout
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > TIMEOUT_MS) {
            SDLogger::getInstance().errorf("CatCamHttpClient: Response timeout");
            client.stop();
            return "{\"error\": \"Response timeout\"}";
        }
        delay(10);
    }

    // Read HTTP status line
    String statusLine = client.readStringUntil('\n');
    SDLogger::getInstance().infof("CatCamHttpClient: %s", statusLine.c_str());

    // Extract status code
    int statusCode = 0;
    int spaceIndex = statusLine.indexOf(' ');
    if (spaceIndex > 0) {
        statusCode = statusLine.substring(spaceIndex + 1, spaceIndex + 4).toInt();
    }

    // Read response headers and get Content-Length
    int contentLength = 0;
    while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            break;
        }
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        }
        SDLogger::getInstance().tracef("CatCamHttpClient: Header: %s", line.c_str());
    }

    // Read response body - wait for data if needed
    String response = "";
    SDLogger::getInstance().tracef("CatCamHttpClient: Reading body, Content-Length: %d", contentLength);

    if (contentLength > 0) {
        response.reserve(contentLength + 1);
        unsigned long bodyTimeout = millis();
        while ((int)response.length() < contentLength && (millis() - bodyTimeout) < 5000) {
            while (client.available() && (int)response.length() < contentLength) {
                char c = client.read();
                response += c;
            }
            if ((int)response.length() < contentLength) {
                delay(10);
            }
        }
        SDLogger::getInstance().tracef("CatCamHttpClient: Read %d bytes of body", response.length());
    }
    else {
        // Fallback: read whatever is available
        delay(100); // Give time for data to arrive
        while (client.available()) {
            response += (char)client.read();
        }
    }

    client.stop();

    SDLogger::getInstance().infof("CatCamHttpClient: Response code: %d", statusCode);
    // Log response body at INFO level for debugging auth failures
    if (statusCode != 200) {
        SDLogger::getInstance().infof("CatCamHttpClient: statusCode: %d", statusCode);
        SDLogger::getInstance().infof("CatCamHttpClient: Body length: %d", response.length());
        if (response.length() > 0) {
            // Log first 200 chars as hex to debug
            String hexDump = "";
            for (int i = 0; i < min((int)response.length(), 100); i++) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02x ", (uint8_t)response[i]);
                hexDump += hex;
            }
            SDLogger::getInstance().infof("CatCamHttpClient: Hex[0:100]: %s", hexDump.c_str());

            // Log the full response in chunks of 300 chars
            String cleanResponse = response;
            cleanResponse.replace("\n", "|");
            cleanResponse.replace("\r", "");
            cleanResponse.replace("\"", "'");  // Replace quotes to avoid log issues
            int chunkSize = 300;
            for (int i = 0; i < min((int)cleanResponse.length(), 1800); i += chunkSize) {
                String chunk = cleanResponse.substring(i, min(i + chunkSize, (int)cleanResponse.length()));
                SDLogger::getInstance().infof("CatCamHttpClient: Body[%d-%d]: %s", i, min(i + chunkSize, (int)cleanResponse.length()), chunk.c_str());
                delay(10);  // Small delay to ensure log transmission
            }
        }
        else {
            SDLogger::getInstance().infof("CatCamHttpClient: No response body received");
        }
    }
    else {
        SDLogger::getInstance().debugf("CatCamHttpClient: Response: %s", response.c_str());
    }

    if (statusCode == 200) {
        return response;
    }
    else {
        return "{\"error\": \"HTTP " + String(statusCode) + "\", \"response\": \"" + response + "\"}";
    }
}
