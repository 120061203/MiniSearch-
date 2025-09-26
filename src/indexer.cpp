#include "indexer.h"
#include "logger.h"
#include <sqlite3.h>
#include <algorithm>
#include <sstream>
#include <regex>
#include <cmath>
#include <chrono>
#include <unordered_map>

namespace MiniSearch {

// 索引器實現類
class Indexer::IndexerImpl {
public:
    IndexerImpl() : m_db(nullptr) {}
    std::mutex m_db_mutex;  // 保護資料庫操作
    
    ~IndexerImpl() {
        if (m_db) {
            sqlite3_close(m_db);
        }
    }
    
    bool initialize(const std::string& db_path) {
        int rc = sqlite3_open(db_path.c_str(), &m_db);
        if (rc != SQLITE_OK) {
            LOG_ERROR("無法開啟資料庫: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        
        return createTables();
    }
    
    bool createTables() {
        const char* create_docs_sql = R"(
            CREATE TABLE IF NOT EXISTS documents (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                url TEXT UNIQUE NOT NULL,
                title TEXT,
                content TEXT,
                text_content TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        )";
        
        const char* create_terms_sql = R"(
            CREATE TABLE IF NOT EXISTS terms (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                term TEXT UNIQUE NOT NULL
            )
        )";
        
        const char* create_index_sql = R"(
            CREATE TABLE IF NOT EXISTS term_documents (
                term_id INTEGER,
                doc_id INTEGER,
                frequency INTEGER DEFAULT 1,
                PRIMARY KEY (term_id, doc_id),
                FOREIGN KEY (term_id) REFERENCES terms(id),
                FOREIGN KEY (doc_id) REFERENCES documents(id)
            )
        )";
        
        const char* create_stats_sql = R"(
            CREATE TABLE IF NOT EXISTS index_stats (
                key TEXT PRIMARY KEY,
                value INTEGER
            )
        )";
        
