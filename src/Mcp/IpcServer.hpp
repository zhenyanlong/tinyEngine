#pragma once
#include <atomic>
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
    void serverThreadFunc(int port);

    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> serverThread_;
    int listenSocket_ = -1;  // SOCKET on Windows, int on POSIX
};