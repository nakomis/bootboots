#ifndef CATCAM_NAMEDIMAGE_H
#define CATCAM_NAMEDIMAGE_H

#include <Arduino.h>

class NamedImage
{
public:
    String filename;
    uint8_t* image;
    size_t size;
};

#endif