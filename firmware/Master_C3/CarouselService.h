#ifndef YUELI_CAROUSEL_SERVICE_H
#define YUELI_CAROUSEL_SERVICE_H
#include <ESPAsyncWebServer.h>
void beginCarouselService();
void registerCarouselRoutes(AsyncWebServer& server);
void loadCarouselData();
void restartCarouselPlayback();
void lockCarousel();
void unlockCarousel();
struct CarouselBody { size_t received; size_t total; bool invalid; char text[1]; };
#endif
