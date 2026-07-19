#include "IpcServer.hpp"
#include "CommandBridge.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using NativeSocket = SOCKET;
using SocketLength = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
using SocketLength = socklen_t;
#define closesocket ::close
#endif

namespace {

using SocketHandle = std::intptr_t;
constexpr SocketHandle kInvalidSocket = static_cast<SocketHandle>(-1);
constexpr size_t kMaxRequestBytes = 1024 * 1024;

NativeSocket toNativeSocket(SocketHandle handle)
{
    return static_cast<NativeSocket>(handle);
}

SocketHandle toSocketHandle(NativeSocket socket)
{
    return static_cast<SocketHandle>(socket);
}

bool isInvalidSocket(NativeSocket socket)
{
#ifdef _WIN32
    return socket == INVALID_SOCKET;
#else
    return socket < 0;
#endif
}

bool initSockets()
{
#ifdef _WIN32
    WSADATA wsa{};
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

void shutdownAndClose(SocketHandle handle)
{
    if (handle == kInvalidSocket) return;

    const NativeSocket socket = toNativeSocket(handle);
#ifdef _WIN32
    ::shutdown(socket, SD_BOTH);
#else
    ::shutdown(socket, SHUT_RDWR);
#endif
    closesocket(socket);
}

SocketHandle createListenSocket(int port)
{
    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (isInvalidSocket(socket)) return kInvalidSocket;

    int opt = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0
        || ::listen(socket, 1) != 0) {
        closesocket(socket);
        return kInvalidSocket;
    }

    return toSocketHandle(socket);
}

bool sendAll(SocketHandle handle, const std::string& payload)
{
    const NativeSocket socket = toNativeSocket(handle);
    size_t sent = 0;
    while (sent < payload.size()) {
        const size_t remaining = payload.size() - sent;
        const int chunkSize = static_cast<int>(std::min<size_t>(remaining, 64 * 1024));
        const int count = ::send(socket, payload.data() + sent, chunkSize, 0);
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

nlohmann::json makeError(const nlohmann::json& id,
                         const std::string& code,
                         const std::string& message)
{
    return {
        {"id", id},
        {"result", nullptr},
        {"error", {{"code", code}, {"message", message}}}
    };
}

} // namespace

IpcServer::IpcServer() = default;
IpcServer::~IpcServer() { stop(); }

bool IpcServer::start(int port)
{
    if (running_.load()) return true;
    if (port < 1 || port > 65535) {
        std::cerr << "[IpcServer] Invalid port: " << port << "\n";
        return false;
    }

    if (!initSockets()) {
        std::cerr << "[IpcServer] Socket initialization failed\n";
        return false;
    }
    socketsInitialized_ = true;

    const SocketHandle listenSocket = createListenSocket(port);
    if (listenSocket == kInvalidSocket) {
        std::cerr << "[IpcServer] Failed to listen on 127.0.0.1:" << port << "\n";
        cleanupSockets();
        socketsInitialized_ = false;
        return false;
    }

    listenSocket_.store(listenSocket);
    running_.store(true);
    serverThread_ = std::make_unique<std::thread>(&IpcServer::serverThreadFunc, this);
    std::cout << "[IpcServer] Listening on 127.0.0.1:" << port << "\n";
    return true;
}

void IpcServer::stop()
{
    running_.store(false);

    shutdownAndClose(clientSocket_.exchange(kInvalidSocket));
    shutdownAndClose(listenSocket_.exchange(kInvalidSocket));

    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }
    serverThread_.reset();

    if (socketsInitialized_) {
        cleanupSockets();
        socketsInitialized_ = false;
    }
}

void IpcServer::serverThreadFunc()
{
    while (running_.load()) {
        const SocketHandle listenHandle = listenSocket_.load();
        if (listenHandle == kInvalidSocket) break;

        sockaddr_in clientAddr{};
        SocketLength addrLength = sizeof(clientAddr);
        const NativeSocket accepted = ::accept(
            toNativeSocket(listenHandle),
            reinterpret_cast<sockaddr*>(&clientAddr),
            &addrLength);

        if (isInvalidSocket(accepted)) {
            if (running_.load()) {
                std::cerr << "[IpcServer] accept failed\n";
            }
            break;
        }

        const SocketHandle clientHandle = toSocketHandle(accepted);
        clientSocket_.store(clientHandle);
        std::cout << "[IpcServer] Client connected\n";
        handleClient(clientHandle);

        const SocketHandle ownedClient = clientSocket_.exchange(kInvalidSocket);
        if (ownedClient != kInvalidSocket) {
            shutdownAndClose(ownedClient);
        }
        std::cout << "[IpcServer] Client disconnected\n";
    }

    const SocketHandle ownedListen = listenSocket_.exchange(kInvalidSocket);
    if (ownedListen != kInvalidSocket) {
        shutdownAndClose(ownedListen);
    }
    running_.store(false);
    std::cout << "[IpcServer] Stopped\n";
}

void IpcServer::handleClient(SocketHandle clientHandle)
{
    std::string buffer;
    char chunk[4096];

    while (running_.load()) {
        const int count = ::recv(toNativeSocket(clientHandle), chunk,
                                 static_cast<int>(sizeof(chunk)), 0);
        if (count <= 0) break;

        buffer.append(chunk, static_cast<size_t>(count));
        if (buffer.size() > kMaxRequestBytes && buffer.find('\n') == std::string::npos) {
            const auto response = makeError(nullptr, "request_too_large",
                                            "Request exceeds 1 MiB").dump() + "\n";
            sendAll(clientHandle, response);
            break;
        }

        size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            nlohmann::json response;
            try {
                const auto request = nlohmann::json::parse(line);
                if (!request.is_object()) {
                    response = makeError(nullptr, "invalid_request", "Request must be a JSON object");
                } else if (!request.contains("id") || !request["id"].is_number_unsigned()) {
                    response = makeError(nullptr, "invalid_request", "id must be an unsigned integer");
                } else if (!request.contains("method") || !request["method"].is_string()
                           || request["method"].get_ref<const std::string&>().empty()) {
                    response = makeError(request["id"], "invalid_request",
                                         "method must be a non-empty string");
                } else if (request.contains("params") && !request["params"].is_object()) {
                    response = makeError(request["id"], "invalid_request", "params must be an object");
                } else {
                    const uint64_t id = request["id"].get<uint64_t>();
                    const auto params = request.value("params", nlohmann::json::object());
                    const auto result = CommandBridge::instance().dispatch(
                        id, request["method"].get<std::string>(), params);

                    response["id"] = id;
                    if (result.contains("error")) {
                        response["result"] = nullptr;
                        response["error"] = result["error"];
                    } else {
                        response["result"] = result;
                        response["error"] = nullptr;
                    }
                }
            } catch (const nlohmann::json::parse_error& error) {
                response = makeError(nullptr, "parse_error", error.what());
            } catch (const std::exception& error) {
                response = makeError(nullptr, "internal_error", error.what());
            }

            if (!sendAll(clientHandle, response.dump() + "\n")) return;
        }
    }
}
