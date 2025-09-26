#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace MiniSearch {

struct StoredPage {
    int id;
    std::string url;
    std::string title;
    std::string content;
    std::string text_content;
    std::string created_at;
    std::string updated_at;
    
    StoredPage() : id(0) {}
};

class Storage {
public:
    Storage();
    ~Storage();
    
    // 資料庫操作
    bool initialize(const std::string& db_path = "search_storage.db");
    void close();
    
    // 頁面儲存
    bool savePage(const StoredPage& page);
    bool updatePage(const StoredPage& page);
    bool deletePage(int page_id);
    bool deletePageByUrl(const std::string& url);
    
    // 頁面查詢
    StoredPage getPage(int page_id);
    StoredPage getPageByUrl(const std::string& url);
    std::vector<StoredPage> getAllPages();
    std::vector<StoredPage> getPagesByDomain(const std::string& domain);
    
    // 統計資訊
    size_t getPageCount();
    size_t getPageCountByDomain(const std::string& domain);
    
    // 清理操作
    void clearAllPages();
    void clearPagesByDomain(const std::string& domain);
    
    // 備份與恢復
    bool backup(const std::string& backup_path);
    bool restore(const std::string& backup_path);

private:
    class StorageImpl;
    std::unique_ptr<StorageImpl> m_impl;
    
    // 建立資料表
    bool createTables();
    
    // URL 正規化
    std::string normalizeUrl(const std::string& url);
    
    // 提取網域
    std::string extractDomain(const std::string& url);
};

} // namespace MiniSearch
