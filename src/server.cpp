#include "datahandler.h"
#include "anomalydetector.h"
#include "nlohmann/json.hpp"
#include "common.h"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <csignal> // For signal handling (Ctrl+C)
#include <atomic> 

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

// --- DEFINISI VARIABEL GLOBAL YANG DEKLARASIKAN DI common.h ---
// Ini adalah definisi untuk global_thresholds yang dideklarasikan sebagai 'extern' di common.h
AnomalyThresholds global_thresholds = {
    .temp_min = 20.0, .temp_max = 26.0,
    .humidity_min = 40.0, .humidity_max = 60.0,
    .light_min = 300.0, .light_max = 800.0
};
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
    char buffer[BUFFER_SIZE];

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
            SensorData data;
            data.timestamp = received_json.value("timestamp", getCurrentTimestamp()); // Default jika tidak ada
            data.temperature = received_json.at("temperature"); // at() akan throw jika key tidak ada
            data.humidity = received_json.at("humidity");
            data.light = received_json.at("light");
            data.sensor_id = received_json.value("sensor_id", "unknown_sensor");

            std::cout << "[SERVER] Menerima data dari " << client_ip
                      << ": T=" << data.temperature << ", H=" << data.humidity
                      << ", L=" << data.light << ", ID=" << data.sensor_id << std::endl;

            // Analisis awal
            std::vector<std::string> anomalies = anomaly_detector.checkAnomaly(data);
            if (!anomalies.empty()) {
                for (const auto& desc : anomalies) {
                    std::cout << "[PERINGATAN CEPAT] " << desc << " - Dari: " << client_ip << std::endl;
                }
            }

            data_handler.addSensorData(data);

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
            err_msg += e.what(); // e.what() bisa berisi nama key
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

        data_handler.saveToBinaryPeriodic();
        data_handler.exportToJsonPeriodic();

        // Contoh Pencarian Anomali Historis Periodik dengan Pengurutan
        if (server_running) { 
            std::cout << "\n[SERVER TASK] Mencari anomali historis (diurut berdasarkan deviasi menurun)..." << std::endl;
            auto all_data = data_handler.getAllSensorData(); // Mungkin perlu optimasi jika data sangat besar
            
            // Cari dan urutkan berdasarkan deviasi, dari yang paling parah
            auto historical_anomalies_by_dev = anomaly_detector.searchHistoricalAnomalies(
                all_data, "", "", "deviation", true); // true untuk descending
            
            if (!historical_anomalies_by_dev.empty()) {
                std::cout << "[SERVER TASK] Anomali historis ditemukan (diurut berdasarkan deviasi menurun):" << std::endl;
                int count = 0;
                for (const auto& anom_res : historical_anomalies_by_dev) {
                    if(count++ >= 5 && historical_anomalies_by_dev.size() > 5) { // Batasi output jika terlalu banyak
                        std::cout << "  ... dan " << (historical_anomalies_by_dev.size() - 5) << " lainnya." << std::endl;
                        break;
                    }
                    std::cout << "  Timestamp: " << anom_res.data.timestamp
                              << ", Sensor: " << anom_res.data.sensor_id
                              << ", Skor Deviasi: " << std::fixed << std::setprecision(2) << anom_res.deviation_score << std::endl;
                    for(const auto& desc : anom_res.anomaly_descriptions){
                        std::cout << "    - " << desc << std::endl;
                    }
                }
            } else {
                std::cout << "[SERVER TASK] Tidak ada anomali historis yang ditemukan (berdasarkan deviasi)." << std::endl;
            }

            // Contoh: Cari dan urutkan berdasarkan timestamp, menaik
            std::cout << "\n[SERVER TASK] Mencari anomali historis (diurut berdasarkan timestamp menaik)..." << std::endl;
            auto historical_anomalies_by_ts = anomaly_detector.searchHistoricalAnomalies(
                all_data, "", "", "timestamp", false); // false untuk ascending

            if (!historical_anomalies_by_ts.empty()) {
                std::cout << "[SERVER TASK] Anomali historis ditemukan (diurut berdasarkan timestamp menaik):" << std::endl;
                 int count = 0;
                for (const auto& anom_res : historical_anomalies_by_ts) {
                     if(count++ >= 5 && historical_anomalies_by_ts.size() > 5) { // Batasi output
                        std::cout << "  ... dan " << (historical_anomalies_by_ts.size() - 5) << " lainnya." << std::endl;
                        break;
                    }
                    std::cout << "  Timestamp: " << anom_res.data.timestamp
                              << ", Sensor: " << anom_res.data.sensor_id
                              << ", Skor Deviasi: " << std::fixed << std::setprecision(2) << anom_res.deviation_score << std::endl;
                    // Tidak perlu menampilkan deskripsi lagi jika sudah ditampilkan di atas
                }
            } else {
                 std::cout << "[SERVER TASK] Tidak ada anomali historis yang ditemukan (berdasarkan timestamp)." << std::endl;
            }
            std::cout << std::endl;
        }
    }
    std::cout << "[SERVER] Thread tugas periodik dihentikan." << std::endl;
    data_handler.exportToJsonPeriodic(true); 
    data_handler.saveToBinaryPeriodic(true); 
}

