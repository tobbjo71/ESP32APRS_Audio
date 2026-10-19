/*
 * SM0TGY fork: stubs used when FORK_SLIM is defined.
 * sensor.cpp, weather.cpp and wireguard_vpn.cpp are left out of the build
 * (build_src_filter). The rest of the firmware still references a few of their
 * symbols; these no-op versions keep the features permanently inactive.
 */
#ifdef FORK_SLIM
#include <Arduino.h>
#include "config.h"
#include "sensor.h"
#include "weather.h"
#include "wireguard_vpn.h"

SensorData sen[SENSOR_NUMBER];
float mslAltitude = 0;

void taskSensor(void *pvParameters) { vTaskDelete(NULL); }
void sensorInit(bool resetAll) {}
void cleanupSensors() {}
int getRawWx(char *strData)
{
    if (strData)
        strData[0] = 0;
    return 0;
}

bool wireguard_active() { return false; }
void wireguard_setup() {}
void wireguard_change_device() {}
#endif
