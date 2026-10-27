#include "cpp_aggregator.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#define CHECK(expr) do { if (!(expr)) { std::cerr << "failed: " #expr << '\n'; std::exit(1); } } while (false)
namespace {
int destroyed = 0;
struct ScriptedSensor { Sensor base; int index = 0; double offset = 0; };
SensorStatus read_sensor(Sensor* base, double* value) {
    auto* self = reinterpret_cast<ScriptedSensor*>(base);
    *value = (self->index++ == 8 ? 1000.0 : 10.0) + self->offset;
    return SENSOR_OK;
}
SensorStatus calibrate_sensor(Sensor* base, double value) {
    reinterpret_cast<ScriptedSensor*>(base)->offset = value;
    return SENSOR_OK;
}
void destroy_sensor(Sensor* base) {
    ++destroyed;
    delete reinterpret_cast<ScriptedSensor*>(base);
}
const SensorVTable table{read_sensor, calibrate_sensor, destroy_sensor};
edge::SensorPtr make_sensor() {
    auto* sensor = new ScriptedSensor{{&table, 1, "test"}};
    return edge::SensorPtr(&sensor->base);
}
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}
}
int main() {
    edge::RollingWindow<3> window;
    CHECK(window.size() == 0 && window.mean() == 0);
    window.push(1); window.push(2); window.push(3); window.push(4);
    CHECK(window.size() == 3 && window.mean() == 3);
    CHECK(std::abs(window.deviation() - std::sqrt(2.0/3)) < 1e-12);
    window.clear(); CHECK(window.size() == 0);
    rejects([] { edge::Aggregator invalid(0); });
    {
        edge::Aggregator aggregator;
        aggregator.add(make_sensor(), 1);
        rejects([&] { aggregator.add(make_sensor(), 1); });
        CHECK(destroyed == 1); // Failed ownership transfer still frees its input.
        rejects([&] { aggregator.calibrate(999, 0); });
        for (int i = 0; i < 8; ++i) CHECK(aggregator.poll().at(0).accepted);
        auto sample = aggregator.poll().at(0);
        CHECK(!sample.accepted && sample.average == 10 && sample.window_count == 8);
        CHECK(aggregator.poll().at(0).accepted);
        aggregator.calibrate(1, 100);
        sample = aggregator.poll().at(0);
        CHECK(sample.accepted && sample.average == 110 && sample.window_count == 1);
        for (int i = 0; i < 40; ++i) sample = aggregator.poll().at(0);
        CHECK(sample.window_count == 32);
    }
    CHECK(destroyed == 2);
}
