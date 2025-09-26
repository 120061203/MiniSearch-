#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>

namespace MiniSearch {

struct Document {
    int id;
    std::string url;
    std::string title;
    std::string content;
    std::string text_content;
    double score;
    
    Document() : id(0), score(0.0) {}
};

struct SearchResult {
    std::vector<Document> documents;
    size_t total_count;
    double search_time_ms;
    
    SearchResult() : total_count(0), search_time_ms(0.0) {}
};

class Indexer {
public:
    Indexer();
    ~Indexer();
    
    // 初始化索引器
    bool initialize(const std::string& db_path = "search_index.db");
    
    // 索引操作
    bool addDocument(const Document& doc);
    bool removeDocument(int doc_id);
    bool updateDocument(const Document& doc);
    
    // 搜尋操作
    SearchResult search(const std::string& query, int limit = 10, int offset = 0);
    
    // 索引統計
    size_t getDocumentCount();
    size_t getTermCount();
    
    // 清理索引
    void clearIndex();

private:
    class IndexerImpl;
    std::unique_ptr<IndexerImpl> m_impl;
    
    // 文字處理
    std::vector<std::string> tokenize(const std::string& text);
    std::string normalizeTerm(const std::string& term);
    bool isValidTerm(const std::string& term);
    
    // 計算 TF-IDF 分數
    double calculateTfIdf(const std::string& term, int doc_id);
    double calculateIdf(const std::string& term);
};

} // namespace MiniSearch
