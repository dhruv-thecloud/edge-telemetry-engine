#include "c_sensor.h"
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#define CHECK(expr) do { if (!(expr)) { std::cerr << "failed: " #expr << '\n'; std::exit(1); } } while (false)
using Factory = SensorStatus (*)(uint32_t, double, double, uint32_t, Sensor**);
void check(Factory create, const char* unit) {
    Sensor *a = nullptr, *b = nullptr;
    double x = 0, y = 0;
    CHECK(create(7, 25, 0, 0, &a) == SENSOR_OK);
    CHECK(a->id == 7 && std::strcmp(a->unit, unit) == 0);
    CHECK(sensor_read(a, &x) == SENSOR_OK && x == 25);
    CHECK(sensor_calibrate(a, 3) == SENSOR_OK);
    CHECK(sensor_calibrate(a, 3) == SENSOR_OK);
    CHECK(sensor_read(a, &x) == SENSOR_OK && x == 28);
    CHECK(sensor_calibrate(a, NAN) == SENSOR_INVALID_ARGUMENT);
    CHECK(sensor_read(a, &x) == SENSOR_OK && x == 28);
    CHECK(sensor_read(a, nullptr) == SENSOR_INVALID_ARGUMENT);
    sensor_destroy(a);
    CHECK(create(1, 100, 2, 42, &a) == SENSOR_OK);
    CHECK(create(2, 100, 2, 42, &b) == SENSOR_OK);
    for (int i = 0; i < 10000; ++i) {
        CHECK(sensor_read(a, &x) == SENSOR_OK);
        CHECK(sensor_read(b, &y) == SENSOR_OK);
        CHECK(x == y && x >= 98 && x <= 102);
    }
    sensor_destroy(a); sensor_destroy(b); a = nullptr;
    CHECK(create(0, NAN, 1, 1, &a) == SENSOR_INVALID_ARGUMENT && !a);
    CHECK(create(0, 1, -1, 1, &a) == SENSOR_INVALID_ARGUMENT && !a);
    CHECK(create(0, 1, INFINITY, 1, &a) == SENSOR_INVALID_ARGUMENT);
    CHECK(create(0, DBL_MAX, DBL_MAX, 1, &a) == SENSOR_OUT_OF_RANGE);
    CHECK(create(0, 1, 1, 1, nullptr) == SENSOR_INVALID_ARGUMENT);
    CHECK(create(0, DBL_MAX / 2, 0, 1, &a) == SENSOR_OK);
    CHECK(sensor_calibrate(a, DBL_MAX) == SENSOR_OUT_OF_RANGE);
    CHECK(sensor_read(a, &x) == SENSOR_OK && x == DBL_MAX / 2);
    sensor_destroy(a);
}
int main() {
    double value = 123;
    CHECK(sensor_read(nullptr, &value) == SENSOR_INVALID_ARGUMENT && value == 123);
    CHECK(sensor_calibrate(nullptr, 0) == SENSOR_INVALID_ARGUMENT);
    sensor_destroy(nullptr);
    check(temperature_sensor_create, "degC");
    check(pressure_sensor_create, "kPa");
}
