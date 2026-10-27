#ifndef EDGE_CPP_AGGREGATOR_HPP
#define EDGE_CPP_AGGREGATOR_HPP
#include "c_sensor.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace edge {
struct SensorDeleter {
    void operator()(Sensor* sensor) const noexcept { sensor_destroy(sensor); }
};
// Sole ownership crosses the language boundary; delete must never free a C object.
using SensorPtr = std::unique_ptr<Sensor, SensorDeleter>;

template<std::size_t N> class RollingWindow {
    static_assert(N > 0);
public:
    void push(double value) noexcept {
        values_[next_] = value;
        next_ = (next_ + 1) % N;
        if (size_ < N) ++size_;
    }
    void clear() noexcept { size_ = next_ = 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] double mean() const noexcept {
        // Divide before summing to avoid overflowing for large finite inputs.
        long double result = 0;
        for (std::size_t i = 0; i < size_; ++i)
            result += static_cast<long double>(values_[i]) / size_;
        return static_cast<double>(result);
    }
    [[nodiscard]] double deviation() const noexcept {
        if (size_ < 2) return 0;
        const long double center = mean();
        long double norm = 0;
        // hypot avoids squaring large deviations directly.
        for (std::size_t i = 0; i < size_; ++i)
            norm = std::hypot(norm, static_cast<long double>(values_[i]) - center);
        return static_cast<double>(norm / std::sqrt(static_cast<long double>(size_)));
    }
private:
    std::array<double, N> values_{};
    std::size_t size_ = 0, next_ = 0;
};

struct Telemetry {
    uint32_t id;
    const char* unit; // Borrowed from the owned sensor; valid during its lifetime.
    double value;
    double average;
    bool accepted;
    std::size_t window_count;
};
class Aggregator {
public:
    explicit Aggregator(double sigma_multiplier = 4.0);
    void add(SensorPtr sensor, double absolute_threshold);
    [[nodiscard]] std::vector<Telemetry> poll();
    void calibrate(uint32_t id, double offset);
private:
    struct Entry {
        SensorPtr sensor;
        double threshold;
        RollingWindow<32> history;
    };
    std::vector<Entry> entries_;
    double sigma_multiplier_;
};
} // namespace edge
#endif
