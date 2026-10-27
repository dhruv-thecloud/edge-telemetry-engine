#ifndef EDGE_C_SENSOR_H
#define EDGE_C_SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sensor Sensor;

typedef enum SensorStatus {
    SENSOR_OK = 0,
    SENSOR_INVALID_ARGUMENT,
    SENSOR_OUT_OF_MEMORY,
    SENSOR_OUT_OF_RANGE
} SensorStatus;

/* All implementations must supply all three callbacks. read() leaves *value
 * unchanged on failure; calibrate() leaves calibration unchanged on failure.
 * destroy() releases the entire concrete object and its owned resources. */
typedef struct SensorVTable {
    SensorStatus (*read)(Sensor *self, double *value);
    SensorStatus (*calibrate)(Sensor *self, double offset);
    void (*destroy)(Sensor *self);
} SensorVTable;

/* Abstract base: never instantiate, copy, free, or modify this struct directly.
 * Concrete types embed it as their FIRST member and install a static const
 * vtable. Metadata and the vtable remain valid for the object's lifetime.
 * Independent instances can run concurrently; callers must serialize access
 * to a shared instance, including reads, calibration, and destruction. */
struct Sensor {
    const SensorVTable *vtable;
    uint32_t id;
    const char *unit;
};

/* Constructors transfer sole ownership through *out on success; on failure
 * they set *out to NULL (when out is non-NULL). Pass an empty output slot to
 * avoid losing an existing owner. nominal and noise_amplitude must be finite;
 * amplitude must be nonnegative. seed=0 selects a fixed nonzero default.
 * Samples are nominal + uniform noise in [-amplitude, amplitude] + offset.
 * Temperature uses degrees Celsius; pressure uses kPa. Simulation deliberately
 * does not clamp physical ranges, so anomaly filtering belongs to the caller. */
SensorStatus temperature_sensor_create(uint32_t id, double nominal,
                                       double noise_amplitude, uint32_t seed,
                                       Sensor **out);
SensorStatus pressure_sensor_create(uint32_t id, double nominal,
                                    double noise_amplitude, uint32_t seed,
                                    Sensor **out);

SensorStatus sensor_read(Sensor *self, double *value);

/* Set (do not accumulate) an absolute additive offset in the sensor's units.
 * This is a simulated correction, not hardware reference-point calibration. */
SensorStatus sensor_calibrate(Sensor *self, double offset);

/* NULL is safe. Other pointers must be live owned sensors; after destruction
 * all aliases are invalid. C++ may use this as a unique_ptr custom deleter. */
void sensor_destroy(Sensor *self);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_C_SENSOR_H */
