#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

// 一个待执行的命令：携带参数 + 返回结果的 promise
struct McpCommand {
    uint64_t                          id;
    std::string                       method;
    nlohmann::json                    params;
    std::shared_ptr<std::promise<nlohmann::json>> result;
};

class CommandBridge {
public:
    static CommandBridge& instance();

    using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

    // IPC 线程调用：投递命令并阻塞等待结果（带超时）
    nlohmann::json dispatch(uint64_t id, const std::string& method,
                            const nlohmann::json& params,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // 主线程 gameLoop 每帧调用：取出并执行所有排队命令
    void drainQueue();

    // 注册命令处理函数（method → handler），handler 在主线程执行
    void registerHandler(const std::string& method, Handler h);

    bool enabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

    // 注册一组内置 handler（ping 等）
    void registerBuiltinHandlers();

private:
    CommandBridge() = default;
    ~CommandBridge() = default;
    CommandBridge(const CommandBridge&) = delete;
    CommandBridge& operator=(const CommandBridge&) = delete;

    bool                                       enabled_ = false;
    std::mutex                                 mtx_;
    std::condition_variable                    cv_;
    std::deque<McpCommand>                     queue_;
    std::unordered_map<std::string, Handler>   handlers_;
    static constexpr int kDefaultTimeoutMs = 5000;
};