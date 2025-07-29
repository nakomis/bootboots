#ifndef CATCAM_HTTPCLIENT_H
#define CATCAM_HTTPCLIENT_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <NamedImage.h>
#include <String.h>

namespace CatCam {

class HttpClient
{
public:
    HttpClient();
    // Initialize the HTTP client (must be called before first use)
    static void init();
    // Post an image to the specified URL with the given API key
    String postImage(NamedImage* namedImage, const char* url, const char* apiKey);

private:
    static const int TIMEOUT_MS = 30000; // 30 second timeout
    static const char* AMAZON_ROOT_CA;    // Certificate stored in PSRAM
    
    // PSRAM helper methods
    static uint8_t* copyToPSRAM(const uint8_t* src, size_t size);
    static void freePSRAM(uint8_t* ptr);
    static uint8_t* allocatePSRAM(size_t size);
    static void deallocatePSRAM(uint8_t* ptr);
    static uint8_t* reallocatePSRAM(uint8_t* ptr, size_t newSize);
};

} // namespace CatCam

#endif
