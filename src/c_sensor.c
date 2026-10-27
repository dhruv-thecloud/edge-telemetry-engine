#include "c_sensor.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct Simulation {
    double nominal;
    double amplitude;
    double offset;
    uint32_t rng;
} Simulation;

typedef struct TemperatureSensor {
    Sensor base; /* First-member layout makes Sensor* downcasts valid in C. */
    Simulation simulation;
} TemperatureSensor;

typedef struct PressureSensor {
    Sensor base;
    Simulation simulation;
} PressureSensor;

_Static_assert(offsetof(TemperatureSensor, base) == 0, "base must be first");
_Static_assert(offsetof(PressureSensor, base) == 0, "base must be first");

/* Conservatively bound every intermediate sum, including after calibration. */
static int valid_range(double nominal, double amplitude, double offset)
{
    return isfinite(nominal) && isfinite(amplitude) && isfinite(offset) &&
           amplitude >= 0.0 &&
           fabs(nominal) <= DBL_MAX - amplitude &&
           fabs(offset) <= (DBL_MAX - amplitude) - fabs(nominal);
}

static SensorStatus simulation_read(Simulation *sim, double *value)
{
    /* Per-instance xorshift32: deterministic, inexpensive, not cryptographic. */
    uint32_t next = sim->rng;
    next ^= next << 13;
    next ^= next >> 17;
    next ^= next << 5;
    const double noise = 2.0 * ((double)next / (double)UINT32_MAX) - 1.0;
    const double result = (sim->nominal + sim->amplitude * noise) + sim->offset;
    if (!isfinite(result)) {
        return SENSOR_OUT_OF_RANGE;
    }
    sim->rng = next;
    *value = result;
    return SENSOR_OK;
}

static SensorStatus simulation_calibrate(Simulation *sim, double offset)
{
    if (!isfinite(offset)) {
        return SENSOR_INVALID_ARGUMENT;
    }
    if (!valid_range(sim->nominal, sim->amplitude, offset)) {
        return SENSOR_OUT_OF_RANGE;
    }
    sim->offset = offset;
    return SENSOR_OK;
}

/* Distinct overrides keep concrete type knowledge behind virtual dispatch.
 * The shared simulation helpers can later be replaced by device drivers. */
static SensorStatus temperature_read(Sensor *self, double *value)
{
    return simulation_read(&((TemperatureSensor *)self)->simulation, value);
}

static SensorStatus temperature_calibrate(Sensor *self, double offset)
{
    return simulation_calibrate(&((TemperatureSensor *)self)->simulation, offset);
}

static void temperature_destroy(Sensor *self)
{
    free((TemperatureSensor *)self);
}

static SensorStatus pressure_read(Sensor *self, double *value)
{
    return simulation_read(&((PressureSensor *)self)->simulation, value);
}

static SensorStatus pressure_calibrate(Sensor *self, double offset)
{
    return simulation_calibrate(&((PressureSensor *)self)->simulation, offset);
}

static void pressure_destroy(Sensor *self)
{
    free((PressureSensor *)self);
}

/* Immutable, shared dispatch tables live for the entire program. */
static const SensorVTable TEMPERATURE_VTABLE = {
    temperature_read, temperature_calibrate, temperature_destroy
};
static const SensorVTable PRESSURE_VTABLE = {
    pressure_read, pressure_calibrate, pressure_destroy
};

static SensorStatus validate_creation(double nominal, double amplitude,
                                      Sensor **out)
{
    if (out == NULL) {
        return SENSOR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!isfinite(nominal) || !isfinite(amplitude) || amplitude < 0.0) {
        return SENSOR_INVALID_ARGUMENT;
    }
    return valid_range(nominal, amplitude, 0.0)
               ? SENSOR_OK : SENSOR_OUT_OF_RANGE;
}

SensorStatus temperature_sensor_create(uint32_t id, double nominal,
                                       double noise_amplitude, uint32_t seed,
                                       Sensor **out)
{
    const SensorStatus status = validate_creation(nominal, noise_amplitude, out);
    if (status != SENSOR_OK) {
        return status;
    }
    TemperatureSensor *sensor = malloc(sizeof(*sensor));
    if (sensor == NULL) {
        return SENSOR_OUT_OF_MEMORY;
    }
    *sensor = (TemperatureSensor){
        {&TEMPERATURE_VTABLE, id, "degC"},
        {nominal, noise_amplitude, 0.0, seed ? seed : UINT32_C(0x9e3779b9)}
    };
    *out = &sensor->base;
    return SENSOR_OK;
}

SensorStatus pressure_sensor_create(uint32_t id, double nominal,
                                    double noise_amplitude, uint32_t seed,
                                    Sensor **out)
{
    const SensorStatus status = validate_creation(nominal, noise_amplitude, out);
    if (status != SENSOR_OK) {
        return status;
    }
    PressureSensor *sensor = malloc(sizeof(*sensor));
    if (sensor == NULL) {
        return SENSOR_OUT_OF_MEMORY;
    }
    *sensor = (PressureSensor){
        {&PRESSURE_VTABLE, id, "kPa"},
        {nominal, noise_amplitude, 0.0, seed ? seed : UINT32_C(0x9e3779b9)}
    };
    *out = &sensor->base;
    return SENSOR_OK;
}

SensorStatus sensor_read(Sensor *self, double *value)
{
    if (self == NULL || value == NULL || self->vtable == NULL ||
        self->vtable->read == NULL) {
        return SENSOR_INVALID_ARGUMENT;
    }
    return self->vtable->read(self, value);
}

SensorStatus sensor_calibrate(Sensor *self, double offset)
{
    if (self == NULL || !isfinite(offset) || self->vtable == NULL ||
        self->vtable->calibrate == NULL) {
        return SENSOR_INVALID_ARGUMENT;
    }
    return self->vtable->calibrate(self, offset);
}

void sensor_destroy(Sensor *self)
{
    if (self != NULL) {
        self->vtable->destroy(self);
    }
}
