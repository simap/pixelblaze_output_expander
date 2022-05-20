
#include "SPI.h"

enum ChannelType {
    CHANNEL_WS2812 = 1, CHANNEL_DRAW_ALL, CHANNEL_APA102_DATA, CHANNEL_APA102_CLOCK
};

typedef struct {
    int8_t magic[4];
    uint8_t channel;
    uint8_t recordType; //set channel ws2812 opts+data, draw all
} PBFrameHeader;

typedef struct {
    uint8_t numElements; //0 to disable channel, usually 3 (RGB) or 4 (RGBW)
    union {
        struct {
            uint8_t redi :2, greeni :2, bluei :2, whitei :2; //color orders, data on the line assumed to be RGB or RGBW
        };
        uint8_t colorOrders;
    };
    uint16_t pixels;
} PBWS2812Channel;

typedef struct {
    uint32_t frequency;
    union {
        struct {
            uint8_t redi :2, greeni :2, bluei :2; //color orders, data on the line assumed to be RGB
        };
        uint8_t colorOrders;
    };
    uint16_t pixels;
} PBAPA102DataChannel;

typedef struct {
    uint32_t frequency;
} PBAPA102ClockChannel;



#define NUM_PIXELS 100

uint8_t pixels[NUM_PIXELS * 4];

PBWS2812Channel pbws2812Channel;
PBFrameHeader frameHeader;

void write(uint8_t * data, int size) {
  while (size--) {
    SPI.write(*data++);
  }
}


void setup() {
  SPI.begin();
  SPI.setFrequency(1000000);
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  
}

void drawPixels() {
  memcpy(frameHeader.magic, "UPXL", 4);

  frameHeader.channel = 0;
  frameHeader.recordType = CHANNEL_WS2812;
  write((uint8_t *) &frameHeader, sizeof(frameHeader));


  pbws2812Channel.numElements = 4; //RGBW
  pbws2812Channel.pixels = NUM_PIXELS;
  pbws2812Channel.redi = 0;
  pbws2812Channel.greeni = 1;
  pbws2812Channel.bluei = 2;
  pbws2812Channel.whitei = 3;
  write((uint8_t *) &pbws2812Channel, sizeof(pbws2812Channel));

  write(pixels, sizeof(pixels));


  frameHeader.channel = 0xff;
  frameHeader.recordType = CHANNEL_DRAW_ALL;
  write((uint8_t *) &frameHeader, sizeof(frameHeader));
  
}


void loop() {

  for (int i = 0; i < NUM_PIXELS; i++) {
    memset(pixels, 1, sizeof(pixels));
    pixels[i * 4 + 3] = 255; //light up the white element for a pixel

    drawPixels();

    delay(11); //wait at least 10ms before sending more data
  }

}
