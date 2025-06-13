cmake_minimum_required(VERSION 3.10)
project(Finpro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # Opsional, tapi praktik yang baik

# Mengatur direktori output agar konsisten dengan launch.json dan tasks.json
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}) # Output .exe/.app ke build/

# Path ke header nlohmann/json
# Asumsikan Anda memiliki folder 'include' di root proyek Anda
# dan nlohmann/json.hpp ada di dalam 'include/nlohmann/json.hpp'
include_directories(include)

# --- Server Executable ---
add_executable(server
    src/server.cpp
    src/datahandler.cpp
    src/anomalydetector.cpp
    # common.h di-include, tidak perlu ditambahkan jika bukan file .cpp
)

# --- Client Executable ---
add_executable(client
    src/client.cpp
)

# Link libraries (penting untuk socket dan thread)
if (WIN32)
    # Untuk Winsock
    target_link_libraries(server PRIVATE ws2_32)
    target_link_libraries(client PRIVATE ws2_32)
else()
    # Untuk POSIX threads dan networking
    # Beberapa sistem mungkin memerlukan -lsocket -lnsl tergantung konfigurasi
    target_link_libraries(server PRIVATE pthread)
    target_link_libraries(client PRIVATE pthread)
endif()

# Opsi compiler (contoh)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(server PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(client PRIVATE -Wall -Wextra -pedantic)
endif()