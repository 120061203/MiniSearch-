#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

namespace MiniSearch {

struct StoredPage {
    int id = 0;
    std::string url;
    std::string title;
    std::string content;
    std::string text_content;
    std::string domain;
    std::string created_at;
    std::string updated_at;
};

struct Document {
    int id = 0;
    std::string url;
    std::string title;
    std::string content;
    std::string text_content;
    double score = 0.0;
    std::string snippet; // 摘要
    std::string highlighted_title; // 高亮標題
    std::string highlighted_snippet; // 高亮摘要
};

class JsonStorage {
public:
    JsonStorage();
    ~JsonStorage();
    
    bool initialize(const std::string& storage_path);
    bool savePage(const StoredPage& page);
    StoredPage getPage(int page_id);
    StoredPage getPageByUrl(const std::string& url);
    std::vector<StoredPage> getAllPages();
    bool deletePage(int page_id);
    bool deletePageByUrl(const std::string& url);
    size_t getPageCount();
    void close();
    
private:
    std::string m_storage_path;
    std::mutex m_mutex;
    std::vector<StoredPage> m_pages;
    int m_next_id = 1;
    
    void loadFromFile();
    void saveToFile();
    std::string extractDomain(const std::string& url);
};

class JsonIndexer {
public:
    JsonIndexer();
    ~JsonIndexer();
    
    bool initialize(const std::string& index_path);
    bool addDocument(const Document& doc);
    bool removeDocument(int doc_id);
    std::vector<Document> search(const std::string& query, int limit = 10, int offset = 0);
    size_t getDocumentCount();
    size_t getTermCount();
    void clearIndex();
    void close();
    
private:
    std::string m_index_path;
    std::mutex m_mutex;
    std::vector<Document> m_documents;
    std::map<std::string, std::vector<int>> m_term_index;
    int m_next_id = 1;
    
    void loadFromFile();
    void saveToFile();
    std::vector<std::string> tokenize(const std::string& text);
    bool isValidTerm(const std::string& term);
    double calculateScore(const std::string& term, int doc_id);
    std::string generateSnippet(const std::string& text, const std::vector<std::string>& query_terms);
    std::string highlightText(const std::string& text, const std::vector<std::string>& query_terms);
};

} // namespace MiniSearch
