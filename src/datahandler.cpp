#include "datahandler.h"
#include <iostream>
#include <algorithm> // Untuk std::sort
#include <fstream> // Sudah di common.h tapi baik untuk dicantumkan

// Implementasi SensorData serialize/deserialize
void SensorData::serialize(std::ostream& os) const {
    size_t ts_len = timestamp.length();
    os.write(reinterpret_cast<const char*>(&ts_len), sizeof(ts_len));
    os.write(timestamp.c_str(), ts_len);

    os.write(reinterpret_cast<const char*>(&temperature), sizeof(temperature));
    os.write(reinterpret_cast<const char*>(&humidity), sizeof(humidity));
    os.write(reinterpret_cast<const char*>(&light), sizeof(light));

    size_t id_len = sensor_id.length();
    os.write(reinterpret_cast<const char*>(&id_len), sizeof(id_len));
    os.write(sensor_id.c_str(), id_len);
}

void SensorData::deserialize(std::istream& is) {
    size_t ts_len;
    is.read(reinterpret_cast<char*>(&ts_len), sizeof(ts_len));
    if (is.gcount() != sizeof(ts_len)) return; // Error atau EOF
    timestamp.resize(ts_len);
    is.read(&timestamp[0], ts_len);

    is.read(reinterpret_cast<char*>(&temperature), sizeof(temperature));
    is.read(reinterpret_cast<char*>(&humidity), sizeof(humidity));
    is.read(reinterpret_cast<char*>(&light), sizeof(light));

    size_t id_len;
    is.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));
    if (is.gcount() != sizeof(id_len)) return;
    sensor_id.resize(id_len);
    is.read(&sensor_id[0], id_len);
}


DataHandler::DataHandler() {
    last_binary_save_time_ = std::chrono::steady_clock::now();
    last_json_export_time_ = std::chrono::steady_clock::now();
    loadFromBinary(); // Muat data saat inisialisasi
}

void DataHandler::addSensorData(const SensorData& data) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    sensor_data_store_.push_back(data);
    json_export_batch_.push_back(data);
}

std::vector<SensorData> DataHandler::getAllSensorData() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return sensor_data_store_; // Mengembalikan salinan
}

void DataHandler::saveToBinaryInternal() {
    std::ofstream ofs(BINARY_DATA_FILE, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << "Error: Tidak dapat membuka file biner untuk ditulis: " << BINARY_DATA_FILE << std::endl;
        return;
    }
    // Salin data_store di dalam lock untuk meminimalkan waktu lock
    std::vector<SensorData> data_to_save;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        data_to_save = sensor_data_store_;
    }

    size_t count = data_to_save.size();
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& record : data_to_save) {
        record.serialize(ofs);
    }
    ofs.close();
    std::cout << "Data berhasil disimpan ke " << BINARY_DATA_FILE << std::endl;
}


void DataHandler::loadFromBinary() {
    std::ifstream ifs(BINARY_DATA_FILE, std::ios::binary);
    if (!ifs) {
        std::cout << "Info: File biner tidak ditemukan atau tidak dapat dibuka: " << BINARY_DATA_FILE << ". Memulai dengan data kosong." << std::endl;
        return;
    }
    std::lock_guard<std::mutex> lock(data_mutex_);
    sensor_data_store_.clear();

    size_t count;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (ifs.gcount() != sizeof(count) || ifs.eof()){
         std::cerr << "Error: Format file biner tidak valid atau file kosong." << std::endl;
         ifs.close();
         return;
    }


    for (size_t i = 0; i < count; ++i) {
        SensorData record;
        record.deserialize(ifs);
        if(ifs.fail() || ifs.eof() && i < count -1) { // Cek jika read gagal atau EOF prematur
            std::cerr << "Error membaca record ke-" << i << " dari file biner." << std::endl;
            break;
        }
        sensor_data_store_.push_back(record);
    }
    ifs.close();
    std::cout << "Data berhasil dimuat dari " << BINARY_DATA_FILE << ". Jumlah record: " << sensor_data_store_.size() << std::endl;
}

void DataHandler::exportToJsonInternal(const std::vector<SensorData>& data_to_export) {
    if (data_to_export.empty()) {
        std::cout << "Tidak ada data baru untuk diekspor ke JSON." << std::endl;
        return;
    }

    std::string timestamp_str = getCurrentTimestamp();
    // Ganti karakter yang tidak valid untuk nama file
    std::replace(timestamp_str.begin(), timestamp_str.end(), ':', '-');
    std::replace(timestamp_str.begin(), timestamp_str.end(), '.', '_');

    std::string filename = JSON_DATA_FILE_TEMPLATE_PREFIX + timestamp_str + JSON_DATA_FILE_TEMPLATE_SUFFIX;
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "Error: Tidak dapat membuka file JSON untuk ditulis: " << filename << std::endl;
        return;
    }

    json j_array = json::array();
    for (const auto& record : data_to_export) {
        json j_record;
        j_record["timestamp"] = record.timestamp;
        j_record["temperature"] = record.temperature;
        j_record["humidity"] = record.humidity;
        j_record["light"] = record.light;
        j_record["sensor_id"] = record.sensor_id;
        j_array.push_back(j_record);
    }

    ofs << j_array.dump(4); // Indentasi 4 spasi
    ofs.close();
    std::cout << "Data berhasil diekspor ke " << filename << std::endl;
}

void DataHandler::saveToBinaryPeriodic(bool force) {
    auto now = std::chrono::steady_clock::now();
    if (force || std::chrono::duration_cast<std::chrono::seconds>(now - last_binary_save_time_).count() >= BINARY_SAVE_INTERVAL_S) {
        saveToBinaryInternal();
        last_binary_save_time_ = now;
    }
}

void DataHandler::exportToJsonPeriodic(bool force) {
    auto now = std::chrono::steady_clock::now();
     if (force || std::chrono::duration_cast<std::chrono::seconds>(now - last_json_export_time_).count() >= JSON_EXPORT_INTERVAL_S) {
        std::vector<SensorData> data_batch_copy;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (!json_export_batch_.empty()) {
                data_batch_copy = json_export_batch_;
                json_export_batch_.clear(); // Kosongkan batch setelah disalin
            }
        }
        if (!data_batch_copy.empty()) {
            exportToJsonInternal(data_batch_copy);
        }
        last_json_export_time_ = now;
    }
}


std::vector<SensorData> DataHandler::searchData(const std::string& start_timestamp_str, const std::string& end_timestamp_str, const std::string& sort_by, bool reverse) {
    std::vector<SensorData> results;
    std::vector<SensorData> current_data;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        current_data = sensor_data_store_; // Salin untuk pencarian
    }

    // Parsing timestamp string ke time_point akan lebih kompleks di C++
    // Untuk kesederhanaan, kita akan membandingkan string secara leksikografis
    // Ini bekerja jika format timestamp konsisten (ISO 8601)
    for (const auto& record : current_data) {
        bool include = true;
        if (!start_timestamp_str.empty() && record.timestamp < start_timestamp_str) {
            include = false;
        }
        if (!end_timestamp_str.empty() && record.timestamp > end_timestamp_str) {
            include = false;
        }
        if (include) {
            results.push_back(record);
        }
    }

    if (sort_by == "timestamp") {
        std::sort(results.begin(), results.end(), [&](const SensorData& a, const SensorData& b) {
            if (reverse) return a.timestamp > b.timestamp;
            return a.timestamp < b.timestamp;
        });
    }
    // Kriteria sorting lain bisa ditambahkan di sini (misal, deviasi)
    // Sorting by "deviation" would require calculating deviation first.

    return results;
}