int main() {
    // Menangani Ctrl+C
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler); // Untuk sinyal terminate lainnya

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

    // Mengizinkan penggunaan kembali alamat (SO_REUSEADDR)
    int optval = 1;
    #ifdef _WIN32
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
    #else
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    #endif


    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    // server_addr.sin_addr.s_addr = INADDR_ANY; // Mendengarkan di semua interface
    // Atau spesifik ke localhost:
    if (inet_pton(AF_INET, SERVER_HOST.c_str(), &server_addr.sin_addr) <= 0) {
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

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) { // SOMAXCONN untuk antrian maksimum
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

    // Jalankan thread untuk tugas periodik
    std::thread periodic_thread(periodic_tasks_thread_func);
    std::vector<std::thread> client_threads;

    // Atur timeout untuk accept() agar bisa cek server_running
    #ifdef _WIN32
        // Timeout di Windows untuk accept() sedikit lebih rumit,
        // biasanya menggunakan select() atau WSAEventSelect.
        // Untuk kesederhanaan, kita biarkan
        // Loop utama akan terus berjalan dan memeriksa server_running.
        // Jika accept() memblokir tanpa batas, server tidak akan mati dengan graceful.
        // Untuk solusi yang lebih baik, gunakan non-blocking socket dengan select/poll/epoll.
    #endif

    while (server_running) {
        sockaddr_in client_addr;
        #ifdef _WIN32
            int client_addr_len = sizeof(client_addr);
        #else
            socklen_t client_addr_len = sizeof(client_addr);
        #endif

        // Menggunakan select untuk timeout pada accept()
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_socket, &read_fds);

        timeval timeout;
        timeout.tv_sec = 1; // Cek setiap 1 detik
        timeout.tv_usec = 0;

        int select_result = select(listen_socket + 1, &read_fds, NULL, NULL, &timeout);

        if (select_result == SOCKET_ERROR) {
            #ifdef _WIN32
                std::cerr << "[SERVER] Select error: " << WSAGetLastError() << std::endl;
            #else
                perror("[SERVER] Select error");
            #endif
            break;
        }

        if (select_result == 0) { // Timeout, tidak ada koneksi baru
            continue;
        }

        // Ada koneksi masuk
        socket_t client_socket = accept(listen_socket, (sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            #ifdef _WIN32
                // Jika errornya karena server_running menjadi false, itu normal
                if (WSAGetLastError() == WSAEINTR && !server_running) {
                    std::cout << "[SERVER] Accept terinterupsi saat shutdown." << std::endl;
                    break;
                }
                std::cerr << "[SERVER] Accept gagal: " << WSAGetLastError() << std::endl;
            #else
                if (errno == EINTR && !server_running) {
                    std::cout << "[SERVER] Accept terinterupsi saat shutdown." << std::endl;
                    break;
                }
                perror("[SERVER] Accept gagal");
            #endif
            continue;
        }

        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
        client_threads.emplace_back(handle_client_connection, client_socket, std::string(client_ip_str));
    }

    // Tunggu semua thread client selesai
    for (std::thread& t : client_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Tunggu thread periodik selesai
    if (periodic_thread.joinable()) {
        periodic_thread.join();
    }

    CLOSE_SOCKET(listen_socket);
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[SERVER] Server dimatikan." << std::endl;
    return 0;
}
