#include "crow.h"
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <vector>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <cinttypes>
#include <algorithm>
#include <mutex>
#include <sys/statvfs.h>

namespace fs = std::filesystem;

// ── Data structs ───────────────────────────────────────

struct GPUMetric {
    int id;
    std::string name;
    float temperature;
    float utilization;
    float mem_used;
    float mem_total;
};

struct MemMetric {
    float total_gb;
    float used_gb;
    float available_gb;
    float pct;
};

// ── Helpers ─────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static std::string timestamp_now() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
        1900 + tm_buf.tm_year, 1 + tm_buf.tm_mon, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

// ── GPU via nvidia-smi ──────────────────────────────

std::vector<GPUMetric> get_gpu_metrics() {
    std::vector<GPUMetric> gpus;
    const char* cmd =
        "nvidia-smi --query-gpu=index,name,temperature.gpu,utilization.gpu,memory.used,memory.total "
        "--format=csv,noheader,nounits 2>/dev/null";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return gpus;

    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) {
        GPUMetric g{};
        std::vector<std::string> parts;
        std::stringstream ss(buf);
        std::string token;
        while (std::getline(ss, token, ',')) {
            parts.push_back(trim(token));
        }
        if (parts.size() >= 6) {
            try {
                g.id = std::stoi(parts[0]);
                g.name = parts[1];
                g.temperature = std::stof(parts[2]);
                g.utilization = std::stof(parts[3]);
                g.mem_used = std::stof(parts[4]);
                g.mem_total = std::stof(parts[5]);
                gpus.push_back(g);
            } catch (...) {}
        }
    }
    pclose(pipe);
    return gpus;
}

// ── CPU usage via /proc/stat ────────────────────────

float get_cpu_usage(int64_t& prev_total, int64_t& prev_idle) {
    std::ifstream f("/proc/stat");
    std::string line;
    std::getline(f, line);
    std::stringstream ss(line);
    ss >> line; // skip "cpu"
    uint64_t user = 0, nice = 0, system = 0, idle = 0;
    uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;
    ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    int64_t total = (int64_t)(user + nice + system + idle + iowait + irq + softirq + steal);
    int64_t idle_t = (int64_t)(idle + iowait);
    int64_t d_total = total - prev_total;
    int64_t d_idle = idle_t - prev_idle;
    prev_total = total;
    prev_idle = idle_t;

    if (d_total == 0) return 0.0f;
    return 100.0f * (1.0f - (float)d_idle / (float)d_total);
}

// ── CPU temperature via sensors ─────────────────────

float get_cpu_temperature() {
    FILE* pipe = popen(
        "sensors 2>/dev/null | grep 'Tctl' | head -1 | grep -oP '[+-]?[0-9]+\\.[0-9]+'", "r");
    if (!pipe) return 0.0f;
    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), pipe)) {
        pclose(pipe);
        try { return std::stof(buf); }
        catch (...) {}
    }
    pclose(pipe);
    return 0.0f;
}

// ── Memory via /proc/meminfo ───────────────────────

MemMetric get_mem_metrics() {
    MemMetric m{0, 0, 0, 0};
    std::ifstream f("/proc/meminfo");
    std::string line;
    uint64_t total = 0, available = 0;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string key;
        ss >> key;
        if (key == "MemTotal:") ss >> total;
        else if (key == "MemAvailable:") ss >> available;
    }
    m.total_gb = (float)total / (1024.0 * 1024.0);
    m.available_gb = (float)available / (1024.0 * 1024.0);
    m.used_gb = m.total_gb - m.available_gb;
    m.pct = m.total_gb > 0 ? 100.0f * m.used_gb / m.total_gb : 0.0f;
    return m;
}

// ── Disk usage ─────────────────────────────────────────────

float get_disk_usage() {
    struct statvfs st;
    if (statvfs("/", &st) == 0) {
        uint64_t total = (uint64_t)st.f_blocks * st.f_frsize;
        uint64_t free = (uint64_t)st.f_bavail * st.f_frsize;
        return total > 0 ? 100.0f * (1.0f - (float)free / (float)total) : 0.0f;
    }
    return 0.0f;
}

// ── Uptime ───────────────────────────────────────────────

float get_uptime_hours() {
    std::ifstream f("/proc/uptime");
    float u = 0;
    f >> u;
    return u / 3600.0f;
}

// ── Logging ─────────────────────────────────────────────

static const std::string LOG_DIR = "/home/gasparilla/galley-temps/data";
static const std::string LOG_FILE = LOG_DIR + "/temps.csv";

void append_csv(const std::string& line) {
    std::ofstream f(LOG_FILE, std::ios::app);
    f << line << "\n";
}

// ── Retention: cap the log at MAX_RETENTION_SECS of history ───────────────
// The dashboard and the GH Pages mirror both fetch the whole CSV on every
// 60s refresh. Once the file had grown to ~330k rows / ~20MB, that download
// was the bottleneck behind the "not posting live data" complaint. Truncate
// to the newest ~7 days, keep a valid header, keep exactly one trailing
// newline, and replace the file atomically so the logger never appends to a
// half-written file. Only rows with a parseable ISO-8601 timestamp are
// compared; anything else (junk, the header, unparseable lines) is dropped.

static const int64_t MAX_RETENTION_SECS = 7 * 24 * 3600;
static std::mutex g_truncate_mu;

static int64_t parse_ts_leading(const std::string& s) {
    int Y = 0, M = 0, D = 0, h = 0, m = 0, ss = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &ss) < 6)
        return -1;
    struct tm t{};
    t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
    t.tm_hour = h; t.tm_min = m; t.tm_sec = ss; t.tm_isdst = -1;
    time_t v = mktime(&t);
    return (v < 0) ? -1 : (int64_t)v;
}

