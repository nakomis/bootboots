# AWS Authentication Library for ESP32

This library provides AWS authentication capabilities for ESP32 devices using IoT certificates and SigV4 request signing for API Gateway endpoints.

## Features

- **Certificate-based Authentication**: Uses IoT device certificates to authenticate with AWS
- **STS Credential Exchange**: Exchanges certificates for temporary AWS credentials
- **SigV4 Request Signing**: Creates properly signed headers for AWS API Gateway requests
- **Automatic Credential Refresh**: Handles credential expiration and renewal
- **Secure Communication**: Uses WiFiClientSecure for encrypted communications

## Setup Requirements

### 1. AWS IoT Setup

1. Create an IoT Thing in AWS IoT Core
2. Generate and download the device certificates:
   - Root CA certificate
   - Device certificate (.crt)
   - Private key (.key)
3. Create an IAM role that can be assumed by your IoT device
4. Set up the necessary policies for your use case

### 2. Certificates Configuration

The root CA, device certificate and private key (PEM), the credentials-provider endpoint and the
Thing name come from the device's `prov` partition (`lib/Provisioning`, written by
`scripts/provision.sh`), never from the firmware image (BOOT-10, see `docs/provisioning.md`).

### 3. AWS IAM Role Setup

Create an IAM role with the following trust policy (replace with your IoT thing ARN):

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Federated": "arn:aws:iam::YOUR_ACCOUNT:oidc-provider/oidc.us-east-1.amazonaws.com"
      },
      "Action": "sts:AssumeRoleWithWebIdentity",
      "Condition": {
        "StringEquals": {
          "oidc.us-east-1.amazonaws.com:sub": "YOUR_IOT_THING_ARN"
        }
      }
    }
  ]
}
```

Attach necessary permissions to the role for your API Gateway and other AWS services.

## Usage

### Basic Usage

```cpp
#include "AWSAuth.h"
#include "Provisioning.h"

const char* AWS_REGION = "us-east-1";
const char* AWS_ROLE_ARN = "arn:aws:iam::123456789012:role/ESP32IoTRole";

AWSAuth awsAuth(AWS_REGION);

void setup() {
    // Initialise with the certificates, endpoint and Thing name from the prov partition
    const ProvisioningData& prov = Provisioning::getInstance().data();
    awsAuth.initialize(prov.rootCa.c_str(), prov.deviceCert.c_str(), prov.deviceKey.c_str(),
                       prov.credentialsEndpoint.c_str(), prov.thingName.c_str());
    
    // Get AWS credentials
    if (awsAuth.getCredentials(AWS_ROLE_ARN, "ESP32Session")) {
        Serial.println("Successfully obtained AWS credentials!");
    }
}
```

### API Gateway Request with SigV4 Signing

```cpp
void makeAPIRequest() {
    String payload = "{\"message\":\"Hello from ESP32\"}";
    
    // Create SigV4 signed headers
    SigV4Headers headers = awsAuth.createSigV4Headers(
        "POST", 
        "/prod/endpoint", 
        "your-api-id.execute-api.us-east-1.amazonaws.com", 
        payload
    );
    
    if (headers.isValid) {
        // Make HTTP request with signed headers
        HTTPClient http;
        WiFiClientSecure client;
        
        http.begin(client, "https://your-api-id.execute-api.us-east-1.amazonaws.com/prod/endpoint");
        http.addHeader("Authorization", headers.authorization);
        http.addHeader("X-Amz-Date", headers.date);
        http.addHeader("X-Amz-Security-Token", awsAuth.getCurrentCredentials().sessionToken);
        http.addHeader("Content-Type", "application/json");
        
        int responseCode = http.POST(payload);
        // Handle response...
    }
}
```

### Credential Management

```cpp
void loop() {
    // Check if credentials need refreshing
    if (!awsAuth.areCredentialsValid()) {
        Serial.println("Refreshing credentials...");
        awsAuth.refreshCredentialsIfNeeded(AWS_ROLE_ARN, "ESP32Session");
    }
    
    // Your application logic here
    delay(60000); // Check every minute
}
```

## API Reference

### AWSAuth Class

#### Constructor
- `AWSAuth(const char* region = "us-east-1")`: Initialize with AWS region

#### Methods
- `bool initialize(const char* awsCertCA, const char* awsCertCRT, const char* awsCertPrivate)`: Initialize with certificates
- `bool getCredentials(const char* roleArn, const char* roleSessionName)`: Exchange certificates for AWS credentials
- `SigV4Headers createSigV4Headers(...)`: Create signed headers for API requests
- `bool areCredentialsValid()`: Check if current credentials are still valid
- `bool refreshCredentialsIfNeeded(...)`: Refresh credentials if they're expired
- `AWSCredentials getCurrentCredentials()`: Get current credential information

### Data Structures

#### AWSCredentials
```cpp
struct AWSCredentials {
    String accessKeyId;
    String secretAccessKey;
    String sessionToken;
    unsigned long expiration;
    bool isValid;
};
```

#### SigV4Headers
```cpp
struct SigV4Headers {
    String authorization;
    String date;
    String contentType;
    String host;
    bool isValid;
};
```

## Security Considerations

1. **Certificate Storage**: Store certificates securely in flash memory
2. **Network Security**: Always use HTTPS/TLS for communications
3. **Credential Management**: Credentials are temporary and automatically expire
4. **Error Handling**: Implement proper error handling for authentication failures
5. **Time Synchronization**: Ensure accurate system time for signature validation

## Troubleshooting

### Common Issues

1. **Certificate Errors**: Ensure certificates are properly formatted and valid
2. **Time Issues**: SigV4 signing requires accurate time - configure NTP
3. **Network Connectivity**: Verify WiFi connection and internet access
4. **IAM Permissions**: Check that the IAM role has necessary permissions
5. **Regional Endpoints**: Ensure you're using the correct AWS region

### Debug Output

Enable serial output to see detailed authentication flow:

```cpp
Serial.begin(115200);
// The library will output detailed debug information
```

## Examples

See the `examples/` directory for complete working examples:
- `aws_auth_example.cpp`: Basic authentication and API Gateway usage
- Integration with existing MessageQueue system

## Dependencies

- ArduinoJson (for JSON parsing)
- WiFiClientSecure (for HTTPS communications)
- HTTPClient (for HTTP requests)
- mbedtls (for cryptographic operations)

## License

This library is provided as-is for educational and development purposes.
