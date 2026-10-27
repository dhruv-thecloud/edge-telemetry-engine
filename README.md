# Hybrid-Paradigm Edge IoT Sensor Simulation & Telemetry Engine

A runnable C11 / C++20 / Python 3.10+ telemetry simulator. C implements manual
virtual dispatch, C++ owns sensors and filters samples, and Python runs a live
terminal dashboard with calibration and optional JSONL logging. No third-party
Python packages are required.

## Report and captured results

- [14-page illustrated technical report (PDF)](docs/REPORT.pdf)
- [Technical report source](REPORT.md)
- [Recorded telemetry and screenshots](docs/evidence/)

The screenshots show actual local results rendered in a static evidence viewer.
This project provides a terminal dashboard; no hosted live web dashboard is claimed.

Repository: https://github.com/dhruv-thecloud/edge-telemetry-engine

## Build and run

Requirements: CMake 3.20+, a C11/C++20 compiler (Clang or GCC recommended), a build
tool such as Make or Ninja, and Python 3.10+ when building tests or the dashboard.
Run these commands from this directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 python/dashboard.py --engine build/edge_telemetry
```

The dashboard prints sensor readings, rolling averages, anomaly flags, and a
40-reading ASCII sparkline. Each sensor's sparkline scales independently to its
recent minimum and maximum; it is a trend indicator, not an absolute scale.
On a terminal, enter `calibrate 1 2.5` to set the temperature correction to +2.5 °C,
or `quit` to stop. Ctrl-C also cleans up the subprocess. Terminal output and
entered commands can interleave. Use `--no-interactive` for automated runs.

```sh
python3 python/dashboard.py --engine build/edge_telemetry \
  --interval 0.1 --count 100 --calibrate 1 2.5 \
  --log telemetry.jsonl --no-interactive
```

`--count 0` runs indefinitely. `--log` appends one telemetry frame per line and
flushes each frame; its parent directory must exist. Logging is not rotated.
For multi-configuration generators, build with `--config Release`, test with
`-C Release`, and use the executable under `build/Release/`. Windows executable
names include `.exe`; Windows builds are not part of the supplied CI matrix.

## Files

```text
edge-telemetry-engine/
├── .github/workflows/ci.yml
├── .gitignore
├── include/
│   ├── c_sensor.h
│   └── cpp_aggregator.hpp
├── src/
│   ├── c_sensor.c
│   ├── cpp_aggregator.cpp
│   └── main.cpp
├── python/
│   ├── dashboard.py
│   └── simulator_client.py
├── tests/
│   ├── test_c_objects.cpp
│   ├── test_cpp_core.cpp
│   └── test_protocol.py
├── CMakeLists.txt
├── REPORT.md
├── README.md
└── setup_and_push.sh
```

## Protocol

The executable reads ASCII commands from stdin and emits exactly one UTF-8 JSON
object per command on stdout, flushed immediately. Startup errors go to stderr.
It does not emit unsolicited frames: polling cadence belongs to the controller.
Only one request may be outstanding in the Python client.

| Command | Response | Effect |
|---|---|---|
| `poll` | `telemetry` | Read every registered sensor once |
| `calibrate ID OFFSET` | `calibrated` | Replace additive offset; clear that sensor's statistics |
| `quit` | `bye` | Destroy sensors and exit successfully |
| Invalid input | `error` | Reject the command and continue |

IDs are unsigned 32-bit decimal integers. Offsets are finite decimal numbers in
the sensor's units. Commands are limited to 4096 bytes excluding the newline;
oversized lines are drained and rejected. EOF shuts down cleanly; a final
unterminated command is processed. Calibration and syntax errors do not terminate
the engine. A fatal initialization error returns exit code 1.

```sh
printf 'poll\ncalibrate 1 2.5\npoll\nquit\n' | build/edge_telemetry
```

Illustrative telemetry (actual values depend on deterministic PRNG state):

```json
{"type":"telemetry","sequence":0,"elapsed_ms":0,"samples":[{"id":1,"unit":"degC","value":25.1,"average":25.1,"accepted":true,"window_count":1},{"id":2,"unit":"kPa","value":101.3,"average":101.3,"accepted":true,"window_count":1}]}
```

`sequence` starts at zero and increments for successful polls. `elapsed_ms` is
monotonic time since engine initialization, not a UTC timestamp. `accepted=false`
means the raw reading is included for inspection but excluded from the average.
A calibration acknowledgement is `{"type":"calibrated","id":1}`. Errors include
a human-readable `message`. Protocol schema changes should be versioned before
independent clients are deployed.

## Sensor and filter defaults

| ID | Sensor | Nominal | Noise amplitude | Seed | Absolute anomaly floor |
|---|---|---|---|---|---|
| 1 | Temperature | 25 °C | ±0.5 °C | 42 | 2 °C |
| 2 | Pressure | 101.325 kPa | ±0.2 kPa | 77 | 1 kPa |

Each instance has an independent xorshift32 state. Seed zero maps to a documented
nonzero seed. This is reproducible simulation noise, not a physical sensor model
or a cryptographically secure generator. Constructors reject non-finite inputs,
negative noise amplitudes, and unsafe numeric ranges. Physical ranges are not
clamped, allowing downstream anomaly experiments.

The first eight accepted samples establish a baseline. Thereafter a reading is
accepted when its absolute distance from the prior mean is at most
`max(absolute_floor, 4 * population_standard_deviation)`. The window retains the
last 32 accepted readings. Rejected samples do not contaminate the baseline.
Calibration resets the history; its first following sample begins a fresh window.
The default bounded noise normally produces no anomalies; the scripted test
sensor injects a spike to demonstrate rejection. A sustained environmental step
can keep being rejected until recalibration resets the baseline.

## C and C++ APIs

`c_sensor.h` exposes the base object, complete vtable contract, two constructors,
and dispatch functions. Callers must not instantiate or mutate the base directly.
An extension embeds `Sensor` first, installs a static vtable, and provides all
callbacks. `sensor_destroy(NULL)` is safe; double destruction is not.

```cpp
Sensor* raw = nullptr;
if (temperature_sensor_create(3, 20.0, 0.2, 123, &raw) != SENSOR_OK)
    throw std::runtime_error("sensor creation failed");
