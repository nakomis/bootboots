#ifndef CATCAM_MESSAGEQUEUE_H
#define CATCAM_MESSAGEQUEUE_H

#include <NamedImage.h>

extern const char *MQTT_SERVER;
extern const String TOPIC;

class MessageQueue
{
public:
    MessageQueue();
    int postImage(NamedImage* namedImage);

    private:
};


#endif