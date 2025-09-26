#include "crawler.h"
#include "parser.h"
#include "task_queue.h"
#include "storage.h"
#include "indexer.h"
#include "logger.h"
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <regex>

namespace MiniSearch {

// CURL 回調函數
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

Crawler::Crawler() : m_redis_port(6379), m_running(false) {
    // 初始化 CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

Crawler::~Crawler() {
    stop();
    // 注意：不要在這裡調用 curl_global_cleanup()
    // 因為其他執行緒可能還在使用了 libcurl
}

void Crawler::setRedisConfig(const std::string& host, int port, const std::string& password) {
    m_redis_host = host;
    m_redis_port = port;
    m_redis_password = password;
}

void Crawler::start(int num_workers) {
    if (m_running) {
        LOG_WARN("爬蟲已在運行中");
        return;
    }
    
    m_running = true;
    m_workers.clear();
    
    // 啟動工作執行緒
    for (int i = 0; i < num_workers; ++i) {
        m_workers.emplace_back(&Crawler::workerThread, this);
    }
    
    LOG_INFO("爬蟲已啟動，工作執行緒數量: " + std::to_string(num_workers));
}

void Crawler::stop() {
    if (!m_running) {
        return;
    }
    
    m_running = false;
    m_queue_cv.notify_all();
    
    // 等待所有工作執行緒結束
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    m_workers.clear();
    LOG_INFO("爬蟲已停止");
}

void Crawler::addUrl(const std::string& url) {
    // 檢查是否已經訪問過
    {
        std::lock_guard<std::mutex> lock(m_visited_mutex);
        if (m_visited_urls.find(url) != m_visited_urls.end()) {
            LOG_DEBUG("URL 已訪問過，跳過: " + url);
            return;
        }
    }
    
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    m_url_queue.push(url);
    m_queue_cv.notify_one();
    
    LOG_DEBUG("添加 URL 到隊列: " + url);
}

std::vector<WebPage> Crawler::getResults() {
    std::lock_guard<std::mutex> lock(m_results_mutex);
    return m_results;
}

void Crawler::clearResults() {
    std::lock_guard<std::mutex> lock(m_results_mutex);
    m_results.clear();
}

bool Crawler::isFinished() const {
    std::lock_guard<std::mutex> queue_lock(m_queue_mutex);
    std::lock_guard<std::mutex> visited_lock(m_visited_mutex);
    
    // 完成條件：隊列為空且沒有正在處理的任務
    return m_url_queue.empty() && m_running;
}

size_t Crawler::getVisitedCount() const {
    std::lock_guard<std::mutex> lock(m_visited_mutex);
    return m_visited_urls.size();
}

size_t Crawler::getQueueSize() const {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    return m_url_queue.size();
}

void Crawler::workerThread() {
    LOG_DEBUG("工作執行緒啟動");
    
    while (m_running) {
        std::string url;
        
        // 從隊列中獲取 URL
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_queue_cv.wait(lock, [this] { return !m_url_queue.empty() || !m_running; });
            
            if (!m_running) {
                break;
            }
            
            if (!m_url_queue.empty()) {
                url = m_url_queue.front();
                m_url_queue.pop();
            }
        }
        
        if (!url.empty()) {
            // 標記為已訪問
            {
                std::lock_guard<std::mutex> lock(m_visited_mutex);
                m_visited_urls.insert(url);
            }
            
            LOG_DEBUG("開始爬取: " + url);
            WebPage page = crawlUrl(url);
            
            // 儲存結果
            {
                std::lock_guard<std::mutex> lock(m_results_mutex);
                m_results.push_back(page);
            }
            
            LOG_DEBUG("完成爬取: " + url + " (狀態: " + std::to_string(page.status_code) + ")");
        }
        
        // 短暫休息避免過度負載，多執行緒可以減少間隔
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    LOG_DEBUG("工作執行緒結束");
}

WebPage Crawler::crawlUrl(const std::string& url) {
    WebPage page;
    page.url = url;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        page.error_message = "無法初始化 CURL";
        return page;
    }
    
    std::string response_body;
    
    // 設定 CURL 選項
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MiniSearchBot/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // 執行請求
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        // 獲取 HTTP 狀態碼
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        page.status_code = static_cast<int>(http_code);
        
        if (page.status_code == 200) {
            page.content = response_body;
            page.title = extractTitle(response_body);
            page.links = extractLinks(response_body, url);
        } else {
            page.error_message = "HTTP 錯誤: " + std::to_string(page.status_code);
        }
    } else {
        page.status_code = -1;
        page.error_message = "CURL 錯誤: " + std::string(curl_easy_strerror(res));
    }
    
    curl_easy_cleanup(curl);
    return page;
}

std::string Crawler::extractContent(const std::string& html) {
    Parser parser;
    ParsedPage parsed = parser.parse(html);
    return parsed.text_content;
}

std::string Crawler::extractTitle(const std::string& html) {
    Parser parser;
    ParsedPage parsed = parser.parse(html);
    return parsed.title;
}

std::vector<std::string> Crawler::extractLinks(const std::string& html, const std::string& base_url) {
    Parser parser;
    ParsedPage parsed = parser.parse(html, base_url);
    return parsed.links;
}

} // namespace MiniSearch