        if (sqlite3_exec(m_db, create_docs_sql, nullptr, nullptr, nullptr) != SQLITE_OK ||
            sqlite3_exec(m_db, create_terms_sql, nullptr, nullptr, nullptr) != SQLITE_OK ||
            sqlite3_exec(m_db, create_index_sql, nullptr, nullptr, nullptr) != SQLITE_OK ||
            sqlite3_exec(m_db, create_stats_sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOG_ERROR("建立資料表失敗: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        
        return true;
    }
    
    bool addDocument(const Document& doc) {
        if (!m_db) {
            LOG_ERROR("索引器資料庫未初始化");
            return false;
        }
        
        LOG_INFO("開始建立索引: " + doc.url);
        
        // 使用互斥鎖保護資料庫操作
        std::lock_guard<std::mutex> lock(this->m_db_mutex);
        
        // 開始事務
        if (sqlite3_exec(m_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) != SQLITE_OK) {
            LOG_ERROR("索引器開始事務失敗: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        
        sqlite3_stmt* stmt;
        const char* sql = R"(
            INSERT OR REPLACE INTO documents (id, url, title, content, text_content, updated_at)
            VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
        )";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR("索引器準備 SQL 語句失敗: " + std::string(sqlite3_errmsg(m_db)));
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
        
        sqlite3_bind_int(stmt, 1, doc.id);
        sqlite3_bind_text(stmt, 2, doc.url.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, doc.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, doc.content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, doc.text_content.c_str(), -1, SQLITE_STATIC);
        
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        
        if (success) {
            LOG_INFO("文檔儲存成功: " + doc.url);
            // 更新索引
            updateIndex(doc);
            LOG_INFO("索引更新完成: " + doc.url);
            // 提交事務
            if (sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
                LOG_ERROR("索引器提交事務失敗: " + std::string(sqlite3_errmsg(m_db)));
                sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
                return false;
            }
            LOG_INFO("索引器事務已提交: " + doc.url);
        } else {
            LOG_ERROR("文檔儲存失敗: " + doc.url);
            // 回滾事務
            sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        
        return success;
    }
    
    bool removeDocument(int doc_id) {
        if (!m_db) return false;
        
        // 刪除文檔
        sqlite3_stmt* stmt;
        const char* sql = "DELETE FROM documents WHERE id = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_int(stmt, 1, doc_id);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        
        if (success) {
            // 刪除相關索引
            const char* delete_index_sql = "DELETE FROM term_documents WHERE doc_id = ?";
            sqlite3_prepare_v2(m_db, delete_index_sql, -1, &stmt, nullptr);
            sqlite3_bind_int(stmt, 1, doc_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        
        return success;
    }
    
    bool updateDocument(const Document& doc) {
        return addDocument(doc); // 使用 INSERT OR REPLACE
    }
    
    SearchResult search(const std::string& query, int limit, int offset) {
        SearchResult result;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        if (!m_db) return result;
        
        // 分詞
        std::vector<std::string> terms = tokenize(query);
        if (terms.empty()) return result;
        
        // 建立搜尋 SQL
        std::stringstream sql_builder;
        sql_builder << R"(
            SELECT d.id, d.url, d.title, d.content, d.text_content,
                   SUM(td.frequency) as score
            FROM documents d
            JOIN term_documents td ON d.id = td.doc_id
            JOIN terms t ON td.term_id = t.id
            WHERE t.term IN ()";
        
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i > 0) sql_builder << ",";
            sql_builder << "?";
        }
        
        sql_builder << R"(
            GROUP BY d.id, d.url, d.title, d.content, d.text_content
            ORDER BY score DESC
            LIMIT ? OFFSET ?
        )";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql_builder.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR("準備搜尋 SQL 失敗");
            return result;
        }
        
        // 綁定參數
        for (size_t i = 0; i < terms.size(); ++i) {
            sqlite3_bind_text(stmt, i + 1, terms[i].c_str(), -1, SQLITE_STATIC);
        }
        sqlite3_bind_int(stmt, terms.size() + 1, limit);
        sqlite3_bind_int(stmt, terms.size() + 2, offset);
        
        // 執行查詢
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Document doc;
            doc.id = sqlite3_column_int(stmt, 0);
            doc.url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            doc.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            doc.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            doc.text_content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            doc.score = sqlite3_column_double(stmt, 5);
            
            result.documents.push_back(doc);
        }
        
        sqlite3_finalize(stmt);
        
        // 計算搜尋時間
        auto end_time = std::chrono::high_resolution_clock::now();
        result.search_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        result.total_count = result.documents.size();
        
        return result;
    }
    
    size_t getDocumentCount() {
        if (!m_db) return 0;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT COUNT(*) FROM documents";
        
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
    
    size_t getTermCount() {
        if (!m_db) return 0;
        
        sqlite3_stmt* stmt;
        const char* sql = "SELECT COUNT(*) FROM terms";
        
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
    
    void clearIndex() {
        if (!m_db) return;
        
        const char* sqls[] = {
            "DELETE FROM term_documents",
            "DELETE FROM documents",
            "DELETE FROM terms",
            "DELETE FROM index_stats"
        };
        
        for (const char* sql : sqls) {
            sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
        }
    }

private:
    sqlite3* m_db;
    
    void updateIndex(const Document& doc) {
        // 分詞
        std::vector<std::string> terms = tokenize(doc.text_content);
        
        // 計算詞頻
        std::unordered_map<std::string, int> term_freq;
        for (const auto& term : terms) {
            term_freq[term]++;
        }
        
        // 更新索引
        for (const auto& pair : term_freq) {
            const std::string& term = pair.first;
            int frequency = pair.second;
            
            // 取得或建立詞彙 ID
            int term_id = getOrCreateTermId(term);
            if (term_id > 0) {
                // 更新或插入詞彙-文檔關聯
                updateTermDocument(term_id, doc.id, frequency);
            }
        }
    }
    
    int getOrCreateTermId(const std::string& term) {
        sqlite3_stmt* stmt;
        const char* sql = "SELECT id FROM terms WHERE term = ?";
        
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, term.c_str(), -1, SQLITE_STATIC);
        
        int term_id = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            term_id = sqlite3_column_int(stmt, 0);
        }
        
        sqlite3_finalize(stmt);
        
        // 如果詞彙不存在，建立它
        if (term_id == -1) {
            const char* insert_sql = "INSERT INTO terms (term) VALUES (?)";
            sqlite3_prepare_v2(m_db, insert_sql, -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, term.c_str(), -1, SQLITE_STATIC);
            
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                term_id = sqlite3_last_insert_rowid(m_db);
            }
            
            sqlite3_finalize(stmt);
        }
        
        return term_id;
    }
    
