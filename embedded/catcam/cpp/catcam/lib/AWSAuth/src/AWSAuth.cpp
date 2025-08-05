#include "AWSAuth.h"
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <HTTPClient.h>

AWSAuth::AWSAuth(const char* region) : region(region) {
    credentials.isValid = false;
    credentials.expiration = 0;
}

bool AWSAuth::initialize(const char* awsCertCA, const char* awsCertCRT, const char* awsCertPrivate) {
    // Configure SSL client with certificates
    sslClient.setCACert(awsCertCA);
    sslClient.setCertificate(awsCertCRT);
    sslClient.setPrivateKey(awsCertPrivate);
    
    // Configure time for certificate validation
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    SDLogger::getInstance().infof("AWS Auth initialized with certificates");
    return true;
}

bool AWSAuth::getCredentials(const char* roleArn, const char* roleSessionName) {
    // For ESP32 with IoT certificates, we'll use STS AssumeRoleWithWebIdentity
    // This requires creating a web identity token from the IoT certificate
    
    String stsEndpoint = "https://sts." + region + ".amazonaws.com/";
    
    // Create the STS request parameters
    String webIdentityToken = createWebIdentityToken();
    if (webIdentityToken.isEmpty()) {
        SDLogger::getInstance().errorf("Failed to create web identity token");
        return false;
    }
    
    String postData = "Action=AssumeRoleWithWebIdentity";
    postData += "&Version=2011-06-15";
    postData += "&RoleArn=" + urlEncode(roleArn);
    postData += "&RoleSessionName=" + urlEncode(roleSessionName);
    postData += "&WebIdentityToken=" + urlEncode(webIdentityToken);
    postData += "&DurationSeconds=3600"; // 1 hour
    
    return performSTSRequest(stsEndpoint, postData);
}

bool AWSAuth::performSTSRequest(const String& stsEndpoint, const String& postData) {
    HTTPClient http;
    http.begin(sslClient, stsEndpoint);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Content-Length", String(postData.length()));
    
    int httpResponseCode = http.POST(postData);
    
    if (httpResponseCode == 200) {
        String response = http.getString();
        SDLogger::getInstance().infof("STS Response received");
        
        // Parse XML response (simplified - in production you'd want proper XML parsing)
        int accessKeyStart = response.indexOf("<AccessKeyId>") + 13;
        int accessKeyEnd = response.indexOf("</AccessKeyId>");
        
        int secretKeyStart = response.indexOf("<SecretAccessKey>") + 17;
        int secretKeyEnd = response.indexOf("</SecretAccessKey>");
        
        int sessionTokenStart = response.indexOf("<SessionToken>") + 14;
        int sessionTokenEnd = response.indexOf("</SessionToken>");
        
        int expirationStart = response.indexOf("<Expiration>") + 12;
        int expirationEnd = response.indexOf("</Expiration>");
        
        if (accessKeyStart > 12 && secretKeyStart > 16 && sessionTokenStart > 13) {
            credentials.accessKeyId = response.substring(accessKeyStart, accessKeyEnd);
            credentials.secretAccessKey = response.substring(secretKeyStart, secretKeyEnd);
            credentials.sessionToken = response.substring(sessionTokenStart, sessionTokenEnd);
            
            // Parse expiration time (simplified)
            String expirationStr = response.substring(expirationStart, expirationEnd);
            credentials.expiration = millis() + (3600 * 1000); // 1 hour from now
            
            credentials.isValid = true;
            
            SDLogger::getInstance().infof("AWS credentials obtained successfully");
            SDLogger::getInstance().infof("Access Key: %s...", credentials.accessKeyId.substring(0, 8).c_str());
            
            http.end();
            return true;
        }
    }
    
    SDLogger::getInstance().errorf("Failed to get AWS credentials. HTTP Code: %d", httpResponseCode);
    String response = http.getString();
    SDLogger::getInstance().errorf("Response: %s", response.c_str());
    
    http.end();
    return false;
}

String AWSAuth::createWebIdentityToken() {
    // For IoT certificates, we need to create a JWT token
    // This is a simplified implementation - in production you'd want proper JWT signing
    
    // JWT Header
    DynamicJsonDocument header(256);
    header["alg"] = "RS256";
    header["typ"] = "JWT";
    
    String headerJson;
    serializeJson(header, headerJson);
    
    // JWT Payload
    DynamicJsonDocument payload(512);
    payload["iss"] = "ESP32-IoT-Device";
    payload["sub"] = "iot-device";
    payload["aud"] = "sts.amazonaws.com";
    payload["exp"] = (millis() / 1000) + 3600; // 1 hour from now
    payload["iat"] = millis() / 1000;
    
    String payloadJson;
    serializeJson(payload, payloadJson);
    
    // Base64 encode header and payload
    String encodedHeader = base64Encode(headerJson);
    String encodedPayload = base64Encode(payloadJson);
    
    // Create signature (simplified - you'd sign with private key)
    String message = encodedHeader + "." + encodedPayload;
    String signature = base64Encode("simplified_signature");
    
    return message + "." + signature;
}

String AWSAuth::base64Encode(const String& input) {
    size_t outputLength;
    unsigned char* output = nullptr;
    
    // Calculate required buffer size
    int ret = mbedtls_base64_encode(nullptr, 0, &outputLength, 
                                   (const unsigned char*)input.c_str(), input.length());
    
    if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        output = new unsigned char[outputLength];
        ret = mbedtls_base64_encode(output, outputLength, &outputLength,
                                   (const unsigned char*)input.c_str(), input.length());
        
        if (ret == 0) {
            String result = String((char*)output);
            delete[] output;
            return result;
        }
        delete[] output;
    }
    
    return "";
}

