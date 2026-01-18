#ifndef CATCAM_HTTPCLIENT_H
#define CATCAM_HTTPCLIENT_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include "AWSAuth.h"
#include "NamedImage.h"

class CatCamHttpClient
{
public:
    CatCamHttpClient();

    // Post an image to the specified URL with SigV4 authentication
    String postImage(NamedImage* namedImage, const char* host, const char* path, AWSAuth* awsAuth);

private:
    static const int TIMEOUT_MS = 30000; // 30 second timeout
};

#endif
