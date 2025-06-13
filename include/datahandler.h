#ifndef DATA_HANDLER_H
#define DATA_HANDLER_H

#include "common.h"
#include "nlohmann/json.hpp" // Pastikan path ini benar
#include <vector>
#include <string>
#include <mutex> // Untuk thread-safety

using json = nlohmann::json;

class DataHandler {
public:
    DataHandler();

    void addSensorData(const SensorData& data);
    std::vector<SensorData> getAllSensorData() const; // Perlu hati-hati dengan data besar
    void saveToBinaryPeriodic(bool force = false);
    void exportToJsonPeriodic(bool force = false);
    void loadFromBinary();

    std::vector<SensorData> searchData(const std::string& start_timestamp, const std::string& end_timestamp, const std::string& sort_by = "timestamp", bool reverse = false);


private:
    std::vector<SensorData> sensor_data_store_;
    std::vector<SensorData> json_export_batch_;
    mutable std::mutex data_mutex_; // Melindungi akses ke sensor_data_store_ dan json_export_batch_

    std::chrono::time_point<std::chrono::steady_clock> last_binary_save_time_;
    std::chrono::time_point<std::chrono::steady_clock> last_json_export_time_;

    void saveToBinaryInternal();
    void exportToJsonInternal(const std::vector<SensorData>& data_to_export);
};

#endif // DATA_HANDLER_H