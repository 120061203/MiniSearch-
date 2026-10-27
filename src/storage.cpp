#include "storage.h"
#include "logger.h"
#include <sqlite3.h>
#include <algorithm>
#include <regex>
#include <fstream>
#include <sstream>

namespace MiniSearch {

// 儲存實現類
class Storage::StorageImpl {
public:
    StorageImpl() : m_db(nullptr) {}
    std::mutex m_db_mutex;  // 保護資料庫操作
    
    ~StorageImpl() {
        if (m_db) {
            sqlite3_close(m_db);
        }
    }
    
    bool initialize(const std::string& db_path) {
        int rc = sqlite3_open(db_path.c_str(), &m_db);
        if (rc != SQLITE_OK) {
            LOG_ERROR("無法開啟儲存資料庫: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        
        // 設定資料庫模式以確保資料持久化
        sqlite3_exec(m_db, "PRAGMA journal_mode = DELETE", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = FULL", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA cache_size = 1000", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA temp_store = MEMORY", nullptr, nullptr, nullptr);
        
        return createTables();
    }
    
    void close() {
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }
    
    bool createTables() {
        const char* create_pages_sql = R"(
            CREATE TABLE IF NOT EXISTS pages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT UNIQUE NOT NULL,
                title TEXT,
                content TEXT,
                text_content TEXT,
                domain TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        )";
        
        const char* create_domains_sql = R"(
            CREATE TABLE IF NOT EXISTS domains (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                domain TEXT UNIQUE NOT NULL,
                page_count INTEGER DEFAULT 0,
                last_crawled DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        )";
        
        if (sqlite3_exec(m_db, create_pages_sql, nullptr, nullptr, nullptr) != SQLITE_OK ||
            sqlite3_exec(m_db, create_domains_sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOG_ERROR("建立儲存資料表失敗: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        
        return true;
    }
    
    bool savePage(const StoredPage& page) {
        if (!m_db) {
            LOG_ERROR("資料庫未初始化");
            return false;
        }
        
        LOG_INFO("開始儲存頁面: " + page.url);
        
        // 使用互斥鎖保護資料庫操作
        std::lock_guard<std::mutex> lock(this->m_db_mutex);
        
        // 開始事務
        if (sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOG_ERROR("開始事務失敗: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        
        sqlite3_stmt* stmt;
        const char* sql = R"(
            INSERT OR REPLACE INTO pages (id, url, title, content, text_content, domain, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
        )";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR("準備儲存 SQL 語句失敗: " + std::string(sqlite3_errmsg(m_db)));
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
        
        sqlite3_bind_int(stmt, 1, page.id);
        sqlite3_bind_text(stmt, 2, page.url.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, page.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, page.content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, page.text_content.c_str(), -1, SQLITE_STATIC);
        
        std::string domain = extractDomain(page.url);
        sqlite3_bind_text(stmt, 6, domain.c_str(), -1, SQLITE_STATIC);
        
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        
        if (success) {
            LOG_INFO("頁面儲存成功: " + page.url);
            updateDomainStats(domain);
            // 提交事務
            if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
                LOG_ERROR("提交事務失敗: " + std::string(sqlite3_errmsg(m_db)));
                sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
                return false;
            }
            // 強制同步到磁碟
            sqlite3_exec(m_db, "PRAGMA synchronous = FULL", nullptr, nullptr, nullptr);
            LOG_INFO("事務已提交: " + page.url);
        } else {
            LOG_ERROR("頁面儲存失敗: " + page.url);
            // 回滾事務
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        
        return success;
    }
    
    bool updatePage(const StoredPage& page) {
        return savePage(page); // 使用 INSERT OR REPLACE
    }
    
    bool deletePage(int page_id) {
        if (!m_db) return false;
        
        sqlite3_stmt* stmt;
        const char* sql = "DELETE FROM pages WHERE id = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_int(stmt, 1, page_id);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        
        return success;
    }
    
    bool deletePageByUrl(const std::string& url) {
        if (!m_db) return false;
        
        sqlite3_stmt* stmt;
        const char* sql = "DELETE FROM pages WHERE url = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_STATIC);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        
        return success;
    }
    
    StoredPage getPage(int page_id) {
        StoredPage page;
        if (!m_db) return page;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id, url, title, content, text_content, created_at, updated_at FROM pages WHERE id = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return page;
        }
        
        sqlite3_bind_int(stmt, 1, page_id);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            page.id = sqlite3_column_int(stmt, 0);
            page.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            page.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            page.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            page.text_content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            page.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            page.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        }
        
        sqlite3_finalize(stmt);
        return page;
    }
    
    StoredPage getPageByUrl(const std::string& url) {
        StoredPage page;
        if (!m_db) return page;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id, url, title, content, text_content, created_at, updated_at FROM pages WHERE url = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return page;
        }
        
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            page.id = sqlite3_column_int(stmt, 0);
            page.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            page.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            page.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            page.text_content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            page.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            page.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        }
        
        sqlite3_finalize(stmt);
        return page;
    }
    
    std::vector<StoredPage> getAllPages() {
        std::vector<StoredPage> pages;
        if (!m_db) return pages;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id, url, title, content, text_content, created_at, updated_at FROM pages ORDER BY created_at DESC";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return pages;
        }
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            StoredPage page;
            page.id = sqlite3_column_int(stmt, 0);
            page.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            page.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            page.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            page.text_content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            page.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            page.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            
            pages.push_back(page);
        }
        
        sqlite3_finalize(stmt);
        return pages;
    }
    
    std::vector<StoredPage> getPagesByDomain(const std::string& domain) {
        std::vector<StoredPage> pages;
        if (!m_db) return pages;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id, url, title, content, text_content, created_at, updated_at FROM pages WHERE domain = ? ORDER BY created_at DESC";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return pages;
        }
        
        sqlite3_bind_text(stmt, 1, domain.c_str(), -1, SQLITE_STATIC);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            StoredPage page;
            page.id = sqlite3_column_int(stmt, 0);
            page.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            page.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            page.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            page.text_content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            page.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            page.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            
            pages.push_back(page);
        }
        
