#ifndef ANOMALY_DETECTOR_H
#define ANOMALY_DETECTOR_H

#include "common.h"
#include <vector>
#include <string>

struct AnomalyResult {
    SensorData data;
    std::vector<std::string> anomaly_descriptions;
    bool is_anomalous = false;
};

class AnomalyDetector {
public:
    AnomalyDetector();
    std::vector<std::string> checkAnomaly(const SensorData& data); // Mengembalikan deskripsi anomali
    std::vector<AnomalyResult> searchHistoricalAnomalies(const std::vector<SensorData>& all_data,
                                                         const std::string& start_time = "",
                                                         const std::string& end_time = "");

};

#endif // ANOMALY_DETECTOR_H