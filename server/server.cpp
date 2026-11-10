
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// 1. Datenstruktur
struct SensorData {
    double temperature = 0;
    double humidity = 0;
    double light = 0;
    double smoke = 0;
};

SensorData sensor_data;
std::mutex sensor_mutex;


// 2. Sensordaten aus POST-Body auslesen
bool parse_sensor_data(const std::string& body, SensorData& data) {

    std::stringstream stream(body);
    std::string field;

    bool temperature_found = false;
    bool humidity_found = false;
    bool light_found = false;
    bool smoke_found = false;

    while (std::getline(stream, field, '&')) {

        size_t pos = field.find('=');

        if (pos == std::string::npos) {
            return false;
        }

        std::string key = field.substr(0, pos);
        std::string value = field.substr(pos + 1);

        try {
            size_t consumed = 0;
            double number = std::stod(value, &consumed);

            if (consumed != value.size()) {
                return false;
            }

            if (key == "temperature") {
                data.temperature = number;
                temperature_found = true;
            }
            else if (key == "humidity") {
                data.humidity = number;
                humidity_found = true;
            }
            else if (key == "light") {
                data.light = number;
                light_found = true;
            }
            else if (key == "smoke") {
                data.smoke = number;
                smoke_found = true;
            }
        }
        catch (...) {
            return false;
        }
    }

    return temperature_found && humidity_found
        && light_found && smoke_found;
}


// 3. HTML-Dashboard erstellen
std::string create_dashboard() {

    SensorData data;

    {
        std::lock_guard<std::mutex> lock(sensor_mutex);
        data = sensor_data;
    }

    std::ostringstream html;
    html << std::fixed << std::setprecision(2);

    html << R"(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sensor Dashboard</title>
<style>
    body {
        font-family: Arial, sans-serif;
        background: #f1f5f9;
        color: #0f172a;
        padding: 40px 20px;
    }
    main {
        max-width: 900px;
        margin: auto;
    }
    .grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
        gap: 20px;
    }
    .card {
        background: white;
        border-radius: 16px;
        padding: 28px;
        box-shadow: 0 4px 16px #0000000d;
    }
    .label {
        color: #64748b;
        font-size: 15px;
    }
    .value {
        font-size: 32px;
        font-weight: bold;
        margin-top: 14px;
    }
</style>
</head>
<body>
<main>
<h1>Sensor Dashboard</h1>
<p>Aktuell gespeicherte Sensordaten</p>
<div class="grid">
)";

    html << "<div class='card'><div class='label'>Temperatur</div>"
         << "<div class='value'>" << data.temperature
         << " &deg;C</div></div>";

    html << "<div class='card'><div class='label'>Luftfeuchtigkeit</div>"
         << "<div class='value'>" << data.humidity
         << " %</div></div>";

    html << "<div class='card'><div class='label'>Helligkeit</div>"
         << "<div class='value'>" << data.light
         << "</div></div>";

    html << "<div class='card'><div class='label'>Rauchpartikel</div>"
         << "<div class='value'>" << data.smoke
         << "</div></div>";

    html << R"(
</div>
</main>
</body>
</html>
)";

    return html.str();
}


// 4. HTTP-Anfrage verarbeiten
std::string process_request(
    const std::string& method,
    const std::string& path,
    const std::string& request_body,
    std::string& status
) {

    if (path != "/" && path != "/sensor") {
    status = "404 Not Found";
    return "Seite nicht gefunden";
}

    if (method == "GET") {
        status = "200 OK";
        return create_dashboard();
    }

    if (method == "POST") {

        SensorData new_data{};

        if (!parse_sensor_data(request_body, new_data)) {
            status = "400 Bad Request";
            return "Ungueltige Sensordaten";
        }

        {
            std::lock_guard<std::mutex> lock(sensor_mutex);
            sensor_data = new_data;
        }

        status = "200 OK";
        return "Sensordaten gespeichert";
    }

    status = "405 Method Not Allowed";
    return "Methode nicht erlaubt";
}


// 5. HTTP-Anfrage vollständig empfangen
bool receive_request(
    int client_fd,
    std::string& pending,
    std::string& method,
    std::string& path,
    std::string& body
) {

    // Header-Ende suchen
    size_t header_end = pending.find("\r\n\r\n");

    while (header_end == std::string::npos) {

        char buffer[4096];

        ssize_t bytes = recv(
            client_fd, buffer, sizeof(buffer), 0
        );

        if (bytes <= 0) {
            return false;
        }

        pending.append(buffer, bytes);

        if (pending.size() > 65536) {
            return false;
        }

        header_end = pending.find("\r\n\r\n");
    }

    // Erste Zeile: POST /sensor HTTP/1.1
    std::string headers = pending.substr(0, header_end);
    std::istringstream stream(headers);

    std::string request_line;
    std::getline(stream, request_line);

    std::string version;
    std::istringstream first_line(request_line);
    if (!(first_line >> method >> path >> version)) {
        return false;
    }

    // Content-Length auslesen
    size_t content_length = 0;
    std::string line;

    while (std::getline(stream, line)) {

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.rfind("Content-Length:", 0) == 0) {
            try {
                content_length = std::stoul(line.substr(15));
            }
            catch (...) {
                return false;
            }
        }
    }

    if (content_length > 65536) {
        return false;
    }

    // Auf vollständigen Body warten
    size_t message_length = header_end + 4 + content_length;

    while (pending.size() < message_length) {

        char buffer[4096];

        ssize_t bytes = recv(
            client_fd, buffer, sizeof(buffer), 0
        );

        if (bytes <= 0) {
            return false;
        }

        pending.append(buffer, bytes);
    }

    body = pending.substr(header_end + 4, content_length);

    // Verarbeitete Anfrage entfernen
    pending.erase(0, message_length);

    return true;
}


// 6. HTTP-Antwort senden
bool send_response(
    int client_fd,
    const std::string& status,
    const std::string& body
) {

    std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " +
        std::to_string(body.size()) + "\r\n"
        "Connection: keep-alive\r\n"
        "\r\n" + body;

    size_t total_sent = 0;

    while (total_sent < response.size()) {

        ssize_t sent = send(
            client_fd,
            response.data() + total_sent,
            response.size() - total_sent,
            MSG_NOSIGNAL
        );

        if (sent <= 0) {
            return false;
        }

        total_sent += sent;
    }

    return true;
}


// 7. Client-Verbindung verwalten
void handle_client(int client_fd) {

    std::string pending;

    while (true) {

        std::string method;
        std::string path;
        std::string request_body;

        // Anfrage empfangen
        if (!receive_request(
            client_fd, pending, method, path, request_body
        )) {
            break;
        }

        // Anfrage verarbeiten
        std::string status;
        std::string response_body = process_request(
            method, path, request_body, status
        );

        // Antwort senden
        if (!send_response(client_fd, status, response_body)) {
            break;
        }
    }

    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        std::cerr << "Socket fehlgeschlagen\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (sockaddr*)&address,
             sizeof(address)) < 0) {
        std::cerr << "Bind fehlgeschlagen\n";
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Listen fehlgeschlagen\n";
        return 1;
    }

    std::cout << "HTTP-Server auf Port 8080\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd < 0) {
            continue;
        }

        std::thread client_thread(handle_client,client_fd);

        client_thread.detach();
    }

    close(server_fd);
    return 0;
}

