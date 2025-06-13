
#include "common.h" // Untuk konfigurasi, getCurrentTimestamp, dan SensorData

#include "nlohmann/json.hpp"

#include <iostream>
#include <string>
#include <thread> // Untuk std::this_thread::sleep_for
#include <chrono>
#include <random> // Untuk data acak
#include <vector> // Untuk buffer dan std::vector<SensorData>
#include <algorithm> // Untuk std::sort

// Platform-specific socket includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
    typedef int socket_t;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR = -1;
#endif

using json = nlohmann::json;

// --- FUNGSI DAN ENUM UNTUK SORTING (Sama seperti di server) ---
enum class SortField {
    TIMESTAMP,
    TEMPERATURE,
    HUMIDITY,
    LIGHT,
    SENSOR_ID
};

enum class SortOrder {
    ASCENDING,
    DESCENDING
};

void sortSensorData(std::vector<SensorData>& data_vec, SortField field, SortOrder order = SortOrder::ASCENDING) {
    std::sort(data_vec.begin(), data_vec.end(),
        [&](const SensorData& a, const SensorData& b) {
        bool comparison_result;
        switch (field) {
            case SortField::TIMESTAMP:
                comparison_result = a.timestamp < b.timestamp;
                break;
            case SortField::TEMPERATURE:
                comparison_result = a.temperature < b.temperature;
                break;
            case SortField::HUMIDITY:
                comparison_result = a.humidity < b.humidity;
                break;
            case SortField::LIGHT:
                comparison_result = a.light < b.light;
                break;
            case SortField::SENSOR_ID:
                comparison_result = a.sensor_id < b.sensor_id;
                break;
            default:
                return false;
        }
        return (order == SortOrder::ASCENDING) ? comparison_result : !comparison_result;
    });
}
// --- AKHIR FUNGSI DAN ENUM SORTING ---


std::mt19937 rng_client(std::chrono::steady_clock::now().time_since_epoch().count());

double random_double_client(double min_val, double max_val) {
    std::uniform_real_distribution<double> dist(min_val, max_val);
    return dist(rng_client);

}

