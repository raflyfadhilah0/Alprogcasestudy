#include "datahandler.h"
#include "anomalydetector.h"
#include "nlohmann/json.hpp"
#include "common.h" // Sudah berisi definisi SensorData

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <csignal>   // For signal handling (Ctrl+C)
#include <atomic>
#include <algorithm> // Ditambahkan untuk std::sort

// Platform-specific socket includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // Link with Ws2_32.lib
    #define CLOSE_SOCKET closesocket
    typedef SOCKET socket_t;
#else // POSIX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h> // For close
    #define CLOSE_SOCKET close
    typedef int socket_t;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR = -1;
#endif

using json = nlohmann::json;

// --- FUNGSI DAN ENUM UNTUK SORTING ---
// Enum untuk menentukan field mana yang akan di-sort
enum class SortField {
    TIMESTAMP,
    TEMPERATURE,
    HUMIDITY,
    LIGHT,
    SENSOR_ID
};

// Enum untuk menentukan urutan sorting
enum class SortOrder {
    ASCENDING,
    DESCENDING
};

/**
 * @brief Mengurutkan vektor SensorData berdasarkan field dan urutan yang ditentukan.
 * @param data_vec Vektor SensorData yang akan diurutkan (passed by reference).
 * @param field Field yang menjadi dasar pengurutan (TIMESTAMP, TEMPERATURE, dll.).
 * @param order Urutan pengurutan (ASCENDING atau DESCENDING).
 */
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
            default: // Seharusnya tidak terjadi jika enum valid
                return false;
        }
        return (order == SortOrder::ASCENDING) ? comparison_result : !comparison_result;
    });
}
// --- AKHIR FUNGSI DAN ENUM SORTING ---


// --- DEFINISI VARIABEL GLOBAL YANG DEKLARASIKAN DI common.h ---
// Ini adalah definisi untuk global_thresholds yang dideklarasikan sebagai 'extern' di common.h
AnomalyThresholds global_thresholds = {
    .temp_min = 20.0, .temp_max = 26.0,
    .humidity_min = 40.0, .humidity_max = 60.0,
    .light_min = 300.0, .light_max = 800.0
}; //
// --- AKHIR DEFINISI GLOBAL ---


DataHandler data_handler; // Global instance for server
AnomalyDetector anomaly_detector; // Global instance

std::atomic<bool> server_running(true); // Untuk menghentikan server dengan graceful

void signal_handler(int signum) {
    std::cout << "\n[SERVER] Menerima sinyal interupsi (" << signum << "), mematikan server..." << std::endl;
    server_running = false;
}


