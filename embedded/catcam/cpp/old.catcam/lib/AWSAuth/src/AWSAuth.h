#ifndef CATCAM_AWSAUTH_H
#define CATCAM_AWSAUTH_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "SDLogger.h"

struct AWSCredentials {
    String accessKeyId;
    String secretAccessKey;
    String sessionToken;
    unsigned long expiration;
    bool isValid;
};

struct SigV4Headers {
    String authorization;
    String date;
    String contentType;
    String host;
    bool isValid;
};

class AWSAuth {
public:
    AWSAuth(const char* region = "us-east-1");
    
    // Initialize with certificates from secrets.h
    bool initialize(const char* awsCertCA, const char* awsCertCRT, const char* awsCertPrivate);
    
    // Exchange certificates for temporary AWS credentials using STS AssumeRoleWithWebIdentity
    bool getCredentials(const char* roleArn, const char* roleSessionName = "ESP32Session");
    
    // Create SigV4 signed headers for API Gateway request
    SigV4Headers createSigV4Headers(const String& method, const String& uri, 
                                   const String& host, const String& payload = "",
                                   const String& contentType = "application/json");
    
    // Get current credentials
    AWSCredentials getCurrentCredentials() const { return credentials; }
    
    // Check if credentials are still valid (not expired)
    bool areCredentialsValid() const;
    
    // Refresh credentials if needed
    bool refreshCredentialsIfNeeded(const char* roleArn, const char* roleSessionName = "ESP32Session");

    // Utility methods
    String urlEncode(const String& str);

private:
    String region;
    AWSCredentials credentials;
    WiFiClientSecure sslClient;
    
    // Helper methods for SigV4 signing
    String createCanonicalRequest(const String& method, const String& uri, 
                                 const String& queryString, const String& headers, 
                                 const String& signedHeaders, const String& payloadHash);
    
    String createStringToSign(const String& algorithm, const String& requestDateTime,
                             const String& credentialScope, const String& canonicalRequestHash);
    
    String createSignature(const String& stringToSign, const String& signingKey);
    
    String getSigningKey(const String& key, const String& dateStamp, 
                        const String& regionName, const String& serviceName);
    
    String hmacSha256(const String& key, const String& data);
    String sha256Hash(const String& data);
    String getISOTimestamp();
    String getDateStamp();
    
    // STS specific methods
    bool performSTSRequest(const String& stsEndpoint, const String& postData);
    String createWebIdentityToken();
    String base64Encode(const String& input);
};

#endif