SensorData generate_sensor_data(const std::string& client_id_str) {
    SensorData data;
    data.timestamp = getCurrentTimestamp();
    data.sensor_id = client_id_str;
    data.temperature = random_double_client(22.0, 25.0) + random_double_client(-1.0, 1.0);
    data.humidity = random_double_client(45.0, 55.0) + random_double_client(-5.0, 5.0);
    // Pastikan tipe data light konsisten double sesuai common.h
    data.light = random_double_client(400.0, 700.0) + random_double_client(-50.0, 50.0);


    if (random_double_client(0.0, 1.0) < 0.1) {
        data.temperature = (random_double_client(0.0, 1.0) < 0.5) ? random_double_client(28.0, 35.0) : random_double_client(15.0, 19.0);
        // std::cout << "[" << client_id_str << "] Mensimulasikan anomali suhu!" << std::endl; // Bisa di-uncomment jika perlu
    }
    if (random_double_client(0.0, 1.0) < 0.05) {
        data.humidity = (random_double_client(0.0, 1.0) < 0.5) ? random_double_client(70.0, 90.0) : random_double_client(20.0, 30.0);
        // std::cout << "[" << client_id_str << "] Mensimulasikan anomali kelembapan!" << std::endl; // Bisa di-uncomment jika perlu

    }
    return data;
}
void processAndDisplaySortedData(const std::string& client_id_str, const std::string& json_data_string) {
    std::cout << "\n[" << client_id_str << "] Memproses data yang diterima untuk diurutkan..." << std::endl;
    if (json_data_string.empty() || json_data_string == "[]" || json_data_string == "No data found" || json_data_string == "Error: Could not retrieve data") {
        std::cout << "[" << client_id_str << "] Tidak ada data yang diterima dari server atau data kosong." << std::endl;
        return;
    }
    try {
        json received_json_array = json::parse(json_data_string);
        std::vector<SensorData> sensor_data_list;

        if (received_json_array.is_array()) {
            for (const auto& item : received_json_array) {
                SensorData data;
                data.timestamp = item.value("timestamp", "N/A");
                data.temperature = item.value("temperature", 0.0);
                data.humidity = item.value("humidity", 0.0);
                data.light = item.value("light", 0.0);
                data.sensor_id = item.value("sensor_id", "unknown");
                sensor_data_list.push_back(data);
            }
        } else {
            std::cerr << "[" << client_id_str << "] Data yang diterima bukan JSON array." << std::endl;
            return;
        }

        if (sensor_data_list.empty()) {
            std::cout << "[" << client_id_str << "] Tidak ada data valid untuk diurutkan." << std::endl;
            return;
        }

        // Contoh sorting: berdasarkan suhu secara menurun
        std::cout << "\n[" << client_id_str << "] Data terurut berdasarkan suhu (Menurun) - maksimal 5 data:" << std::endl;
        sortSensorData(sensor_data_list, SortField::TEMPERATURE, SortOrder::DESCENDING);
        for (size_t i = 0; i < sensor_data_list.size() && i < 5; ++i) {
            const auto& d = sensor_data_list[i];
            std::cout << "  ID: " << d.sensor_id << ", Timestamp: " << d.timestamp
                      << ", Temp: " << std::fixed << std::setprecision(2) << d.temperature << " C, Hum: " << d.humidity
                      << "%, Light: " << d.light << " Lux" << std::endl;
        }
         if(sensor_data_list.size() > 5) std::cout << "  ... dan " << sensor_data_list.size() - 5 << " data lainnya." << std::endl;


        // Contoh sorting lain: berdasarkan timestamp secara menaik
        std::cout << "\n[" << client_id_str << "] Data terurut berdasarkan timestamp (Menaik) - maksimal 5 data:" << std::endl;
        sortSensorData(sensor_data_list, SortField::TIMESTAMP, SortOrder::ASCENDING);
        for (size_t i = 0; i < sensor_data_list.size() && i < 5; ++i) {
            const auto& d = sensor_data_list[i];
            std::cout << "  ID: " << d.sensor_id << ", Timestamp: " << d.timestamp
                      << ", Temp: " << std::fixed << std::setprecision(2) << d.temperature << " C, Hum: " << d.humidity
                      << "%, Light: " << d.light << " Lux" << std::endl;
        }
        if(sensor_data_list.size() > 5) std::cout << "  ... dan " << sensor_data_list.size() - 5 << " data lainnya." << std::endl;


    } catch (json::parse_error& e) {
        std::cerr << "[" << client_id_str << "] Error parsing JSON di client: " << e.what() << ". Data diterima: " << json_data_string << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[" << client_id_str << "] Error lain saat memproses data: " << e.what() << std::endl;
    }
}