edge::Aggregator manager;
manager.add(edge::SensorPtr(raw), 1.0);
auto frame = manager.poll();
manager.calibrate(3, -0.5);
```

`SensorPtr` is a `std::unique_ptr<Sensor, SensorDeleter>` whose deleter calls the C
virtual destructor. No shared ownership is needed. `add()` consumes ownership
on entry, including when validation or allocation throws. Sensor IDs must be
unique. `Telemetry::unit` borrows the sensor's unit string and must not outlive
its owner. Calls to one aggregator or sensor must be serialized by the caller.

## Validation and CI

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DEDGE_SANITIZERS=ON -DEDGE_WARNINGS_AS_ERRORS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

Tests remain active in Release builds. They cover C polymorphism and numeric
validation, seed repeatability, ownership on rejection and destruction, rolling
window eviction, injected spikes, calibration resets, command recovery, EOF,
command length limits, and Python dashboard logs. CI runs tests with address and
undefined-behavior sanitizers on Ubuntu and macOS. Do not enable `-ffast-math`:
finite-input validation depends on normal floating-point semantics.

No claim of hard real-time behavior or benchmarked throughput is made. See
[REPORT.md](REPORT.md) for performance costs and deployment limitations.

## Push to GitHub

Create an empty GitHub repository first and configure Git authentication and
`user.name` / `user.email`. Inspect the project files, then run:

```sh
chmod +x setup_and_push.sh
./setup_and_push.sh https://github.com/YOUR_USERNAME/edge-telemetry-engine.git
```

The script stages the listed project files, commits changes, configures `origin`,
and pushes `main`. It checks the branch and existing remote instead of silently
rewriting them, and never force-pushes. It refuses initialization inside a parent
repository and refuses pre-existing staged changes. An existing remote with
unrelated history may reject the push; resolve that history explicitly. A failed
push retains the local commit for retry. The script does not create a GitHub
repository or install credentials.
