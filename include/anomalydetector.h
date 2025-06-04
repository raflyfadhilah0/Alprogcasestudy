#ifndef ANOMALY_DETECTOR_H
#define ANOMALY_DETECTOR_H

#include "common.h"
#include <vector>
#include <string>
#include <algorithm> // Untuk std::sort
#include <utility> // Untuk std::pair

struct AnomalyResult {
    SensorData data;
    std::vector<std::string> anomaly_descriptions;
    bool is_anomalous = false;
    double deviation_score = 0.0; // Skor untuk mengukur tingkat deviasi
};

class AnomalyDetector {
public:
    AnomalyDetector();

    // Mengembalikan deskripsi anomali dan skor deviasi
    std::pair<std::vector<std::string>, double> checkAnomalyWithScore(const SensorData& data);
    
    // Versi lama, bisa tetap ada jika digunakan untuk pengecekan cepat tanpa perlu skor
    std::vector<std::string> checkAnomaly(const SensorData& data); 

    std::vector<AnomalyResult> searchHistoricalAnomalies(
        const std::vector<SensorData>& all_data,
        const std::string& start_time = "",
        const std::string& end_time = "",
        const std::string& sort_by = "timestamp", // Opsi: "timestamp" atau "deviation"
        bool sort_descending = false); // true untuk urutan menurun, false untuk menaik
};

#endif // ANOMALY_DETECTOR_H