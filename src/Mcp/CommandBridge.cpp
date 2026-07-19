#include "CommandBridge.hpp"
#include <iostream>

CommandBridge& CommandBridge::instance()
{
    static CommandBridge bridge;
    return bridge;
}

nlohmann::json CommandBridge::dispatch(uint64_t id, const std::string& method,
                                        const nlohmann::json& params,
                                        std::chrono::milliseconds timeout)
{
    if (!enabled()) {
        return {
            {"error", {{"code", "bridge_disabled"}, {"message", "CommandBridge is not enabled"}}}
        };
    }

    auto prom = std::make_shared<std::promise<nlohmann::json>>();
    std::future<nlohmann::json> fut = prom->get_future();

    {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push_back(McpCommand{id, method, params, std::move(prom)});
    }
    cv_.notify_one();

    if (fut.wait_for(timeout) == std::future_status::timeout) {
        return {
            {"error", {{"code", "timeout"}, {"message", "handler execution timed out"}}}
        };
    }

    return fut.get();
}

void CommandBridge::drainQueue()
{
    if (!enabled()) return;

    std::deque<McpCommand> localQueue;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        localQueue.swap(queue_);
    }

    for (auto& cmd : localQueue) {
        nlohmann::json result;
        auto it = handlers_.find(cmd.method);
        if (it == handlers_.end()) {
            result = {
                {"error", {{"code", "unknown_method"}, {"message", "Unknown method: " + cmd.method}}}
            };
        } else {
            try {
                result = it->second(cmd.params);
            } catch (const std::exception& e) {
                result = {
                    {"error", {{"code", "handler_error"}, {"message", std::string(e.what())}}}
                };
            }
        }
        cmd.result->set_value(std::move(result));
    }
}

void CommandBridge::registerHandler(const std::string& method, Handler h)
{
    handlers_[method] = std::move(h);
}

void CommandBridge::registerBuiltinHandlers()
{
    registerHandler("ping", [](const nlohmann::json&) -> nlohmann::json {
        return {{"pong", true}};
    });
}
