# BootBoots - Autonomous Cat Territory Management System

![BootBoots Logo](https://via.placeholder.com/300x100/2c3e50/ffffff?text=BootBoots)

## 🐱 Project Overview

BootBoots is an intelligent, autonomous cat territory management system built on the ESP32-CAM platform. It uses AI-powered computer vision to identify specific cats and selectively activate deterrent mechanisms to protect designated areas while keeping friendly cats safe.

### Key Features

- **AI-Powered Cat Recognition**: Uses AWS SageMaker for accurate cat identification
- **Selective Deterrent System**: Ultrasonic atomizer activation based on cat identity
- **Ultra-Safe Design**: Multiple validation layers prevent false positives
- **Special Kappa Protection**: Enhanced safety for frequently misidentified cats
- **Comprehensive Logging**: SD card storage with detailed event logs
- **Bluetooth Low Energy**: Remote monitoring and log access
- **Over-The-Air Updates**: Remote firmware and web app updates
- **WiFi Connectivity**: Cloud integration and remote access
- **Real-Time Monitoring**: Web-based dashboard for system status

## 🏗️ System Architecture

### Hardware Components

- **ESP32-CAM**: Main controller with integrated camera
- **Ultrasonic Atomizer**: Deterrent mechanism (PCF8574 controlled)
- **SD Card**: Local logging and image storage
- **WiFi Module**: Cloud connectivity and OTA updates
- **Bluetooth LE**: Local monitoring and diagnostics

### Software Stack

#### ESP32 Firmware
- **Platform**: ESP32 Arduino Framework with PlatformIO
- **Language**: C++17
- **Libraries**: 
  - ESP32-CAM for image capture
  - ArduinoJson for data serialization
  - ESP32 BLE Arduino for Bluetooth
  - ArduinoOTA for wireless updates
  - ESPmDNS for network discovery

#### Web Application
- **Framework**: React with TypeScript
- **UI Library**: Material-UI
- **Authentication**: AWS Cognito
- **Bluetooth**: Web Bluetooth API
- **Updates**: Service Worker with OTA support

## 🔧 Installation & Setup

### Prerequisites

- PlatformIO IDE or Arduino IDE with ESP32 support
- Node.js 16+ and npm
- AWS Account (for SageMaker integration)
- ESP32-CAM development board

### ESP32 Firmware Setup

1. **Clone the Repository**
   ```bash
   git clone <repository-url>
   cd bootboots/embedded/catcam/cpp/catcam
   ```

2. **Install PlatformIO**
   ```bash
   pip install platformio
   ```

3. **Configure Secrets**
   ```bash
   cp include/secrets.h.template include/secrets.h
   ```
   
   Edit `include/secrets.h` with your credentials:
   ```cpp
   // WiFi Configuration
   const char* const WIFI_SSID = "your-wifi-network";
   const char* const WIFI_PASSWORD = "your-wifi-password";
   
   // API Configuration
   const char* const API_URL = "https://your-sagemaker-endpoint.amazonaws.com/invocations";
   const char* const API_KEY = "your-api-key-here";
   
   // OTA Update Configuration
   const char* const OTA_PASSWORD = "your-secure-ota-password";
   ```

4. **Build and Upload**
   ```bash
   pio run --target upload
   ```

### Web Application Setup

1. **Navigate to Web App Directory**
   ```bash
   cd /path/to/sandboxsite/sandbox-app
   ```

2. **Install Dependencies**
   ```bash
   npm install
   ```

3. **Start Development Server**
   ```bash
   npm start
   ```

4. **Access Application**
   - Local: http://localhost:3000
   - Production: https://sandbox.nakomis.com

## 🤖 AI Cat Recognition System

### Supported Cats

The system recognizes 6 specific cats with unique behavioral profiles:

| Index | Cat Name | Special Notes |
|-------|----------|---------------|
| 0     | Boots    | Standard detection |
| 1     | Chi      | Standard detection |
| 2     | **Kappa** | **Protected** - Enhanced safety due to 67% misidentification rate |
| 3     | Mu       | Standard detection |
| 4     | Tau      | Standard detection |
| 5     | Wolf     | Standard detection |

### Safety Thresholds

- **Minimum Confidence**: 85% for deterrent activation
- **Kappa Protection**: 95% confidence required + additional validation
- **Cooldown Period**: 30 seconds between activations
- **False Positive Prevention**: Multiple validation layers

### Detection Process

1. **Image Capture**: ESP32-CAM captures high-resolution image
2. **AI Analysis**: Image sent to AWS SageMaker endpoint
3. **Confidence Evaluation**: Multiple threshold checks
4. **Safety Validation**: Special Kappa protection logic
5. **Action Decision**: Deterrent activation or safe passage
6. **Logging**: Complete event logging to SD card

## 🔄 Over-The-Air (OTA) Update System

### ESP32 OTA Updates

#### Features
- **Password Protection**: Secure updates with configurable password
- **Network Discovery**: Available as "BootBoots-CatCam.local"
- **Safety Integration**: Automatically disables deterrent during updates
- **Progress Tracking**: Real-time update status via Bluetooth
- **Error Recovery**: Comprehensive error handling and logging

#### Deployment Process

1. **Prepare New Firmware**
   ```bash
   cd bootboots/embedded/catcam/cpp/catcam
   # Make your code changes
   pio run  # Verify build succeeds
   ```

2. **Deploy via PlatformIO**
   ```bash
   # Option 1: Command line OTA upload
   pio run --target upload --upload-port BootBoots-CatCam.local
   
   # Option 2: Use PlatformIO IDE
   # 1. Open PlatformIO IDE
   # 2. Click "Upload" dropdown
   # 3. Select "Upload via Network"
   # 4. Choose "BootBoots-CatCam" from device list
   # 5. Enter OTA password when prompted
   ```

3. **Deploy via Arduino IDE**
   ```bash
   # 1. Open Arduino IDE
   # 2. Go to Tools > Port
   # 3. Select "BootBoots-CatCam at <IP address>"
   # 4. Click Upload
   # 5. Enter OTA password when prompted
   ```

#### OTA Security

- **Password Authentication**: Configurable in `secrets.h`
- **Network Encryption**: WPA2/WPA3 WiFi security
- **mDNS Discovery**: Secure local network discovery
- **Update Validation**: Firmware signature verification

### Web Application OTA Updates

#### Features
- **Service Worker**: Background update detection
- **Automatic Checks**: Every 30 minutes
- **Manual Updates**: User-initiated update checks
- **Progressive Enhancement**: Works offline with cached resources
- **Safe Rollback**: Automatic fallback on update failure
- **S3 + CloudFront**: AWS-hosted with global CDN distribution

#### Deployment Process

**Your Setup**: AWS CDK with S3 bucket + CloudFront distribution

1. **Update Version Number**
   ```bash
   cd sandboxsite/sandbox-app
   # Update version in package.json
   npm version patch  # or minor/major
   ```

2. **Build Production Version**
   ```bash
   npm run build
   # Creates optimized build in ./build/ directory
   ```

3. **Deploy to S3 Bucket**
   ```bash
   # Get your S3 bucket name from CDK stack
   aws cloudformation describe-stacks --stack-name SandboxCloudfrontStack \
     --query 'Stacks[0].Outputs[?OutputKey==`BucketName`].OutputValue' --output text
   
   # Sync build files to S3 (replace BUCKET_NAME with actual bucket)
   aws s3 sync ./build/ s3://BUCKET_NAME --delete
   
   # Set proper cache headers for static assets
   aws s3 cp ./build/static/ s3://BUCKET_NAME/static/ --recursive \
     --cache-control "max-age=31536000" --metadata-directive REPLACE
   
   # Set short cache for index.html to enable quick updates
   aws s3 cp ./build/index.html s3://BUCKET_NAME/index.html \
     --cache-control "max-age=300" --metadata-directive REPLACE
   ```

4. **Invalidate CloudFront Cache**
   ```bash
   # Get CloudFront distribution ID
   aws cloudformation describe-stacks --stack-name SandboxCloudfrontStack \
     --query 'Stacks[0].Outputs[?OutputKey==`DistributionId`].OutputValue' --output text
   
   # Create invalidation (replace DISTRIBUTION_ID with actual ID)
   aws cloudfront create-invalidation --distribution-id DISTRIBUTION_ID \
     --paths "/*"
   ```

5. **Automated Deployment Script**
   Create `deploy.sh` in `sandbox-app/` directory:
   ```bash
   #!/bin/bash
   set -e
   
   echo "🚀 Starting BootBoots Web App Deployment..."
   
   # Get current version
   CURRENT_VERSION=$(node -p "require('./package.json').version")
   echo "📦 Current version: $CURRENT_VERSION"
   
   # Build production version
   echo "🔨 Building production version..."
   npm run build
   
   # Get AWS resources from CloudFormation
   BUCKET_NAME=$(aws cloudformation describe-stacks --stack-name SandboxCloudfrontStack \
     --query 'Stacks[0].Outputs[?OutputKey==`BucketName`].OutputValue' --output text)
   DISTRIBUTION_ID=$(aws cloudformation describe-stacks --stack-name SandboxCloudfrontStack \
     --query 'Stacks[0].Outputs[?OutputKey==`DistributionId`].OutputValue' --output text)
   
   echo "📡 Deploying to S3 bucket: $BUCKET_NAME"
   
   # Sync files to S3
   aws s3 sync ./build/ s3://$BUCKET_NAME --delete
   
   # Set cache headers
   aws s3 cp ./build/static/ s3://$BUCKET_NAME/static/ --recursive \
     --cache-control "max-age=31536000" --metadata-directive REPLACE
   aws s3 cp ./build/index.html s3://$BUCKET_NAME/index.html \
     --cache-control "max-age=300" --metadata-directive REPLACE
   
   # Invalidate CloudFront
   echo "🌐 Invalidating CloudFront distribution: $DISTRIBUTION_ID"
   aws cloudfront create-invalidation --distribution-id $DISTRIBUTION_ID --paths "/*"
   
   echo "✅ Deployment complete! Version $CURRENT_VERSION is now live."
   echo "🔗 URL: https://sandbox.nakomis.com"
   ```

   Make it executable and run:
   ```bash
   chmod +x deploy.sh
   ./deploy.sh
   ```

#### Versioning Strategy

**Semantic Versioning**: Use `package.json` version field
- **Patch** (1.0.1): Bug fixes, minor updates
- **Minor** (1.1.0): New features, OTA updates
- **Major** (2.0.0): Breaking changes

**Service Worker Versioning**:
```javascript
// In public/sw.js
const CACHE_NAME = 'bootboots-v' + process.env.REACT_APP_VERSION;
```

**Environment Variables**:
```bash
# Add to .env file
REACT_APP_VERSION=$npm_package_version
```

**Automatic Version Detection**:
```typescript
// In OTAUpdate.tsx
const currentVersion = process.env.REACT_APP_VERSION || '1.0.0';
```

#### Update Process
- Service worker detects new version via cache name change
- User sees update notification in OTA Updates tab
- One-click installation with automatic reload
- CloudFront ensures global distribution
- S3 versioning provides rollback capability

## 📊 System Monitoring

### Bluetooth Integration

The system provides comprehensive monitoring via Bluetooth Low Energy:

#### Available Services
- **System Status**: Real-time operational data
- **Log Access**: Recent SD card log entries
- **Update Status**: OTA update progress and status
- **Configuration**: Runtime parameter adjustment

#### Connection Process
1. Open web application
2. Navigate to "Bluetooth" tab
3. Click "Connect to BootBoots"
4. Select device from browser dialog
5. Monitor real-time system status

### Logging System

#### Log Categories
- **INFO**: Normal operations and status updates
- **WARN**: Non-critical issues and warnings
- **ERROR**: System errors and failures
- **CRITICAL**: Severe issues requiring attention

#### Log Storage
- **Primary**: SD card with rotation
- **Secondary**: Serial output for debugging
- **Remote**: Bluetooth access for monitoring

## 🔒 Security Features

### Authentication & Authorization
- **Web App**: AWS Cognito integration
- **ESP32 OTA**: Password-protected updates
- **Bluetooth**: Secure pairing and encryption

### Safety Systems
- **Multi-Layer Validation**: Prevents false positive activations
- **Kappa Protection**: Enhanced safety for misidentified cats
- **System Monitoring**: Continuous health checks
- **Emergency Shutdown**: Manual and automatic safety stops

### Network Security
- **WPA2/WPA3**: Secure WiFi connectivity
- **HTTPS**: Encrypted web communications
- **mDNS**: Secure local network discovery

## 🛠️ Development

### Project Structure

```
bootboots/embedded/catcam/cpp/catcam/
├── src/
│   └── main.cpp                 # Main application logic
├── lib/
│   ├── Atomizer/               # Deterrent control
│   ├── BluetoothService/       # BLE communication
│   ├── Camera/                 # ESP32-CAM integration
│   ├── HttpClient/             # API communication
│   ├── MessageQueue/           # Event handling
│   ├── NamedImage/             # Image data structures
│   ├── OTAUpdate/              # Over-the-air updates
│   ├── PCF8574Manager/         # I2C GPIO expansion
│   ├── SDLogger/               # Logging system
│   └── WifiConnect/            # WiFi management
├── include/
│   ├── SystemState.h           # Shared system state
│   └── secrets.h.template      # Configuration template
└── platformio.ini              # Build configuration
```

### Building from Source

1. **Install Dependencies**
   ```bash
   pio lib install
   ```

2. **Build Firmware**
   ```bash
   pio run
   ```

3. **Run Tests**
   ```bash
   pio test
   ```

4. **Upload to Device**
   ```bash
   pio run --target upload
   ```

### Memory Usage

Current build statistics:
- **RAM Usage**: 21.3% (69,808 / 327,680 bytes)
- **Flash Usage**: 62.9% (1,977,777 / 3,145,728 bytes)
- **Build Time**: ~28 seconds

## 📈 Performance Metrics

### System Specifications
- **Detection Latency**: <2 seconds from image capture to decision
- **WiFi Range**: 50+ meters (depending on environment)
- **Battery Life**: N/A (powered system)
- **Storage Capacity**: Limited by SD card size
- **Concurrent Connections**: 1 Bluetooth, unlimited web clients

### Reliability Features
- **Watchdog Timer**: Automatic system recovery
- **Error Recovery**: Graceful failure handling
- **Health Monitoring**: Continuous system checks
- **Redundant Logging**: Multiple log destinations

## 🐛 Troubleshooting

### Common Issues

#### Build Errors
```bash
# Clean build environment
pio run --target clean
pio run

# Update libraries
pio lib update
```

#### WiFi Connection Issues
- Verify credentials in `secrets.h`
- Check network signal strength
- Ensure WPA2/WPA3 compatibility

#### OTA Update Failures
- Verify OTA password
- Check network connectivity
- Ensure sufficient flash memory
- Monitor serial output for errors

#### Bluetooth Connection Problems
- Clear browser Bluetooth cache
- Restart ESP32 device
- Check Web Bluetooth API support

### Debug Mode

Enable verbose logging:
```cpp
// In main.cpp
#define DEBUG_MODE 1
```

### Serial Monitoring
```bash
pio device monitor
```

## 🤝 Contributing

### Development Guidelines
1. Follow C++17 standards
2. Use consistent naming conventions
3. Add comprehensive comments
4. Include unit tests for new features
5. Update documentation

### Pull Request Process
1. Fork the repository
2. Create feature branch
3. Implement changes with tests
4. Update documentation
5. Submit pull request

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🙏 Acknowledgments

- **AWS SageMaker**: AI/ML model hosting
- **ESP32 Community**: Hardware and software support
- **React Community**: Web application framework
- **PlatformIO**: Development environment

## 📞 Support

For support and questions:
- **Issues**: GitHub Issues tracker
- **Documentation**: This README and inline comments
- **Community**: Project discussions

---

**BootBoots** - Keeping your territory safe, one cat at a time! 🐱🛡️