void handle_client_connection(socket_t client_socket, std::string client_ip) {
    std::cout << "[SERVER] Koneksi diterima dari " << client_ip << std::endl;
    char buffer[BUFFER_SIZE]; // BUFFER_SIZE dari common.h

    while (server_running) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received == SOCKET_ERROR) {
            #ifdef _WIN32
                if (WSAGetLastError() == WSAECONNRESET) {
                    std::cerr << "[SERVER] Koneksi dari " << client_ip << " direset paksa." << std::endl;
                } else {
                    std::cerr << "[SERVER] Error recv dari " << client_ip << ": " << WSAGetLastError() << std::endl;
                }
            #else
                if (errno == ECONNRESET) {
                    std::cerr << "[SERVER] Koneksi dari " << client_ip << " direset paksa." << std::endl;
                } else {
                    perror("[SERVER] Error recv");
                }
            #endif
            break;
        }
        if (bytes_received == 0) {
            std::cout << "[SERVER] Koneksi dari " << client_ip << " ditutup oleh client." << std::endl;
            break;
        }
        if (!server_running) break; // Cek jika server diminta berhenti

        buffer[bytes_received] = '\0'; // Null-terminate C-string
        std::string message_str(buffer);
        // std::cout << "[SERVER] Menerima raw: " << message_str << std::endl;


        try {
            json received_json = json::parse(message_str);
            SensorData data; // SensorData dari common.h
            data.timestamp = received_json.value("timestamp", getCurrentTimestamp()); // getCurrentTimestamp() dari common.h
            data.temperature = received_json.at("temperature");
            data.humidity = received_json.at("humidity");
            data.light = received_json.at("light");
            data.sensor_id = received_json.value("sensor_id", "unknown_sensor");

            std::cout << "[SERVER] Menerima data dari " << client_ip
                      << ": T=" << data.temperature << ", H=" << data.humidity
                      << ", L=" << data.light << ", ID=" << data.sensor_id << std::endl;

            // Analisis awal
            std::vector<std::string> anomalies = anomaly_detector.checkAnomaly(data); // checkAnomaly dari AnomalyDetector
            if (!anomalies.empty()) {
                for (const auto& desc : anomalies) {
                    std::cout << "[PERINGATAN CEPAT] " << desc << " - Dari: " << client_ip << std::endl;
                }
            }

            data_handler.addSensorData(data); // addSensorData dari DataHandler

            // Kirim balasan (opsional)
            const char* response = "Data diterima";
            send(client_socket, response, strlen(response), 0);

        } catch (json::parse_error& e) {
            std::cerr << "[SERVER] Error parsing JSON dari " << client_ip << ": " << e.what() << ". Pesan: " << message_str << std::endl;
            const char* error_response = "Error: Format data JSON tidak valid";
            send(client_socket, error_response, strlen(error_response), 0);
        } catch (json::type_error& e) { // Misal jika tipe data tidak sesuai (string vs number)
            std::cerr << "[SERVER] Error tipe JSON dari " << client_ip << ": " << e.what() << ". Pesan: " << message_str << std::endl;
            const char* error_response = "Error: Tipe data JSON tidak sesuai";
            send(client_socket, error_response, strlen(error_response), 0);
        } catch (json::out_of_range& e) { // Misal jika key 'at(...)' tidak ditemukan
            std::cerr << "[SERVER] Error key JSON tidak ditemukan dari " << client_ip << ": " << e.what() << ". Pesan: " << message_str << std::endl;
            std::string err_msg = "Error: Data tidak lengkap, key hilang: ";
            err_msg += e.what();
            send(client_socket, err_msg.c_str(), err_msg.length(), 0);
        }
        catch (const std::exception& e) {
            std::cerr << "[SERVER] Error memproses data dari " << client_ip << ": " << e.what() << std::endl;
            const char* error_response = "Error internal server";
            send(client_socket, error_response, strlen(error_response), 0);
        }
    }

    CLOSE_SOCKET(client_socket);
    std::cout << "[SERVER] Koneksi dengan " << client_ip << " ditutup." << std::endl;
}

void periodic_tasks_thread_func() {
    while (server_running) {
        for(int i = 0; i < 10 && server_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!server_running) break;

        data_handler.saveToBinaryPeriodic(); //
        data_handler.exportToJsonPeriodic(); //

        // --- CONTOH PENGGUNAAN FUNGSI SORTING ---
        if (server_running) {
            std::cout << "\n[SERVER TASK PERIODIK] Mengambil dan mengurutkan data..." << std::endl;
            
            std::vector<SensorData> all_data = data_handler.getAllSensorData(); // getAllSensorData dari DataHandler

            if (!all_data.empty()) {
                // Contoh: Urutkan berdasarkan temperatur secara menurun
                sortSensorData(all_data, SortField::TEMPERATURE, SortOrder::DESCENDING);
                std::cout << "[SERVER TASK PERIODIK] Top 5 Data diurutkan berdasarkan temperatur (DESC):" << std::endl;
                for (size_t i = 0; i < all_data.size() && i < 5; ++i) {
                    const auto& d = all_data[i];
                    std::cout << "  Timestamp: " << d.timestamp
                              << ", Temp: " << d.temperature
                              << ", Hum: " << d.humidity
                              << ", Light: " << d.light
                              << ", ID: " << d.sensor_id << std::endl;
                }

                // Contoh: Urutkan berdasarkan timestamp secara menaik
                sortSensorData(all_data, SortField::TIMESTAMP, SortOrder::ASCENDING);
                std::cout << "[SERVER TASK PERIODIK] Top 5 Data diurutkan berdasarkan timestamp (ASC):" << std::endl;
                for (size_t i = 0; i < all_data.size() && i < 5; ++i) {
                     const auto& d = all_data[i];
                    std::cout << "  Timestamp: " << d.timestamp
                              << ", Temp: " << d.temperature
                              << ", Hum: " << d.humidity
                              << ", Light: " << d.light
                              << ", ID: " << d.sensor_id << std::endl;
                }
            } else {
                std::cout << "[SERVER TASK PERIODIK] Tidak ada data untuk diurutkan saat ini." << std::endl;
            }

            // Contoh pencarian anomali historis (dari kode asli, bisa digabung atau disesuaikan)
            // std::cout << "\n[SERVER TASK] Mencari anomali historis..." << std::endl;
            // auto historical_anomalies = anomaly_detector.searchHistoricalAnomalies(all_data);
            // if (!historical_anomalies.empty()) {
            //     std::cout << "[SERVER TASK] Anomali historis ditemukan:" << std::endl;
            //     for (const auto& anom_res : historical_anomalies) {
            //         for(const auto& desc : anom_res.anomaly_descriptions){
            //             std::cout << "   - " << desc << " (ID: " << anom_res.data.sensor_id << ")" << std::endl;
            //         }
            //     }
            // } else {
            //     std::cout << "[SERVER TASK] Tidak ada anomali historis baru." << std::endl;
            // }
        }
        // --- AKHIR CONTOH PENGGUNAAN FUNGSI SORTING ---
    }
    std::cout << "[SERVER] Thread tugas periodik dihentikan." << std::endl;
    data_handler.exportToJsonPeriodic(true); // Force export
    data_handler.saveToBinaryPeriodic(true); // Force save
}


