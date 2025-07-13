#ifndef CATCAM_MESSAGEQUEUE_H
#define CATCAM_MESSAGEQUEUE_H

extern const char *MQTT_SERVER;
extern const String TOPIC;

class MessageQueue
{
public:
    MessageQueue();
    int postImage(const String& filename, const uint8_t* message, size_t size);

    private:
};


#endif