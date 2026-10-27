#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>

namespace MiniSearch {

class SearchServer {
public:
    SearchServer();
    ~SearchServer();
    
    // 伺服器配置
    void setHost(const std::string& host);
    void setPort(int port);
    void setIndexer(std::shared_ptr<class JsonIndexer> indexer);
    void setStorage(std::shared_ptr<class JsonStorage> storage);
    
    // 伺服器操作
    bool start();
    void stop();
    bool isRunning() const;
    
    // 等待伺服器停止
    void wait();

private:
    class ServerImpl;
    std::unique_ptr<ServerImpl> m_impl;
    
    std::string m_host;
    int m_port;
    std::shared_ptr<class JsonIndexer> m_indexer;
    std::shared_ptr<class JsonStorage> m_storage;
    std::atomic<bool> m_running;
    std::thread m_server_thread;
    
    void serverThread();
};

} // namespace MiniSearch