bool AWSAuth::areCredentialsValid() const {
    return credentials.isValid && (millis() < credentials.expiration);
}

bool AWSAuth::refreshCredentialsIfNeeded(const char* roleArn, const char* roleSessionName) {
    if (!areCredentialsValid()) {
        return getCredentials(roleArn, roleSessionName);
    }
    return true;
}

SigV4Headers AWSAuth::createSigV4Headers(const String& method, const String& uri, 
                                         const String& host, const String& payload,
                                         const String& contentType) {
    SigV4Headers headers;
    headers.isValid = false;
    
    if (!areCredentialsValid()) {
        SDLogger::getInstance().errorf("Invalid AWS credentials for SigV4 signing");
        return headers;
    }
    
    // Get current time
    String amzDate = getISOTimestamp();
    String dateStamp = getDateStamp();
    
    // Create canonical request
    String payloadHash = sha256Hash(payload);
    String canonicalHeaders = "host:" + host + "\n" + 
                             "x-amz-date:" + amzDate + "\n" +
                             "x-amz-security-token:" + credentials.sessionToken + "\n";
    String signedHeaders = "host;x-amz-date;x-amz-security-token";
    
    String canonicalRequest = createCanonicalRequest(method, uri, "", canonicalHeaders, 
                                                    signedHeaders, payloadHash);
    
    // Create string to sign
    String algorithm = "AWS4-HMAC-SHA256";
    String credentialScope = dateStamp + "/" + region + "/execute-api/aws4_request";
    String stringToSign = createStringToSign(algorithm, amzDate, credentialScope, 
                                           sha256Hash(canonicalRequest));
    
    // Create signature
    String signingKey = getSigningKey(credentials.secretAccessKey, dateStamp, region, "execute-api");
    String signature = createSignature(stringToSign, signingKey);
    
    // Create authorization header
    String authorizationHeader = algorithm + " " +
                                "Credential=" + credentials.accessKeyId + "/" + credentialScope + ", " +
                                "SignedHeaders=" + signedHeaders + ", " +
                                "Signature=" + signature;
    
    headers.authorization = authorizationHeader;
    headers.date = amzDate;
    headers.contentType = contentType;
    headers.host = host;
    headers.isValid = true;
    
    return headers;
}

String AWSAuth::createCanonicalRequest(const String& method, const String& uri, 
                                      const String& queryString, const String& headers, 
                                      const String& signedHeaders, const String& payloadHash) {
    return method + "\n" +
           uri + "\n" +
           queryString + "\n" +
           headers + "\n" +
           signedHeaders + "\n" +
           payloadHash;
}

String AWSAuth::createStringToSign(const String& algorithm, const String& requestDateTime,
                                  const String& credentialScope, const String& canonicalRequestHash) {
    return algorithm + "\n" +
           requestDateTime + "\n" +
           credentialScope + "\n" +
           canonicalRequestHash;
}

String AWSAuth::createSignature(const String& stringToSign, const String& signingKey) {
    return hmacSha256(signingKey, stringToSign);
}

String AWSAuth::getSigningKey(const String& key, const String& dateStamp, 
                             const String& regionName, const String& serviceName) {
    String kDate = hmacSha256("AWS4" + key, dateStamp);
    String kRegion = hmacSha256(kDate, regionName);
    String kService = hmacSha256(kRegion, serviceName);
    String kSigning = hmacSha256(kService, "aws4_request");
    return kSigning;
}

String AWSAuth::hmacSha256(const String& key, const String& data) {
    // Simplified HMAC-SHA256 implementation using mbedtls
    unsigned char output[32];
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    mbedtls_md_hmac(md_info, (const unsigned char*)key.c_str(), key.length(),
                    (const unsigned char*)data.c_str(), data.length(), output);
    
    // Convert to hex string
    String result = "";
    for (int i = 0; i < 32; i++) {
        if (output[i] < 16) result += "0";
        result += String(output[i], HEX);
    }
    return result;
}

String AWSAuth::sha256Hash(const String& data) {
    unsigned char output[32];
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    mbedtls_md(md_info, (const unsigned char*)data.c_str(), data.length(), output);
    
    // Convert to hex string
    String result = "";
    for (int i = 0; i < 32; i++) {
        if (output[i] < 16) result += "0";
        result += String(output[i], HEX);
    }
    return result;
}

String AWSAuth::urlEncode(const String& str) {
    String encoded = "";
    char c;
    char code0;
    char code1;
    
    for (int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encoded += '+';
        } else if (isalnum(c)) {
            encoded += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            code0 = ((c >> 4) & 0xf) + '0';
            if (((c >> 4) & 0xf) > 9) {
                code0 = ((c >> 4) & 0xf) - 10 + 'A';
            }
            encoded += '%';
            encoded += code0;
            encoded += code1;
        }
    }
    return encoded;
}

String AWSAuth::getISOTimestamp() {
    time_t now;
    time(&now);
    struct tm* timeinfo = gmtime(&now);
    
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", timeinfo);
    return String(buffer);
}

String AWSAuth::getDateStamp() {
    time_t now;
    time(&now);
    struct tm* timeinfo = gmtime(&now);
    
    char buffer[9];
    strftime(buffer, sizeof(buffer), "%Y%m%d", timeinfo);
    return String(buffer);
}