    void updateTermDocument(int term_id, int doc_id, int frequency) {
        sqlite3_stmt* stmt;
        const char* sql = R"(
            INSERT OR REPLACE INTO term_documents (term_id, doc_id, frequency)
            VALUES (?, ?, ?)
        )";
        
        sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, term_id);
        sqlite3_bind_int(stmt, 2, doc_id);
        sqlite3_bind_int(stmt, 3, frequency);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        
        // 簡單的分詞邏輯：按空格和標點符號分割
        std::string current_token;
        
        for (char c : text) {
            // 檢查是否為字母、數字或非ASCII字符（包含中文）
            if (std::isalnum(c) || (unsigned char)c > 127) {
                current_token += c;
            } else {
                // 遇到分隔符，處理當前詞彙
                if (!current_token.empty()) {
                    // 只對英文轉小寫
                    if (std::all_of(current_token.begin(), current_token.end(), 
                                   [](char c) { return std::isalpha(c) || std::isdigit(c); })) {
                        std::transform(current_token.begin(), current_token.end(), 
                                     current_token.begin(), ::tolower);
                    }
                    
                    if (isValidTerm(current_token)) {
                        tokens.push_back(current_token);
                    }
                    current_token.clear();
                }
            }
        }
        
        // 處理最後一個詞彙
        if (!current_token.empty()) {
            if (std::all_of(current_token.begin(), current_token.end(), 
                           [](char c) { return std::isalpha(c) || std::isdigit(c); })) {
                std::transform(current_token.begin(), current_token.end(), 
                             current_token.begin(), ::tolower);
            }
            
            if (isValidTerm(current_token)) {
                tokens.push_back(current_token);
            }
        }
        
        return tokens;
    }
    
    bool isValidTerm(const std::string& term) {
        return term.length() >= 2 && term.length() <= 50;
    }
};

Indexer::Indexer() : m_impl(std::make_unique<IndexerImpl>()) {
}

Indexer::~Indexer() = default;

bool Indexer::initialize(const std::string& db_path) {
    return m_impl->initialize(db_path);
}

bool Indexer::addDocument(const Document& doc) {
    return m_impl->addDocument(doc);
}

bool Indexer::removeDocument(int doc_id) {
    return m_impl->removeDocument(doc_id);
}

bool Indexer::updateDocument(const Document& doc) {
    return m_impl->updateDocument(doc);
}

SearchResult Indexer::search(const std::string& query, int limit, int offset) {
    return m_impl->search(query, limit, offset);
}

size_t Indexer::getDocumentCount() {
    return m_impl->getDocumentCount();
}

size_t Indexer::getTermCount() {
    return m_impl->getTermCount();
}

void Indexer::clearIndex() {
    m_impl->clearIndex();
}

std::vector<std::string> Indexer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::regex word_regex(R"(\b\w+\b)");
    std::sregex_iterator iter(text.begin(), text.end(), word_regex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        std::string token = iter->str();
        std::transform(token.begin(), token.end(), token.begin(), ::tolower);
        
        if (isValidTerm(token)) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

std::string Indexer::normalizeTerm(const std::string& term) {
    std::string normalized = term;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    return normalized;
}

bool Indexer::isValidTerm(const std::string& term) {
    return term.length() >= 2 && term.length() <= 50;
}

double Indexer::calculateTfIdf(const std::string& /*term*/, int /*doc_id*/) {
    // 簡化實現，實際應該計算 TF-IDF
    return 1.0;
}

double Indexer::calculateIdf(const std::string& /*term*/) {
    // 簡化實現，實際應該計算 IDF
    return 1.0;
}

} // namespace MiniSearch
