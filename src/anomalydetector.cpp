#include "anomalydetector.h"
#include <sstream>    // Untuk format pesan
#include <iostream>   // Untuk debug
#include <cmath>      // Untuk fabs (jika menggunakan metode skor deviasi yang lebih kompleks)
#include <algorithm>  // Untuk std::sort
#include <iomanip>    // Untuk std::fixed, std::setprecision

AnomalyDetector::AnomalyDetector() {}

// Implementasi checkAnomaly yang memanggil versi baru dengan skor
std::vector<std::string> AnomalyDetector::checkAnomaly(const SensorData& data) {
    return checkAnomalyWithScore(data).first;
}

// Implementasi baru yang mengembalikan deskripsi dan skor deviasi
std::pair<std::vector<std::string>, double> AnomalyDetector::checkAnomalyWithScore(const SensorData& data) {
    std::vector<std::string> anomalies;
    std::stringstream ss;
    double current_deviation_score = 0.0;

    // Ambil timestamp tanpa milidetik untuk pesan yang lebih bersih
    std::string display_timestamp = data.timestamp;
    size_t dot_pos = display_timestamp.rfind('.');
    if (dot_pos != std::string::npos) {
        display_timestamp = display_timestamp.substr(0, dot_pos);
    }

    // Cek Suhu
    if (data.temperature < global_thresholds.temp_min) {
        ss.str(""); // Clear stringstream
        ss << "Peringatan (" << display_timestamp << "): Suhu (" << std::fixed << std::setprecision(2) << data.temperature
           << " C) di bawah batas normal minimum (" << global_thresholds.temp_min << " C).";
        anomalies.push_back(ss.str());
        current_deviation_score += (global_thresholds.temp_min - data.temperature);
    } else if (data.temperature > global_thresholds.temp_max) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Suhu (" << std::fixed << std::setprecision(2) << data.temperature
           << " C) di atas batas normal maksimum (" << global_thresholds.temp_max << " C).";
        anomalies.push_back(ss.str());
        current_deviation_score += (data.temperature - global_thresholds.temp_max);
    }

    // Cek Kelembapan
    if (data.humidity < global_thresholds.humidity_min) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Kelembapan (" << std::fixed << std::setprecision(2) << data.humidity
           << " %) di bawah batas normal minimum (" << global_thresholds.humidity_min << " %).";
        anomalies.push_back(ss.str());
        current_deviation_score += (global_thresholds.humidity_min - data.humidity);
    } else if (data.humidity > global_thresholds.humidity_max) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Kelembapan (" << std::fixed << std::setprecision(2) << data.humidity
           << " %) di atas batas normal maksimum (" << global_thresholds.humidity_max << " %).";
        anomalies.push_back(ss.str());
        current_deviation_score += (data.humidity - global_thresholds.humidity_max);
    }

    // Cek Intensitas Cahaya
    if (data.light < global_thresholds.light_min) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Intensitas Cahaya (" << std::fixed << std::setprecision(0) << data.light
           << " Lux) di bawah batas normal minimum (" << global_thresholds.light_min << " Lux).";
        anomalies.push_back(ss.str());
        current_deviation_score += (global_thresholds.light_min - data.light);
    } else if (data.light > global_thresholds.light_max) {
        ss.str("");
        ss << "Peringatan (" << display_timestamp << "): Intensitas Cahaya (" << std::fixed << std::setprecision(0) << data.light
           << " Lux) di atas batas normal maksimum (" << global_thresholds.light_max << " Lux).";
        anomalies.push_back(ss.str());
        current_deviation_score += (data.light - global_thresholds.light_max);
    }
    
    return {anomalies, current_deviation_score};
}

std::vector<AnomalyResult> AnomalyDetector::searchHistoricalAnomalies(
    const std::vector<SensorData>& all_data,
    const std::string& start_time_str,
    const std::string& end_time_str,
    const std::string& sort_by,
    bool sort_descending) {
    std::vector<AnomalyResult> found_anomalies;

    for (const auto& record : all_data) {
        // Filter waktu
        if (!start_time_str.empty() && record.timestamp < start_time_str) {
            continue;
        }
        if (!end_time_str.empty() && record.timestamp > end_time_str) {
            continue;
        }

        auto [anomalies_detected, score] = checkAnomalyWithScore(record); // Gunakan metode baru
        if (!anomalies_detected.empty()) {
            AnomalyResult ar;
            ar.data = record;
            ar.anomaly_descriptions = anomalies_detected;
            ar.is_anomalous = true;
            ar.deviation_score = score; // Simpan skor deviasi
            found_anomalies.push_back(ar);
        }
    }

    // Logika Pengurutan
    if (sort_by == "timestamp") {
        std::sort(found_anomalies.begin(), found_anomalies.end(), 
            [&](const AnomalyResult& a, const AnomalyResult& b) {
            if (sort_descending) return a.data.timestamp > b.data.timestamp;
            return a.data.timestamp < b.data.timestamp;
        });
    } else if (sort_by == "deviation") {
        std::sort(found_anomalies.begin(), found_anomalies.end(), 
            [&](const AnomalyResult& a, const AnomalyResult& b) {
            // Skor deviasi yang lebih tinggi biasanya berarti anomali yang lebih "parah"
            if (sort_descending) return a.deviation_score > b.deviation_score;
            return a.deviation_score < b.deviation_score;
        });
    }
    // Anda bisa menambahkan peringatan jika kriteria sort_by tidak valid

    return found_anomalies;
}