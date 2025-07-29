#ifndef CATCAM_HTTPCLIENT_H
#define CATCAM_HTTPCLIENT_H

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <NamedImage.h>
#include <String.h>

namespace CatCam {

class HttpClient
{
public:
    HttpClient();
    String postImage(NamedImage* namedImage, const char* url, const char* apiKey);

private:
    static const int TIMEOUT_MS = 30000; // 30 second timeout
};

} // namespace CatCam

#endif
