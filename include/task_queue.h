#pragma once

#include <string>
#include <vector>
#include <memory>

namespace MiniSearch {

class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();
    
    // 連接 Redis
    bool connect(const std::string& host = "localhost", int port = 6379, const std::string& password = "");
    void disconnect();
    
    // 任務隊列操作
    bool pushUrl(const std::string& url);
    std::string popUrl();
    std::vector<std::string> popUrls(int count = 10);
    
    // 檢查 URL 是否已處理
    bool isUrlProcessed(const std::string& url);
    void markUrlProcessed(const std::string& url);
    
    // 獲取隊列狀態
    size_t getQueueSize();
    size_t getProcessedCount();
    
    // 清空隊列
    void clearQueue();
    void clearProcessed();

private:
    class RedisClient;
    std::unique_ptr<RedisClient> m_redis;
    
    static const std::string QUEUE_KEY;
    static const std::string PROCESSED_KEY;
};

} // namespace MiniSearch
