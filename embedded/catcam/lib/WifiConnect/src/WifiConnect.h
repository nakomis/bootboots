#ifndef MUSHROOM_WIFI_CONNECT_H
#define MUSHROOM_WIFI_CONNECT_H

class WifiConnect
{
public:
    WifiConnect();
    // Credentials come from the prov partition (Provisioning), never from the image.
    int connect(const char* ssid, const char* password);
};

#endif