        sqlite3_finalize(stmt);
        return pages;
    }
    
    size_t getPageCount() {
        if (!m_db) return 0;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT COUNT(*) FROM pages";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }
        
        size_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        
        sqlite3_finalize(stmt);
        return count;
    }
    
    size_t getPageCountByDomain(const std::string& domain) {
        if (!m_db) return 0;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT COUNT(*) FROM pages WHERE domain = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }
        
        sqlite3_bind_text(stmt, 1, domain.c_str(), -1, SQLITE_STATIC);
        
        size_t count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        
        sqlite3_finalize(stmt);
        return count;
    }
    
    void clearAllPages() {
        if (!m_db) return;
        
        const char* sqls[] = {
            "DELETE FROM pages",
            "DELETE FROM domains"
        };
        
        for (const char* sql : sqls) {
            sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
        }
    }
    
    void clearPagesByDomain(const std::string& domain) {
        if (!m_db) return;
        
        sqlite3_stmt* stmt;
        const char* sql = "DELETE FROM pages WHERE domain = ?";
        
        sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        // 更新網域統計
        updateDomainStats(domain);
    }
    
    bool backup(const std::string& backup_path) {
        if (!m_db) return false;
        
        sqlite3* backup_db;
        int rc = sqlite3_open(backup_path.c_str(), &backup_db);
        if (rc != SQLITE_OK) {
            LOG_ERROR("無法建立備份檔案: " + backup_path);
            return false;
        }
        
        sqlite3_backup* backup = sqlite3_backup_init(backup_db, "main", m_db, "main");
        if (backup) {
            sqlite3_backup_step(backup, -1);
            sqlite3_backup_finish(backup);
        }
        
        sqlite3_close(backup_db);
        return true;
    }
    
    bool restore(const std::string& backup_path) {
        if (!m_db) return false;
        
        // 關閉現有連接
        sqlite3_close(m_db);
        m_db = nullptr;
        
        // 複製備份檔案
        std::ifstream src(backup_path, std::ios::binary);
        std::ofstream dst("restored.db", std::ios::binary);
        dst << src.rdbuf();
        
        // 重新開啟資料庫
        int rc = sqlite3_open("restored.db", &m_db);
        return (rc == SQLITE_OK);
    }

private:
    sqlite3* m_db;
    
    void updateDomainStats(const std::string& domain) {
        if (!m_db) return;
        
        sqlite3_stmt* stmt;
        const char* sql = R"(
            INSERT OR REPLACE INTO domains (domain, page_count, last_crawled)
            VALUES (?, (SELECT COUNT(*) FROM pages WHERE domain = ?), CURRENT_TIMESTAMP)
        )";
        
        sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, domain.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    std::string extractDomain(const std::string& url) {
        std::regex url_regex(R"(https?://([^/]+))");
        std::smatch match;
        
        if (std::regex_search(url, match, url_regex)) {
            return match[1].str();
        }
        
        return "";
    }
};

Storage::Storage() : m_impl(std::make_unique<StorageImpl>()) {
}

Storage::~Storage() = default;

bool Storage::initialize(const std::string& db_path) {
    return m_impl->initialize(db_path);
}

void Storage::close() {
    m_impl->close();
}

bool Storage::savePage(const StoredPage& page) {
    return m_impl->savePage(page);
}

bool Storage::updatePage(const StoredPage& page) {
    return m_impl->updatePage(page);
}

bool Storage::deletePage(int page_id) {
    return m_impl->deletePage(page_id);
}

bool Storage::deletePageByUrl(const std::string& url) {
    return m_impl->deletePageByUrl(url);
}

StoredPage Storage::getPage(int page_id) {
    return m_impl->getPage(page_id);
}

StoredPage Storage::getPageByUrl(const std::string& url) {
    return m_impl->getPageByUrl(url);
}

std::vector<StoredPage> Storage::getAllPages() {
    return m_impl->getAllPages();
}

std::vector<StoredPage> Storage::getPagesByDomain(const std::string& domain) {
    return m_impl->getPagesByDomain(domain);
}

size_t Storage::getPageCount() {
    return m_impl->getPageCount();
}

size_t Storage::getPageCountByDomain(const std::string& domain) {
    return m_impl->getPageCountByDomain(domain);
}

void Storage::clearAllPages() {
    m_impl->clearAllPages();
}

void Storage::clearPagesByDomain(const std::string& domain) {
    m_impl->clearPagesByDomain(domain);
}

bool Storage::backup(const std::string& backup_path) {
    return m_impl->backup(backup_path);
}

bool Storage::restore(const std::string& backup_path) {
    return m_impl->restore(backup_path);
}

std::string Storage::normalizeUrl(const std::string& url) {
    std::string normalized = url;
    
    // 移除尾隨斜線
    if (normalized.length() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    
    // 轉換為小寫
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    return normalized;
}

std::string Storage::extractDomain(const std::string& url) {
    std::regex url_regex(R"(https?://([^/]+))");
    std::smatch match;
    
    if (std::regex_search(url, match, url_regex)) {
        return match[1].str();
    }
    
    return "";
}

} // namespace MiniSearch
