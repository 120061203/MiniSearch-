#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_set>

namespace MiniSearch {

struct WebPage {
    std::string url;
    std::string content;
    std::string title;
    std::vector<std::string> links;
    int status_code;
    std::string error_message;
    
    WebPage() : status_code(0) {}
};

class Crawler {
public:
    Crawler();
    ~Crawler();
    
    // 設定 Redis 連接參數
    void setRedisConfig(const std::string& host, int port, const std::string& password = "");
    
    // 啟動爬蟲
    void start(int num_workers = 4);
    void stop();
    
    // 添加 URL 到爬取隊列
    void addUrl(const std::string& url);
    
    // 獲取爬取結果
    std::vector<WebPage> getResults();
    
    // 清除爬取結果
    void clearResults();
    
    // 檢查是否正在運行
    bool isRunning() const { return m_running; }
    
    // 檢查是否完成爬取
    bool isFinished() const;
    
    // 獲取爬取統計
    size_t getVisitedCount() const;
    size_t getQueueSize() const;

private:
    void workerThread();
    WebPage crawlUrl(const std::string& url);
    std::string extractContent(const std::string& html);
    std::string extractTitle(const std::string& html);
    std::vector<std::string> extractLinks(const std::string& html, const std::string& base_url);
    
    std::string m_redis_host;
    int m_redis_port;
    std::string m_redis_password;
    
    std::atomic<bool> m_running;
    std::vector<std::thread> m_workers;
    std::queue<std::string> m_url_queue;
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    
    std::vector<WebPage> m_results;
    std::mutex m_results_mutex;
    
    mutable std::unordered_set<std::string> m_visited_urls;
    mutable std::mutex m_visited_mutex;
};

} // namespace MiniSearch
