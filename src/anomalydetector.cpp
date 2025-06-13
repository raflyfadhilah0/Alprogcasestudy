#include "anomalydetector.h"
#include <sstream> // Untuk format pesan
#include <iostream> // Untuk debug

AnomalyDetector::AnomalyDetector() {}

std::vector<std::string> AnomalyDetector::checkAnomaly(const SensorData& data) {
    std::vector<std::string> anomalies;
    std::stringstream ss;

    // Ambil timestamp tanpa milidetik untuk pesan yang lebih bersih
    std::string display_timestamp = data.timestamp;
    size_t dot_pos = display_timestamp.rfind('.');
    if (dot_pos != std::string::npos) {
        display_timestamp = display_timestamp.substr(0, dot_pos);
    }


    if (data.temperature < global_thresholds.temp_min || data.temperature > global_thresholds.temp_max) {
        ss.str(""); // Clear stringstream
        ss << "Peringatan (" << display_timestamp << "): Suhu (" << std::fixed << std::setprecision(2) << data.temperature
           << " C) di luar batas normal [" << global_thresholds.temp_min << ", " << global_thresholds.temp_max << "].";
        anomalies.push_back(ss.str());
    }
    if (data.humidity < global_thresholds.humidity_min || data.humidity > global_thresholds.humidity_max) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Kelembapan (" << std::fixed << std::setprecision(2) << data.humidity
           << " %) di luar batas normal [" << global_thresholds.humidity_min << ", " << global_thresholds.humidity_max << "].";
        anomalies.push_back(ss.str());
    }
    if (data.light < global_thresholds.light_min || data.light > global_thresholds.light_max) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Intensitas Cahaya (" << std::fixed << std::setprecision(0) << data.light
           << " Lux) di luar batas normal [" << global_thresholds.light_min << ", " << global_thresholds.light_max << "].";
        anomalies.push_back(ss.str());
    }
    return anomalies;
}

std::vector<AnomalyResult> AnomalyDetector::searchHistoricalAnomalies(
    const std::vector<SensorData>& all_data,
    const std::string& start_time_str,
    const std::string& end_time_str) {
    std::vector<AnomalyResult> found_anomalies;

    for (const auto& record : all_data) {
        // Filter waktu
        if (!start_time_str.empty() && record.timestamp < start_time_str) {
            continue;
        }
        if (!end_time_str.empty() && record.timestamp > end_time_str) {
            continue;
        }

        std::vector<std::string> anomalies_detected = checkAnomaly(record);
        if (!anomalies_detected.empty()) {
            AnomalyResult ar;
            ar.data = record;
            ar.anomaly_descriptions = anomalies_detected;
            ar.is_anomalous = true;
            found_anomalies.push_back(ar);
        }
    }
    // Pengurutan bisa ditambahkan di sini jika perlu
    // std::sort(found_anomalies.begin(), found_anomalies.end(), ...);
    return found_anomalies;
}