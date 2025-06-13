#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <chrono> // Untuk timestamp
#include <iomanip> // Untuk format waktu
#include <sstream> // Untuk format waktu
#include <fstream> // Untuk file I/O

// Konfigurasi
const std::string SERVER_HOST = "127.0.0.1"; // Gunakan 127.0.0.1 untuk localhost
const int SERVER_PORT = 9999;
const int BUFFER_SIZE = 4096; // Tingkatkan buffer jika data yang diminta bisa sangat besar
const int SENSOR_UPDATE_INTERVAL_S = 5;
const int BINARY_SAVE_INTERVAL_S = 60;
const int JSON_EXPORT_INTERVAL_S = 300;

const std::string BINARY_DATA_FILE = "sensor_data.bin";
const std::string JSON_DATA_FILE_TEMPLATE_PREFIX = "sensor_data_export_";
const std::string JSON_DATA_FILE_TEMPLATE_SUFFIX = ".json";

// Batas normal (contoh)
struct AnomalyThresholds {
    double temp_min = 20.0;
    double temp_max = 26.0;
    double humidity_min = 40.0;
    double humidity_max = 60.0;
    double light_min = 300.0;
    double light_max = 800.0;
};

extern AnomalyThresholds global_thresholds; // Deklarasi eksternal

struct SensorData {
    std::string timestamp; // ISO 8601 format: YYYY-MM-DDTHH:MM:SS.sssZ
    double temperature;
    double humidity;
    double light;
    std::string sensor_id;

    // Untuk serialisasi/deserialisasi biner sederhana
    void serialize(std::ostream& os) const;
    void deserialize(std::istream& is);
};

// Fungsi utilitas untuk timestamp
inline std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&itt), "%Y-%m-%dT%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return ss.str();
}

#endif // COMMON_H