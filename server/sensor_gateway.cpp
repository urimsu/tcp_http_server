#include <iostream>
#include <cstring>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <random>

void sendMeasurements(int client_fd, const std::string& ip, int port) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    double temperature = std::uniform_int_distribution<>(-10, 40)(gen);
    double humidity = std::uniform_int_distribution<>(10, 100)(gen);
    double light = std::uniform_int_distribution<>(0, 100)(gen);
    double smoke = std::uniform_int_distribution<>(0, 1000)(gen);

    std::string body = "temperature=" + std::to_string(temperature) +
                       "&humidity=" + std::to_string(humidity) +
                       "&light=" + std::to_string(light) +
                       "&smoke=" + std::to_string(smoke);

    std::string request =
        "POST /sensor HTTP/1.1\r\n"
        "Host: " + ip + ":" + std::to_string(port) + "\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: keep-alive\r\n"
        "\r\n" +
        body;

    if (send(client_fd, request.c_str(), request.size(), 0) < 0) {
        perror("Error sending HTTP request");
        return;
    }
    std::cout << "HTTP POST sent\n\n";

    // Receive the HTTP response from the server
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        if (bytes < 0) perror("Error receiving HTTP response");
        else std::cout << "Server disconnected\n";
        return;
    }

    std::cout << "Server response: " << buffer << "\n";
}


int main() {
    int port = 8080;
    std::string ip = "0.0.0.0";

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (client_fd < 0) {
        perror("Error creating socket");
        return 1;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &server_address.sin_addr) <= 0) {
        std::cerr << "Invalid server address\n";
        close(client_fd);
        return 1;
    }

    if (connect(client_fd, (sockaddr*)&server_address, sizeof(server_address)) < 0) {
        perror("Error connecting to server");
        close(client_fd);
        return 1;
    }

    std::cout << "Connected to server!\n";

    while (true) {
        sendMeasurements(client_fd, ip, port);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    close(client_fd);
    return 0;

}

