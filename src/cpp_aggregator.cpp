#include "cpp_aggregator.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace edge {
Aggregator::Aggregator(double sigma_multiplier) : sigma_multiplier_(sigma_multiplier) {
    if (!std::isfinite(sigma_multiplier) || sigma_multiplier <= 0)
        throw std::invalid_argument("sigma multiplier must be positive and finite");
}
void Aggregator::add(SensorPtr sensor, double absolute_threshold) {
    if (!sensor || !sensor->vtable || !sensor->vtable->read ||
        !sensor->vtable->calibrate || !sensor->vtable->destroy || !sensor->unit ||
        !std::isfinite(absolute_threshold) || absolute_threshold <= 0)
        throw std::invalid_argument("invalid sensor or anomaly threshold");
    for (const auto& entry : entries_)
        if (entry.sensor->id == sensor->id)
            throw std::invalid_argument("duplicate sensor id");
    entries_.push_back({std::move(sensor), absolute_threshold, {}});
}
std::vector<Telemetry> Aggregator::poll() {
    std::vector<Telemetry> result;
    result.reserve(entries_.size());
    for (auto& entry : entries_) {
        double value = 0;
        if (sensor_read(entry.sensor.get(), &value) != SENSOR_OK || !std::isfinite(value))
            throw std::runtime_error("sensor read failed: " + std::to_string(entry.sensor->id));
        const long double limit = std::max(static_cast<long double>(entry.threshold),
            static_cast<long double>(sigma_multiplier_) * entry.history.deviation());
        const bool accepted = entry.history.size() < 8 ||
            std::abs(static_cast<long double>(value) - entry.history.mean()) <= limit;
        if (accepted) entry.history.push(value);
        result.push_back({entry.sensor->id, entry.sensor->unit, value,
                          entry.history.mean(), accepted, entry.history.size()});
    }
    return result;
}
void Aggregator::calibrate(uint32_t id, double offset) {
    for (auto& entry : entries_) {
        if (entry.sensor->id != id) continue;
        if (sensor_calibrate(entry.sensor.get(), offset) != SENSOR_OK)
            throw std::invalid_argument("invalid calibration offset");
        // A new calibration defines a new baseline: discard old statistics.
        entry.history.clear();
        return;
    }
    throw std::invalid_argument("unknown sensor id");
}
} // namespace edge
