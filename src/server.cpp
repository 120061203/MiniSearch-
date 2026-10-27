#include "server.h"
#include "logger.h"
#include "json_storage.h"
#include <httplib.h>
#include <sstream>
#include <thread>
#include <algorithm>

namespace MiniSearch {

// 搜尋伺服器實現類
class SearchServer::ServerImpl {
public:
    ServerImpl() : m_server(nullptr), m_indexer(nullptr), m_storage(nullptr) {}
    
    ~ServerImpl() {
        if (m_server) {
            try {
                LOG_INFO("正在停止 HTTP 伺服器...");
                m_server->stop(); // First stop the server
                LOG_INFO("HTTP 伺服器已停止");
            } catch (const std::exception& e) {
                LOG_ERROR("停止 HTTP 伺服器時發生錯誤: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("停止 HTTP 伺服器時發生未知異常");
            }
            
            try {
                delete m_server;
                m_server = nullptr;
                LOG_INFO("HTTP 伺服器資源已釋放");
            } catch (const std::exception& e) {
                LOG_ERROR("釋放 HTTP 伺服器資源時發生錯誤: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("釋放 HTTP 伺服器資源時發生未知異常");
            }
        }
    }
    
    bool start(const std::string& host, int port, 
               std::shared_ptr<JsonIndexer> indexer,
               std::shared_ptr<JsonStorage> storage) {
        m_indexer = indexer;
        m_storage = storage;
        
        m_server = new httplib::Server();
        
        // 設定路由
        setupRoutes();
        
        // 啟動伺服器
        LOG_INFO("啟動 HTTP 伺服器: " + host + ":" + std::to_string(port));
        return m_server->listen(host.c_str(), port);
    }
    
    void stop() {
        if (m_server) {
            m_server->stop();
        }
    }

private:
    httplib::Server* m_server;
    std::shared_ptr<JsonIndexer> m_indexer;
    std::shared_ptr<JsonStorage> m_storage;
    
    void setupRoutes() {
        // 設定 CORS 標頭的中間件
        m_server->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            return httplib::Server::HandlerResponse::Unhandled;
        });
        
        // 處理 OPTIONS 請求
        m_server->Options(".*", [](const httplib::Request&, httplib::Response& res) {
            return;
        });
        
        // 健康檢查端點
        m_server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("OK", "text/plain");
        });
        
        // 搜尋端點
        m_server->Get("/search", [this](const httplib::Request& req, httplib::Response& res) {
            handleSearch(req, res);
        });
        
        // 統計端點
        m_server->Get("/stats", [this](const httplib::Request& req, httplib::Response& res) {
            handleStats(req, res);
        });
        
        // 頁面詳情端點
        m_server->Get("/page/(\\d+)", [this](const httplib::Request& req, httplib::Response& res) {
            handlePageDetail(req, res);
        });
        
        // 網域統計端點
        m_server->Get("/domain/([^/]+)", [this](const httplib::Request& req, httplib::Response& res) {
            handleDomainStats(req, res);
        });
        
