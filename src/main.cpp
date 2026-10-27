#include "cpp_aggregator.hpp"
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void json_string(std::ostream& out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}
// Bound memory even when the command producer sends an arbitrarily long line.
bool read_line(std::string& line, bool& oversized) {
    line.clear(); oversized = false;
    char c;
    bool any = false;
    while (std::cin.get(c)) {
        any = true;
        if (c == '\n') return true;
        if (line.size() < 4096) line += c;
        else oversized = true;
    }
    return any;
}
void require_end(std::istringstream& input) {
    std::string extra;
    if (input >> extra) throw std::invalid_argument("unexpected command arguments");
}
uint32_t parse_id(const std::string& text) {
    uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid sensor id");
    return value;
}
}
int main() {
    std::locale::global(std::locale::classic());
    std::cout << std::setprecision(17);
    try {
        edge::Aggregator aggregator;
        Sensor* raw = nullptr;
        if (temperature_sensor_create(1, 25.0, 0.5, 42, &raw) != SENSOR_OK)
            throw std::runtime_error("temperature sensor allocation failed");
        aggregator.add(edge::SensorPtr(raw), 2.0);
        if (pressure_sensor_create(2, 101.325, 0.2, 77, &raw) != SENSOR_OK)
            throw std::runtime_error("pressure sensor allocation failed");
        aggregator.add(edge::SensorPtr(raw), 1.0);
        const auto start = std::chrono::steady_clock::now();
        uint64_t sequence = 0;
        std::string line;
        bool oversized;
        while (read_line(line, oversized)) {
            try {
                if (oversized) throw std::invalid_argument("command exceeds 4096 bytes");
                std::istringstream input(line);
                std::string command;
                input >> command;
                if (command == "poll") {
                    require_end(input);
                    const auto samples = aggregator.poll();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
                    std::cout << "{\"type\":\"telemetry\",\"sequence\":" << sequence++
                              << ",\"elapsed_ms\":" << elapsed << ",\"samples\":[";
                    bool first = true;
                    for (const auto& sample : samples) {
                        if (!first) std::cout << ',';
                        first = false;
                        std::cout << "{\"id\":" << sample.id << ",\"unit\":";
                        json_string(std::cout, sample.unit);
                        std::cout << ",\"value\":" << sample.value
                                  << ",\"average\":" << sample.average
                                  << ",\"accepted\":" << (sample.accepted ? "true" : "false")
                                  << ",\"window_count\":" << sample.window_count << '}';
                    }
                    std::cout << "]}\n";
                } else if (command == "calibrate") {
                    std::string id_text;
                    double offset;
                    if (!(input >> id_text >> offset))
                        throw std::invalid_argument("usage: calibrate ID OFFSET");
                    require_end(input);
                    const auto id = parse_id(id_text);
                    aggregator.calibrate(id, offset);
                    std::cout << "{\"type\":\"calibrated\",\"id\":" << id << "}\n";
                } else if (command == "quit") {
                    require_end(input);
                    std::cout << "{\"type\":\"bye\"}\n" << std::flush;
                    return 0;
                } else {
                    throw std::invalid_argument("expected poll, calibrate ID OFFSET, or quit");
                }
            } catch (const std::exception& error) {
                std::cout << "{\"type\":\"error\",\"message\":";
                json_string(std::cout, error.what());
                std::cout << "}\n";
            }
            std::cout.flush();
            if (!std::cout) return 1;
        }
        return std::cin.bad() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
