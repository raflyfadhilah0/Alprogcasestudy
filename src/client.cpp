#include "common.h" // Untuk konfigurasi dan getCurrentTimestamp
#include "nlohmann/json.hpp"

#include <iostream>
#include <string>
#include <thread> // Untuk std::this_thread::sleep_for
#include <chrono>
#include <random> // Untuk data acak
#include <vector> // Untuk buffer

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

// Generator data sensor acak (lebih baik daripada yang di Python)
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

double random_double(double min_val, double max_val) {
    std::uniform_real_distribution<double> dist(min_val, max_val);
    return dist(rng);
}

SensorData generate_sensor_data(const std::string& client_id_str) {
    SensorData data;
    data.timestamp = getCurrentTimestamp();
    data.sensor_id = client_id_str;

    // Data normal
    data.temperature = random_double(22.0, 25.0) + random_double(-1.0, 1.0);
    data.humidity = random_double(45.0, 55.0) + random_double(-5.0, 5.0);
    data.light = static_cast<int>(random_double(400.0, 700.0) + random_double(-50.0, 50.0));

    // Sesekali anomali
    if (random_double(0.0, 1.0) < 0.1) { // 10% anomali suhu
        data.temperature = (random_double(0.0, 1.0) < 0.5) ? random_double(28.0, 35.0) : random_double(15.0, 19.0);
        std::cout << "[" << client_id_str << "] Mensimulasikan anomali suhu!" << std::endl;
    }
    if (random_double(0.0, 1.0) < 0.05) { // 5% anomali kelembapan
        data.humidity = (random_double(0.0, 1.0) < 0.5) ? random_double(70.0, 90.0) : random_double(20.0, 30.0);
        std::cout << "[" << client_id_str << "] Mensimulasikan anomali kelembapan!" << std::endl;
    }
    return data;
}


int main(int argc, char *argv[]) {
    std::string client_id = "CppClient-01";
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

    socket_t client_socket = INVALID_SOCKET;
    bool connected = false;
    int retry_delay_s = 5;

    while (true) { // Loop untuk mencoba koneksi ulang
        if (!connected) {
            client_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket == INVALID_SOCKET) {
                #ifdef _WIN32
                    std::cerr << "[" << client_id << "] Error membuat socket: " << WSAGetLastError() << std::endl;
                #else
                    perror("Error membuat socket");
                #endif
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s));
                continue;
            }

            sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(SERVER_PORT);
            if (inet_pton(AF_INET, SERVER_HOST.c_str(), &server_addr.sin_addr) <= 0) {
                 std::cerr << "[" << client_id << "] inet_pton error untuk " << SERVER_HOST << std::endl;
                 CLOSE_SOCKET(client_socket);
                 std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s));
                 continue;
            }


            std::cout << "[" << client_id << "] Mencoba terhubung ke server " << SERVER_HOST << ":" << SERVER_PORT << "..." << std::endl;
            if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
                #ifdef _WIN32
                    std::cerr << "[" << client_id << "] Gagal terhubung ke server: " << WSAGetLastError() << ". Mencoba lagi dalam " << retry_delay_s << " detik." << std::endl;
                #else
                    perror(("[" + client_id + "] Gagal terhubung ke server. Mencoba lagi...").c_str());
                #endif
                CLOSE_SOCKET(client_socket);
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_s));
                continue;
            }
            std::cout << "[" << client_id << "] Terhubung ke server." << std::endl;
            connected = true;
        }

        SensorData s_data = generate_sensor_data(client_id);
        json j_data;
        j_data["timestamp"] = s_data.timestamp;
        j_data["temperature"] = s_data.temperature;
        j_data["humidity"] = s_data.humidity;
        j_data["light"] = s_data.light;
        j_data["sensor_id"] = s_data.sensor_id;

        std::string message = j_data.dump(); // Serialize JSON ke string

        int bytes_sent = send(client_socket, message.c_str(), message.length(), 0);
        if (bytes_sent == SOCKET_ERROR) {
            #ifdef _WIN32
                std::cerr << "[" << client_id << "] Error mengirim data: " << WSAGetLastError() << ". Koneksi mungkin terputus." << std::endl;
            #else
                perror(("[" + client_id + "] Error mengirim data").c_str());
            #endif
            CLOSE_SOCKET(client_socket);
            connected = false; // Set untuk mencoba koneksi ulang
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Tunggu sebentar sebelum mencoba lagi
            continue;
        }
        std::cout << "[" << client_id << "] Mengirim: T=" << s_data.temperature
                  << ", H=" << s_data.humidity << ", L=" << s_data.light << std::endl;

        // Menerima balasan (opsional)
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            std::cout << "[" << client_id << "] Menerima dari server: " << buffer << std::endl;
        } else if (bytes_received == 0) {
            std::cout << "[" << client_id << "] Server menutup koneksi." << std::endl;
            CLOSE_SOCKET(client_socket);
            connected = false;
            continue;
        } else { // SOCKET_ERROR
            #ifdef _WIN32
                std::cerr << "[" << client_id << "] Error recv balasan: " << WSAGetLastError() << std::endl;
            #else
                perror(("[" + client_id + "] Error recv balasan").c_str());
            #endif
            CLOSE_SOCKET(client_socket);
            connected = false;
            continue;
        }

        std::this_thread::sleep_for(std::chrono::seconds(SENSOR_UPDATE_INTERVAL_S));
    } // Akhir while(true)

    if (client_socket != INVALID_SOCKET) {
        CLOSE_SOCKET(client_socket);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}