static bool truncate_retention_locked() {
    std::lock_guard<std::mutex> lock(g_truncate_mu);
    std::ifstream in(LOG_FILE, std::ios::binary);
    if (!in.is_open()) return false;

    std::string out_path = LOG_FILE + ".tmp";
    std::ofstream out(out_path, std::ios::binary);
    if (!out.is_open()) return false;

    int64_t now = (int64_t)time(nullptr);
    int64_t cutoff = now - MAX_RETENTION_SECS;

    std::string line;
    bool keep_header = true;
    int kept_rows = 0;
    bool ever_written = false;

    while (std::getline(in, line)) {
        if (keep_header && line.rfind("timestamp,", 0) == 0) {
            out << line << "\n";
            keep_header = false;
            ever_written = true;
            continue;
        }
        int64_t ts = parse_ts_leading(line);
        if (ts >= cutoff) {
            if (keep_header) { out << "timestamp,cpu_usage,cpu_temp,mem_pct,disk_pct,uptime_h,gpu0_util,gpu0_temp,gpu1_util,gpu1_temp\n"; keep_header = false; }
            out << line << "\n";
            kept_rows++;
            ever_written = true;
        }
    }
    if (!ever_written) {
        out << "timestamp,cpu_usage,cpu_temp,mem_pct,disk_pct,uptime_h,gpu0_util,gpu0_temp,gpu1_util,gpu1_temp\n";
    }
    out.flush();
    in.close();
    if (out.fail()) { out.close(); fs::remove(out_path); return false; }
    out.close();
    int rc = ::rename(out_path.c_str(), LOG_FILE.c_str());
    if (rc != 0) { fs::remove(out_path); return false; }
    return true;
}

// ── Global state ────────────────────────────────────────────────
static int64_t g_prev_total = 0;
static int64_t g_prev_idle = 0;

int main() {
    fs::create_directories(LOG_DIR);

    // Write CSV header if file doesn't exist yet
    if (!fs::exists(LOG_FILE)) {
        append_csv("timestamp,cpu_usage,cpu_temp,mem_pct,disk_pct,uptime_h,gpu0_util,gpu0_temp,gpu1_util,gpu1_temp");
    }

    // Background metrics collector — every 30 seconds
    std::thread logger([]() {
        while (true) {
            try {
                auto ts = timestamp_now();
                auto gpus = get_gpu_metrics();
                float cpu = get_cpu_usage(g_prev_total, g_prev_idle);
                float cpu_temp = get_cpu_temperature();
                auto mem = get_mem_metrics();
                float disk = get_disk_usage();
                float uptime = get_uptime_hours();

                // Collect GPU values with defaults
                float g0u = 0, g0t = 0, g1u = 0, g1t = 0;
                for (auto& g : gpus) {
                    if (g.id == 0) { g0u = g.utilization; g0t = g.temperature; }
                    if (g.id == 1) { g1u = g.utilization; g1t = g.temperature; }
                }

                std::ostringstream row;
                row << ts
                    << "," << std::fixed << std::setprecision(1) << cpu
                    << "," << cpu_temp
                    << "," << mem.pct
                    << "," << disk
                    << "," << std::setprecision(1) << uptime
                    << "," << g0u << "," << g0t
                    << "," << g1u << "," << g1t;
                append_csv(row.str());

            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    });
    logger.detach();

    // Wait for first data point
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // ── Crow HTTP server ───────────────────────────────
    crow::Crow app;

    // Serve raw CSV log — automatically truncated to ~1 week of history on
    // every hit, so clients never download more than ~7 days of rows (~1-2MB
    // at the current 30s sample rate) instead of the ~330k-row 20MB file we
    // used to serve.
    CROW_ROUTE(app, "/api/metrics")([]() {
        truncate_retention_locked();
        std::ifstream f(LOG_FILE);
        if (!f.is_open()) return std::string("[]");
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    });

    // Live snapshot
    CROW_ROUTE(app, "/api/current")([]() {
        auto gpus = get_gpu_metrics();
        float cpu = get_cpu_usage(g_prev_total, g_prev_idle);
        float cpu_temp = get_cpu_temperature();
        auto mem = get_mem_metrics();
        float disk = get_disk_usage();

        crow::json::wvalue result;
        result["timestamp"] = (int64_t)std::time(nullptr);
        result["cpu_usage"] = cpu;
        result["cpu_temp"] = cpu_temp;
        result["mem_usage_pct"] = mem.pct;
        result["mem_total_gb"] = mem.total_gb;
        result["mem_used_gb"] = mem.used_gb;
        result["disk_usage_pct"] = disk;
        result["uptime_hours"] = get_uptime_hours();

        for (auto& g : gpus) {
            auto make_gpu = []() {
                crow::json::wvalue o;
                return o;
            };
            auto obj = make_gpu();
            obj["id"] = g.id;
            obj["name"] = g.name;
            obj["utilization"] = g.utilization;
            obj["temperature"] = g.temperature;
            obj["mem_used_gb"] = g.mem_used / 1024.0f;
            obj["mem_total_gb"] = g.mem_total / 1024.0f;
            result["gpu_" + std::to_string(g.id)] = std::move(obj);
        }
        return result;
    });

    CROW_ROUTE(app, "/health")([]() { return "ok"; });

    CROW_ROUTE(app, "/")([]() {
        return "Galley Temps monitoring server on port 18900<br>"
               "API: /api/metrics (CSV log)<br>"
               "     /api/current (live JSON)<br>"
               "Dashboard: https://jsteve1.github.io/galley-temps/<br>";
    });

    app.port(18900)
       .multithreaded()
       .run();

    return 0;
}
