#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <signal.h>
#include <curl/curl.h>
#include <execinfo.h>
#include <cxxabi.h>

#include "logger.h"
#include "crawler.h"
#include "parser.h"
#include "task_queue.h"
#include "indexer.h"
#include "storage.h"
#include "server.h"

using namespace MiniSearch;

// 全域變數用於優雅關閉
std::atomic<bool> g_running(true);
std::unique_ptr<Crawler> g_crawler;
std::unique_ptr<SearchServer> g_server;

// 堆疊追蹤函數
void printStackTrace() {
    void *array[10];
    size_t size = backtrace(array, 10);
    char **strings = backtrace_symbols(array, size);
    
    LOG_ERROR("堆疊追蹤:");
    for (size_t i = 0; i < size; i++) {
        LOG_ERROR("  " + std::string(strings[i]));
    }
    free(strings);
}

// 信號處理函數
void signalHandler(int signal) {
    LOG_ERROR("收到信號: " + std::to_string(signal));
    printStackTrace();
    
    LOG_INFO("收到關閉信號，正在停止服務...");
    g_running = false;
    
    try {
        if (g_crawler) {
            LOG_INFO("正在停止爬蟲...");
            g_crawler->stop();
            LOG_INFO("爬蟲已停止");
        }
        
        if (g_server) {
            LOG_INFO("正在關閉伺服器...");
            g_server->stop();
            LOG_INFO("伺服器已關閉");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("信號處理中發生錯誤: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("信號處理中發生未知異常");
    }
}

// 顯示使用說明
void showUsage(const char* program_name) {
    std::cout << "迷你搜尋引擎爬蟲系統 v1.0.0\n\n";
    std::cout << "使用方法:\n";
    std::cout << "  " << program_name << " [選項]\n\n";
    std::cout << "選項:\n";
    std::cout << "  --crawler              啟動爬蟲模式\n";
    std::cout << "  --server               啟動搜尋 API 伺服器\n";
    std::cout << "  --both                 同時啟動爬蟲和伺服器\n";
    std::cout << "  --workers N            設定爬蟲工作執行緒數量 (預設: 4)\n";
    std::cout << "  --redis-host HOST      Redis 主機位址 (預設: localhost)\n";
    std::cout << "  --redis-port PORT      Redis 連接埠 (預設: 6379)\n";
    std::cout << "  --server-host HOST     伺服器主機位址 (預設: localhost)\n";
    std::cout << "  --server-port PORT     伺服器連接埠 (預設: 8080)\n";
    std::cout << "  --db-path PATH         資料庫檔案路徑 (預設: ./search.db)\n";
    std::cout << "  --log-level LEVEL      日誌等級 (DEBUG|INFO|WARN|ERROR, 預設: INFO)\n";
    std::cout << "  --help                 顯示此說明\n\n";
    std::cout << "範例:\n";
    std::cout << "  " << program_name << " --crawler --workers 8\n";
    std::cout << "  " << program_name << " --server --server-port 9090\n";
    std::cout << "  " << program_name << " --both --workers 4 --server-port 8080\n";
}

// 解析命令列參數
struct Config {
    bool crawler_mode = false;
    bool server_mode = false;
    int workers = 4;
    std::string redis_host = "localhost";
    int redis_port = 6379;
    std::string server_host = "localhost";
    int server_port = 8080;
    std::string db_path = "./search.db";
    std::string log_level = "INFO";
};

Config parseArguments(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            showUsage(argv[0]);
            exit(0);
        }
        else if (arg == "--crawler") {
            config.crawler_mode = true;
        }
        else if (arg == "--server") {
            config.server_mode = true;
        }
        else if (arg == "--both") {
            config.crawler_mode = true;
            config.server_mode = true;
        }
        else if (arg == "--workers" && i + 1 < argc) {
            config.workers = std::stoi(argv[++i]);
        }
        else if (arg == "--redis-host" && i + 1 < argc) {
            config.redis_host = argv[++i];
        }
        else if (arg == "--redis-port" && i + 1 < argc) {
            config.redis_port = std::stoi(argv[++i]);
        }
        else if (arg == "--server-host" && i + 1 < argc) {
            config.server_host = argv[++i];
        }
        else if (arg == "--server-port" && i + 1 < argc) {
            config.server_port = std::stoi(argv[++i]);
        }
        else if (arg == "--db-path" && i + 1 < argc) {
            config.db_path = argv[++i];
        }
        else if (arg == "--log-level" && i + 1 < argc) {
            config.log_level = argv[++i];
        }
        else {
            std::cerr << "未知參數: " << arg << std::endl;
            showUsage(argv[0]);
            exit(1);
        }
    }
    
    // 如果沒有指定模式，預設啟動爬蟲
    if (!config.crawler_mode && !config.server_mode) {
        config.crawler_mode = true;
    }
    
    return config;
}