int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup gagal." << std::endl;
        return 1;
    }
#endif

    socket_t listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == INVALID_SOCKET) {
        #ifdef _WIN32
            std::cerr << "Error membuat socket: " << WSAGetLastError() << std::endl;
        #else
            perror("Error membuat socket");
        #endif
        return 1;
    }

    int optval = 1;
    #ifdef _WIN32
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
    #else
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    #endif

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT); // SERVER_PORT dari common.h
    if (inet_pton(AF_INET, SERVER_HOST.c_str(), &server_addr.sin_addr) <= 0) { // SERVER_HOST dari common.h
        std::cerr << "inet_pton error untuk " << SERVER_HOST << std::endl;
        CLOSE_SOCKET(listen_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        #ifdef _WIN32
            std::cerr << "Bind gagal: " << WSAGetLastError() << std::endl;
        #else
            perror("Bind gagal");
        #endif
        CLOSE_SOCKET(listen_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        #ifdef _WIN32
            std::cerr << "Listen gagal: " << WSAGetLastError() << std::endl;
        #else
            perror("Listen gagal");
        #endif
        CLOSE_SOCKET(listen_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    std::cout << "[SERVER] Server berjalan di " << SERVER_HOST << ":" << SERVER_PORT << std::endl;
    std::cout << "[SERVER] Menunggu koneksi client..." << std::endl;

    std::thread periodic_thread(periodic_tasks_thread_func);
    std::vector<std::thread> client_threads;

    while (server_running) {
        sockaddr_in client_addr;
        #ifdef _WIN32
            int client_addr_len = sizeof(client_addr);
        #else
            socklen_t client_addr_len = sizeof(client_addr);
        #endif

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_socket, &read_fds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(listen_socket + 1, &read_fds, NULL, NULL, &timeout);

        if (select_result == SOCKET_ERROR) {
            if (!server_running) { // Jika server berhenti, select bisa terinterupsi
                 std::cout << "[SERVER] Select terinterupsi karena server shutdown." << std::endl;
                 break;
            }
            #ifdef _WIN32
                std::cerr << "[SERVER] Select error: " << WSAGetLastError() << std::endl;
            #else
                perror("[SERVER] Select error");
            #endif
            // Pertimbangkan break jika error berlanjut dan bukan karena shutdown
            if (errno != EINTR) { // Hindari loop tak terbatas pada EINTR jika bukan shutdown
                 break;
            }
            continue;
        }

        if (select_result == 0) {
            continue;
        }

        socket_t client_socket = accept(listen_socket, (sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            if (!server_running) {
                std::cout << "[SERVER] Accept terinterupsi karena server shutdown." << std::endl;
                break;
            }
            #ifdef _WIN32
                std::cerr << "[SERVER] Accept gagal: " << WSAGetLastError() << std::endl;
            #else
                perror("[SERVER] Accept gagal");
            #endif
            continue;
        }

        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
        client_threads.emplace_back(handle_client_connection, client_socket, std::string(client_ip_str));
    }

    std::cout << "[SERVER] Menunggu client threads selesai..." << std::endl;
    for (std::thread& t : client_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
     std::cout << "[SERVER] Semua client threads selesai." << std::endl;

    std::cout << "[SERVER] Menunggu periodic task thread selesai..." << std::endl;
    if (periodic_thread.joinable()) {
        periodic_thread.join();
    }
    std::cout << "[SERVER] Periodic task thread selesai." << std::endl;

    CLOSE_SOCKET(listen_socket);
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[SERVER] Server dimatikan." << std::endl;
    return 0;
}