int main(int argc, char *argv[]) {
    std::string client_id = "CppClient-DefaultID"; // Default jika tidak ada argumen
    if (argc > 1) {
        client_id = argv[1];
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup gagal." << std::endl;
        return 1;
    }
#endif

    socket_t client_socket_sender = INVALID_SOCKET; // Socket untuk mengirim data
    socket_t client_socket_receiver = INVALID_SOCKET; // Socket untuk meminta data (bisa sama atau beda)
    
    bool connected_sender = false;
    int retry_delay_s = 5;
    int messages_to_send = 3; // Kirim beberapa pesan saja untuk demo, lalu minta data

    std::cout << "[" << client_id << "] Memulai mode pengiriman data periodik..." << std::endl;
    for (int i=0; i < messages_to_send ; ++i) {
        if (!connected_sender) {
            client_socket_sender = socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket_sender == INVALID_SOCKET) { /* ... error handling ... */ 
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s)); continue;
            }
            sockaddr_in server_addr_sender;
            server_addr_sender.sin_family = AF_INET;
            server_addr_sender.sin_port = htons(SERVER_PORT);
            if (inet_pton(AF_INET, SERVER_HOST.c_str(), &server_addr_sender.sin_addr) <= 0) { /* ... error handling ... */ 
                CLOSE_SOCKET(client_socket_sender); std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s)); continue;
            }
            if (connect(client_socket_sender, (sockaddr*)&server_addr_sender, sizeof(server_addr_sender)) == SOCKET_ERROR) {
                #ifdef _WIN32
                    std::cerr << "[" << client_id << "] Gagal terhubung ke server untuk mengirim: " << WSAGetLastError() << ". Mencoba lagi..." << std::endl;
                #else
                    perror(("[" + client_id + "] Gagal terhubung ke server untuk mengirim. Mencoba lagi...").c_str());
                #endif
                CLOSE_SOCKET(client_socket_sender); std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s)); continue;
            }
            std::cout << "[" << client_id << "] Terhubung ke server untuk mengirim data." << std::endl;
            connected_sender = true;
        }

        SensorData s_data = generate_sensor_data(client_id);
        json j_msg_data = {
            {"type", "sensor_data"}, // Tambahkan tipe pesan
            {"payload", {
                {"timestamp", s_data.timestamp},
                {"temperature", s_data.temperature},
                {"humidity", s_data.humidity},
                {"light", s_data.light},
                {"sensor_id", s_data.sensor_id}
            }}
        };
        std::string message_to_send = j_msg_data.dump();

        if (send(client_socket_sender, message_to_send.c_str(), message_to_send.length(), 0) == SOCKET_ERROR) {
            /* ... error handling, set connected_sender = false ... */
            connected_sender = false; std::cerr << "[" << client_id << "] Error mengirim data." << std::endl; continue;
        }
        std::cout << "[" << client_id << "] Mengirim: T=" << s_data.temperature << ", H=" << s_data.humidity << ", L=" << s_data.light << std::endl;
        
        char buffer_resp[BUFFER_SIZE];
        memset(buffer_resp, 0, BUFFER_SIZE);
        if (recv(client_socket_sender, buffer_resp, BUFFER_SIZE - 1, 0) > 0) {
            std::cout << "[" << client_id << "] Menerima dari server: " << buffer_resp << std::endl;
        } else { /* ... error handling, set connected_sender = false ... */ 
            connected_sender = false; std::cerr << "[" << client_id << "] Error menerima balasan atau koneksi ditutup." << std::endl;
        }
        if (i < messages_to_send - 1) {
             std::this_thread::sleep_for(std::chrono::seconds(SENSOR_UPDATE_INTERVAL_S));
        }
    }
    if (client_socket_sender != INVALID_SOCKET) {
        CLOSE_SOCKET(client_socket_sender);
    }
    std::cout << "\n[" << client_id << "] Selesai mengirim data periodik.\n" << std::endl;


    // --- Bagian untuk Meminta Semua Data dari Server dan Melakukan Sorting ---
    std::cout << "--- [" << client_id << "] Meminta semua data dari server untuk diurutkan ---" << std::endl;
    client_socket_receiver = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket_receiver == INVALID_SOCKET) {
        std::cerr << "[" << client_id << "] Gagal membuat socket untuk meminta data." << std::endl;
    } else {
        sockaddr_in server_addr_receiver;
        server_addr_receiver.sin_family = AF_INET;
        server_addr_receiver.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_HOST.c_str(), &server_addr_receiver.sin_addr);

        if (connect(client_socket_receiver, (sockaddr*)&server_addr_receiver, sizeof(server_addr_receiver)) != SOCKET_ERROR) {
            std::cout << "[" << client_id << "] Terhubung ke server untuk meminta data." << std::endl;
            
            json request_all_data_msg = {
                {"type", "get_all_data"}
            };
            std::string request_str = request_all_data_msg.dump();
            
            if (send(client_socket_receiver, request_str.c_str(), request_str.length(), 0) != SOCKET_ERROR) {
                std::cout << "[" << client_id << "] Permintaan 'get_all_data' terkirim." << std::endl;
                
                std::string full_data_str = "";
                char recv_buffer[BUFFER_SIZE];
                int bytes_in;
                // Loop untuk menerima semua bagian data jika besar
                // Untuk kesederhanaan, contoh ini mungkin tidak menangani data yang sangat besar dengan sempurna
                // Dalam kasus nyata, Anda mungkin perlu protokol panjang pesan atau delimiter.
                // Namun, karena server akan mengirim semua data dalam satu JSON array besar,
                // kita coba terima sebanyak mungkin.
                std::cout << "[" << client_id << "] Menunggu data dari server..." << std::endl;
                
                // Set timeout untuk recv agar tidak menunggu selamanya
                // Ini adalah implementasi sederhana, idealnya gunakan select() untuk non-blocking recv
                #ifdef _WIN32
                    DWORD timeout_ms = 5000; // 5 detik
                    setsockopt(client_socket_receiver, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
                #else
                    timeval tv;
                    tv.tv_sec = 5; // 5 detik
                    tv.tv_usec = 0;
                    setsockopt(client_socket_receiver, SOL_SOCKET, SO_RCVTIMEO, (const timeval *)&tv, sizeof(tv));
                #endif

                // Loop sederhana untuk menerima data, mungkin perlu disempurnakan untuk data besar
                // Untuk data yang sangat besar, server harus mengirim panjangnya dulu atau menggunakan delimiter.
                // Asumsi saat ini, server mengirimkan JSON array string dalam satu atau beberapa paket.
                // Dan BUFFER_SIZE cukup untuk menampung respons JSON secara keseluruhan.
                // Ini adalah penyederhanaan.
                bytes_in = recv(client_socket_receiver, recv_buffer, BUFFER_SIZE - 1, 0);
                if (bytes_in > 0) {
                    recv_buffer[bytes_in] = '\0';
                    full_data_str.append(recv_buffer);
                    std::cout << "[" << client_id << "] Menerima " << bytes_in << " bytes dari server." << std::endl;
                     // Jika data sangat besar dan server mengirimnya dalam beberapa bagian,
                     // Anda memerlukan loop di sini dan mekanisme untuk mengetahui kapan semua data telah diterima.
                     // Untuk saat ini, kita asumsikan server mengirim semua data dan itu muat di buffer,
                     // atau setidaknya bagian signifikan pertama.
                } else if (bytes_in == 0) {
                    std::cout << "[" << client_id << "] Server menutup koneksi saat meminta data." << std::endl;
                } else {
                    #ifdef _WIN32
                        if(WSAGetLastError() == WSAETIMEDOUT) {
                             std::cerr << "[" << client_id << "] Timeout saat menerima data dari server." << std::endl;
                        } else {
                             std::cerr << "[" << client_id << "] Error recv data dari server: " << WSAGetLastError() << std::endl;
                        }
                    #else
                         if(errno == EAGAIN || errno == EWOULDBLOCK) {
                            std::cerr << "[" << client_id << "] Timeout saat menerima data dari server." << std::endl;
                         } else {
                            perror(("[" + client_id + "] Error recv data dari server").c_str());
                         }
                    #endif
                }
                
                if (!full_data_str.empty()) {
                    processAndDisplaySortedData(client_id, full_data_str);
                } else {
                    std::cout << "[" << client_id << "] Tidak menerima data dari server untuk diurutkan." << std::endl;
                }

            } else {
                std::cerr << "[" << client_id << "] Gagal mengirim permintaan 'get_all_data'." << std::endl;
            }
            CLOSE_SOCKET(client_socket_receiver);
        } else {
            std::cerr << "[" << client_id << "] Gagal terhubung ke server untuk meminta data." << std::endl;
        }
    }
    std::cout << "--- [" << client_id << "] Selesai sesi permintaan data dan sorting ---" << std::endl;

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}