int main(int argc, char* argv[]) {
    // 設定信號處理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 解析命令列參數
    Config config = parseArguments(argc, argv);
    
    // 設定日誌等級
    LogLevel level = LogLevel::INFO;
    if (config.log_level == "DEBUG") level = LogLevel::DEBUG;
    else if (config.log_level == "WARN") level = LogLevel::WARN;
    else if (config.log_level == "ERROR") level = LogLevel::ERROR;
    
    Logger::getInstance().setLevel(level);
    
    LOG_INFO("迷你搜尋引擎爬蟲系統啟動中...");
    LOG_INFO("資料庫路徑: " + config.db_path);
    
    try {
        // 初始化儲存和索引器
        auto storage = std::make_shared<Storage>();
        auto indexer = std::make_shared<Indexer>();
        
        if (!storage->initialize(config.db_path)) {
            LOG_ERROR("無法初始化儲存系統");
            return 1;
        }
        
        if (!indexer->initialize(config.db_path)) {
            LOG_ERROR("無法初始化索引系統");
            return 1;
        }
        
        // 啟動爬蟲
        if (config.crawler_mode) {
            LOG_INFO("啟動爬蟲模式，工作執行緒數量: " + std::to_string(config.workers));
            
            g_crawler = std::make_unique<Crawler>();
            g_crawler->setRedisConfig(config.redis_host, config.redis_port);
            g_crawler->start(config.workers);
            
            // 添加根目錄 URL，爬蟲會自動發現其他連結
            g_crawler->addUrl("https://xsong.us/");
            
            LOG_INFO("爬蟲已啟動，按 Ctrl+C 停止");
        }
        
        // 啟動搜尋伺服器
        if (config.server_mode) {
            
            g_server = std::make_unique<SearchServer>();
            g_server->setHost(config.server_host);
            g_server->setPort(config.server_port);
            g_server->setIndexer(indexer);
            g_server->setStorage(storage);
            
            if (!g_server->start()) {
                LOG_ERROR("無法啟動搜尋伺服器");
                return 1;
            }
            
            LOG_INFO("搜尋 API 已啟動，端點: http://" + config.server_host + ":" + std::to_string(config.server_port));
        }
        // 主迴圈
        int progress_counter = 0;
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            progress_counter++;
            
            // 每 10 秒報告一次進度
            if (g_crawler && g_crawler->isRunning() && progress_counter % 10 == 0) {
                LOG_INFO("📈 爬取進度: 已訪問 " + std::to_string(g_crawler->getVisitedCount()) + 
                        " 個頁面，隊列中還有 " + std::to_string(g_crawler->getQueueSize()) + " 個待處理");
            }
            
            // 處理爬蟲結果
            if (g_crawler && g_crawler->isRunning()) {
                auto results = g_crawler->getResults();
                if (!results.empty()) {
                    LOG_INFO("處理爬蟲結果，數量: " + std::to_string(results.size()));
                    
                    // 批量處理結果，提高效率
                    std::vector<StoredPage> pages_to_save;
                    std::vector<Document> docs_to_index;
                    std::vector<std::string> new_links;
                    
                    for (const auto& page : results) {
                        if (page.status_code == 200 && !page.content.empty()) {
                            // 準備儲存資料
                            StoredPage stored_page;
                            stored_page.url = page.url;
                            stored_page.title = page.title;
                            stored_page.content = page.content;
                            stored_page.text_content = page.content;
                            pages_to_save.push_back(stored_page);
                            
                            // 準備索引資料
                            Document doc;
                            doc.url = page.url;
                            doc.title = page.title;
                            doc.content = page.content;
                            doc.text_content = page.content;
                            docs_to_index.push_back(doc);
                            
                            // 收集新連結
                            for (const auto& link : page.links) {
                                // 檢查是否為同域名的連結（包含 xsong.us 或相對路徑）
                                if (link.find("xsong.us") != std::string::npos || 
                                    (link[0] == '/' && link.find("://") == std::string::npos)) {
                                    new_links.push_back(link);
                                }
                            }
                        }
                    }
                    
                    // 批量儲存到資料庫
                    for (const auto& stored_page : pages_to_save) {
                        if (storage->savePage(stored_page)) {
                            LOG_INFO("已儲存頁面: " + stored_page.url);
                        } else {
                            LOG_ERROR("儲存頁面失敗: " + stored_page.url);
                        }
                    }
                    
                    // 批量建立索引
                    for (const auto& doc : docs_to_index) {
                        if (indexer->addDocument(doc)) {
                            LOG_INFO("已建立索引: " + doc.url);
                        } else {
                            LOG_ERROR("建立索引失敗: " + doc.url);
                        }
                    }
                    
                    // 批量添加新連結
                    if (!new_links.empty()) {
                        LOG_INFO("發現 " + std::to_string(new_links.size()) + " 個新連結");
                        for (const auto& link : new_links) {
                            g_crawler->addUrl(link);
                            LOG_INFO("添加連結: " + link);
                        }
                    }
                    
                    // 清空已處理的結果
                    g_crawler->clearResults();
                }
            }
            
            // 檢查爬蟲狀態
            if (g_crawler && !g_crawler->isRunning()) {
                LOG_WARN("爬蟲已停止");
                break;
            }
            
            // 檢查爬蟲是否完成 - 需要連續幾次檢查隊列都為空才認為完成
            static int empty_checks = 0;
            if (g_crawler && g_crawler->isRunning() && g_crawler->isFinished()) {
                empty_checks++;
                if (empty_checks >= 3) {  // 連續3次檢查都為空才完成
                    LOG_INFO("🎉 爬取完成！");
                    LOG_INFO("📊 爬取統計:");
                    LOG_INFO("   - 已訪問頁面: " + std::to_string(g_crawler->getVisitedCount()) + " 個");
                    LOG_INFO("   - 剩餘隊列: " + std::to_string(g_crawler->getQueueSize()) + " 個");
                    LOG_INFO("💾 所有頁面已存入資料庫並建立索引");
                    LOG_INFO("🔍 現在可以開始搜尋了！");
                    LOG_INFO("   搜尋端點: http://localhost:8080/search?q=關鍵字");
                    LOG_INFO("   統計端點: http://localhost:8080/stats");
                    
                    // 停止爬蟲
                    g_crawler->stop();
                    break;
                }
            } else {
                empty_checks = 0;  // 重置計數器
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("程式執行錯誤: " + std::string(e.what()));
        LOG_ERROR("異常類型: " + std::string(typeid(e).name()));
        return 1;
    } catch (...) {
        LOG_ERROR("未知異常發生");
        return 1;
    }
    
    // 清理資源 - 按正確順序
    LOG_INFO("開始清理資源...");
    
    // 先停止爬蟲，確保所有資料已寫入資料庫
    try {
        if (g_crawler) {
            LOG_INFO("正在停止爬蟲...");
            g_crawler->stop();  // 停止爬蟲
            LOG_INFO("爬蟲已停止");
            
            // 等待所有工作執行緒完成
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            g_crawler.reset();
            LOG_INFO("爬蟲資源已釋放");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("停止爬蟲時發生錯誤: " + std::string(e.what()));
        LOG_ERROR("異常類型: " + std::string(typeid(e).name()));
    } catch (...) {
        LOG_ERROR("停止爬蟲時發生未知異常");
    }
    
    // 再關閉 HTTP 伺服器
    try {
        if (g_server) {
            LOG_INFO("正在關閉 HTTP 伺服器...");
            g_server.reset();  // 先關閉 HTTP 伺服器
            LOG_INFO("HTTP 伺服器已關閉");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("關閉伺服器時發生錯誤: " + std::string(e.what()));
        LOG_ERROR("異常類型: " + std::string(typeid(e).name()));
    } catch (...) {
        LOG_ERROR("關閉伺服器時發生未知異常");
    }
    
    try {
        LOG_INFO("正在清理 curl 全局資源...");
        // 清理 curl 全局資源
        curl_global_cleanup();
        LOG_INFO("curl 資源已清理");
    } catch (const std::exception& e) {
        LOG_ERROR("清理 curl 資源時發生錯誤: " + std::string(e.what()));
        LOG_ERROR("異常類型: " + std::string(typeid(e).name()));
    } catch (...) {
        LOG_ERROR("清理 curl 資源時發生未知異常");
    }
    
    LOG_INFO("程式正常結束");
    return 0;
}
