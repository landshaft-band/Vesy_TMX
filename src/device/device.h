#pragma once
#include <GyverHX711.h>
extern GyverHX711 sensor;
void deviceSetup();
void deviceLoop();
float getFilteredWeight();
bool isEmpty();
// НОВАЯ функция: true, если вес в данный момент стабилен.
bool isStable();