        // 根路徑 - API 文件
        m_server->Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(getApiDocumentation(), "text/html");
        });
    }
    
    void handleSearch(const httplib::Request& req, httplib::Response& res) {
        try {
            std::string query = req.get_param_value("q");
            if (query.empty()) {
                res.status = 400;
                res.set_content("{\"error\": \"缺少查詢參數 q\"}", "application/json");
                return;
            }
            
            int limit = 10;
            int offset = 0;
            
            if (req.has_param("limit")) {
                limit = std::stoi(req.get_param_value("limit"));
                limit = std::max(1, std::min(100, limit)); // 限制在 1-100 之間
            }
            
            if (req.has_param("offset")) {
                offset = std::stoi(req.get_param_value("offset"));
                offset = std::max(0, offset);
            }
            
            LOG_INFO("搜尋查詢: " + query + " (limit=" + std::to_string(limit) + ", offset=" + std::to_string(offset) + ")");
            
            auto documents = m_indexer->search(query, limit, offset);
            
            // 建立簡單的 JSON 回應
            std::stringstream json_response;
            json_response << "{";
            json_response << "\"query\":\"" << escapeJson(query) << "\",";
            json_response << "\"total_count\":" << documents.size() << ",";
            json_response << "\"search_time_ms\":0,";
            json_response << "\"limit\":" << limit << ",";
            json_response << "\"offset\":" << offset << ",";
            json_response << "\"documents\":[";
            
            for (size_t i = 0; i < documents.size(); ++i) {
                const auto& doc = documents[i];
                if (i > 0) json_response << ",";
                
                json_response << "{";
                json_response << "\"id\":" << doc.id << ",";
                json_response << "\"url\":\"" << escapeJson(doc.url) << "\",";
                json_response << "\"title\":\"" << escapeJson(doc.title) << "\",";
                json_response << "\"score\":" << doc.score << ",";
                
                // Google 風格的摘要
                json_response << "\"snippet\":\"" << escapeJson(doc.snippet) << "\",";
                json_response << "\"highlighted_title\":\"" << escapeJson(doc.highlighted_title) << "\",";
                json_response << "\"highlighted_snippet\":\"" << escapeJson(doc.highlighted_snippet) << "\"";
                json_response << "}";
            }
            
            json_response << "]}";
            
            res.set_content(json_response.str(), "application/json");
            
        } catch (const std::exception& e) {
            LOG_ERROR("搜尋錯誤: " + std::string(e.what()));
            res.status = 500;
            res.set_content("{\"error\": \"搜尋服務錯誤\"}", "application/json");
        }
    }
    
    void handleStats(const httplib::Request&, httplib::Response& res) {
        try {
            std::stringstream stats;
            stats << "{";
            stats << "\"document_count\":" << m_indexer->getDocumentCount() << ",";
            stats << "\"term_count\":" << m_indexer->getTermCount() << ",";
            stats << "\"page_count\":" << m_storage->getPageCount();
            stats << "}";
            
            res.set_content(stats.str(), "application/json");
            
        } catch (const std::exception& e) {
            LOG_ERROR("統計錯誤: " + std::string(e.what()));
            res.status = 500;
            res.set_content("{\"error\": \"統計服務錯誤\"}", "application/json");
        }
    }
    
    void handlePageDetail(const httplib::Request& req, httplib::Response& res) {
        try {
            std::string page_id_str = req.matches[1];
            int page_id = std::stoi(page_id_str);
            
            StoredPage page = m_storage->getPage(page_id);
            if (page.id == 0) {
                res.status = 404;
                res.set_content("{\"error\": \"頁面不存在\"}", "application/json");
                return;
            }
            
            std::stringstream page_json;
            page_json << "{";
            page_json << "\"id\":" << page.id << ",";
            page_json << "\"url\":\"" << escapeJson(page.url) << "\",";
            page_json << "\"title\":\"" << escapeJson(page.title) << "\",";
            page_json << "\"content\":\"" << escapeJson(page.content) << "\",";
            page_json << "\"text_content\":\"" << escapeJson(page.text_content) << "\",";
            page_json << "\"created_at\":\"" << escapeJson(page.created_at) << "\",";
            page_json << "\"updated_at\":\"" << escapeJson(page.updated_at) << "\"";
            page_json << "}";
            
            res.set_content(page_json.str(), "application/json");
            
        } catch (const std::exception& e) {
            LOG_ERROR("頁面詳情錯誤: " + std::string(e.what()));
            res.status = 500;
            res.set_content("{\"error\": \"頁面服務錯誤\"}", "application/json");
        }
    }
    
    void handleDomainStats(const httplib::Request& req, httplib::Response& res) {
        try {
            std::string domain = req.matches[1];
            
            std::stringstream domain_stats;
            domain_stats << "{";
            domain_stats << "\"domain\":\"" << escapeJson(domain) << "\",";
            domain_stats << "\"page_count\":" << m_storage->getPageCount() << ",";
            domain_stats << "\"pages\":[";
            
            std::vector<StoredPage> pages = m_storage->getAllPages();
            for (size_t i = 0; i < pages.size(); ++i) {
                const auto& page = pages[i];
                if (i > 0) domain_stats << ",";
                
                domain_stats << "{";
                domain_stats << "\"id\":" << page.id << ",";
                domain_stats << "\"url\":\"" << escapeJson(page.url) << "\",";
                domain_stats << "\"title\":\"" << escapeJson(page.title) << "\",";
                domain_stats << "\"created_at\":\"" << escapeJson(page.created_at) << "\"";
                domain_stats << "}";
            }
            
            domain_stats << "]}";
            res.set_content(domain_stats.str(), "application/json");
            
        } catch (const std::exception& e) {
            LOG_ERROR("網域統計錯誤: " + std::string(e.what()));
            res.status = 500;
            res.set_content("{\"error\": \"網域服務錯誤\"}", "application/json");
        }
    }
    
    std::string escapeJson(const std::string& str) {
        std::string escaped;
        for (char c : str) {
            switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c; break;
            }
        }
        return escaped;
    }
    
    static std::string getApiDocumentation() {
        return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>迷你搜尋引擎 API</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .endpoint { background: #f5f5f5; padding: 10px; margin: 10px 0; border-radius: 5px; }
        .method { color: #007bff; font-weight: bold; }
        .url { color: #28a745; font-family: monospace; }
    </style>
</head>
<body>
    <h1>迷你搜尋引擎 API 文件</h1>
    
    <div class="endpoint">
        <div class="method">GET</div>
        <div class="url">/search?q=關鍵字&limit=10&offset=0</div>
        <p>搜尋功能。參數：q (查詢關鍵字), limit (結果數量，1-100), offset (偏移量)</p>
    </div>
    
    <div class="endpoint">
        <div class="method">GET</div>
        <div class="url">/stats</div>
        <p>獲取系統統計資訊</p>
    </div>
    
    <div class="endpoint">
        <div class="method">GET</div>
        <div class="url">/page/{id}</div>
        <p>獲取特定頁面的詳細資訊</p>
    </div>
    
    <div class="endpoint">
        <div class="method">GET</div>
        <div class="url">/domain/{domain}</div>
        <p>獲取特定網域的統計資訊</p>
    </div>
    
    <div class="endpoint">
        <div class="method">GET</div>
        <div class="url">/health</div>
        <p>健康檢查</p>
    </div>
</body>
</html>
        )";
    }
};

SearchServer::SearchServer() : m_impl(std::make_unique<ServerImpl>()), m_host("localhost"), m_port(8080) {
}

SearchServer::~SearchServer() = default;

void SearchServer::setHost(const std::string& host) {
    m_host = host;
}

void SearchServer::setPort(int port) {
    m_port = port;
}

void SearchServer::setIndexer(std::shared_ptr<JsonIndexer> indexer) {
    m_indexer = indexer;
}

void SearchServer::setStorage(std::shared_ptr<JsonStorage> storage) {
    m_storage = storage;
}

bool SearchServer::start() {
    if (!m_indexer || !m_storage) {
        LOG_ERROR("索引器或儲存系統未設定");
        return false;
    }
    
    m_running = true;
    m_server_thread = std::thread([this]() {
        m_impl->start(m_host, m_port, m_indexer, m_storage);
    });
    
    return true;
}

void SearchServer::stop() {
    m_running = false;
    m_impl->stop();
    
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
}

bool SearchServer::isRunning() const {
    return m_running;
}

void SearchServer::wait() {
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
}

} // namespace MiniSearch
