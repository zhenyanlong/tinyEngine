#include "IpcServer.hpp"
#include "CommandBridge.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
#define closesocket ::close
#endif

namespace {

bool initSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int createListenSocket(int port)
{
    int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return -1;

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closesocket(fd);
        return -1;
    }

    if (::listen(fd, 1) < 0) {
        closesocket(fd);
        return -1;
    }

    return fd;
}

void handleClient(int clientFd)
{
    std::string buffer;
    char buf[4096];

    while (true) {
        int n = static_cast<int>(::recv(clientFd, buf, sizeof(buf) - 1, 0));
        if (n <= 0) break;

        buf[n] = '\0';
        buffer.append(buf, static_cast<size_t>(n));

        // 按 \n 分隔处理每一行 JSON
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.empty()) continue;

            try {
                auto req = nlohmann::json::parse(line);
                uint64_t id = req.value("id", uint64_t(0));
                std::string method = req.value("method", "");
                auto params = req.value("params", nlohmann::json::object());

                auto result = CommandBridge::instance().dispatch(id, method, params);

                nlohmann::json response;
                response["id"] = id;
                if (result.contains("error")) {
                    response["error"] = result["error"];
                    response["result"] = nullptr;
                } else {
                    response["result"] = result;
                    response["error"] = nullptr;
                }

                std::string respStr = response.dump() + "\n";
                ::send(clientFd, respStr.c_str(), static_cast<int>(respStr.size()), 0);

            } catch (const nlohmann::json::parse_error& e) {
                nlohmann::json errResp = {
                    {"id", nullptr},
                    {"result", nullptr},
                    {"error", {{"code", "parse_error"}, {"message", std::string(e.what())}}}
                };
                std::string respStr = errResp.dump() + "\n";
                ::send(clientFd, respStr.c_str(), static_cast<int>(respStr.size()), 0);
            }
        }
    }

    closesocket(clientFd);
}

} // namespace

IpcServer::IpcServer() = default;
IpcServer::~IpcServer() { stop(); }

bool IpcServer::start(int port)
{
    if (running_) return true;
    if (!initSockets()) return false;

    running_ = true;
    serverThread_ = std::make_unique<std::thread>(&IpcServer::serverThreadFunc, this, port);
    return true;
}

void IpcServer::stop()
{
    running_ = false;

    if (listenSocket_ >= 0) {
        closesocket(listenSocket_);
        listenSocket_ = -1;
    }

    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }
    serverThread_.reset();

    cleanupSockets();
}

void IpcServer::serverThreadFunc(int port)
{
    listenSocket_ = createListenSocket(port);
    if (listenSocket_ < 0) {
        std::cerr << "[IpcServer] Failed to create listen socket on port " << port << "\n";
        running_ = false;
        return;
    }

    std::cout << "[IpcServer] Listening on 127.0.0.1:" << port << "\n";

    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = static_cast<int>(::accept(listenSocket_,
            reinterpret_cast<sockaddr*>(&clientAddr), &addrLen));
        if (clientFd < 0) {
            if (running_) {
                std::cerr << "[IpcServer] accept failed\n";
            }
            break;
        }

        std::cout << "[IpcServer] Client connected\n";
        handleClient(clientFd);
        std::cout << "[IpcServer] Client disconnected\n";
    }

    if (listenSocket_ >= 0) {
        closesocket(listenSocket_);
        listenSocket_ = -1;
    }

    std::cout << "[IpcServer] Stopped\n";
}