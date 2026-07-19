#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    bool start(int port = 9527);
    void stop();
    bool isRunning() const { return running_; }

private:
    using SocketHandle = std::intptr_t;
    static constexpr SocketHandle kInvalidSocket = static_cast<SocketHandle>(-1);

    void serverThreadFunc();
    void handleClient(SocketHandle clientSocket);

    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> serverThread_;
    std::atomic<SocketHandle> listenSocket_{kInvalidSocket};
    std::atomic<SocketHandle> clientSocket_{kInvalidSocket};
    bool socketsInitialized_ = false;
};
