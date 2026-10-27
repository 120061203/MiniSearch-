#include "json_storage.h"
#include "logger.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace MiniSearch {

// UTF-8 清理函數 - 暫時只保留 ASCII 字符，跳過所有非 ASCII
std::string cleanUtf8(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        
        // 只保留 ASCII 字符
        if (c < 128) {
            result += c;
        }
        // 跳過所有非 ASCII 字符
        else {
            result += '?'; // 用問號替換非 ASCII 字符
        }
    }
    
    return result;
}

// 生成摘要
std::string JsonIndexer::generateSnippet(const std::string& text, const std::vector<std::string>& query_terms) {
    if (text.empty()) return "";
    
    // 清理 HTML 標籤，只保留純文字
    std::string clean_text = text;
    
    // 移除 HTML 標籤
    size_t pos = 0;
    while ((pos = clean_text.find('<', pos)) != std::string::npos) {
        size_t end_pos = clean_text.find('>', pos);
        if (end_pos != std::string::npos) {
            clean_text.erase(pos, end_pos - pos + 1);
        } else {
            break;
        }
    }
    
    // 清理多餘的空白字符
    std::string result;
    bool in_space = false;
    for (char c : clean_text) {
        if (std::isspace(c)) {
            if (!in_space) {
                result += ' ';
                in_space = true;
            }
        } else {
            result += c;
            in_space = false;
        }
    }
    
    // 簡單的摘要生成：取前 200 個字符
    std::string snippet = result.substr(0, std::min(200, (int)result.size()));
    
    // 如果文本太長，在句號處截斷
    size_t last_period = snippet.find_last_of("。.!?");
    if (last_period != std::string::npos && last_period > 50) {
        snippet = snippet.substr(0, last_period + 1);
    }
    
    // 如果沒有句號，在空格處截斷
    if (snippet.size() > 150) {
        size_t last_space = snippet.find_last_of(" \t\n");
        if (last_space != std::string::npos && last_space > 50) {
            snippet = snippet.substr(0, last_space);
        }
    }
    
    return snippet;
}

// 高亮文本
std::string JsonIndexer::highlightText(const std::string& text, const std::vector<std::string>& query_terms) {
    std::string result = text;
    
    for (const auto& term : query_terms) {
        if (term.empty()) continue;
        
        std::string highlighted_term = "<mark>" + term + "</mark>";
        
        // 不區分大小寫的替換
        std::string lower_text = result;
        std::string lower_term = term;
        std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
        std::transform(lower_term.begin(), lower_term.end(), lower_term.begin(), ::tolower);
        
        size_t pos = 0;
        while ((pos = lower_text.find(lower_term, pos)) != std::string::npos) {
            result.replace(pos, term.length(), highlighted_term);
            lower_text.replace(pos, term.length(), highlighted_term);
            pos += highlighted_term.length();
        }
    }
    
    return result;
}

// JsonStorage 實現
JsonStorage::JsonStorage() {}

JsonStorage::~JsonStorage() {
    close();
}

bool JsonStorage::initialize(const std::string& storage_path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_storage_path = storage_path;
    loadFromFile();
    return true;
}

void JsonStorage::loadFromFile() {
    std::ifstream file(m_storage_path);
    if (!file.is_open()) {
        return; // 檔案不存在，使用空的資料
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        if (j.contains("pages") && j["pages"].is_array()) {
            m_pages.clear();
            for (const auto& page_json : j["pages"]) {
                StoredPage page;
                page.id = page_json.value("id", 0);
                page.url = page_json.value("url", "");
                page.title = page_json.value("title", "");
                page.content = page_json.value("content", "");
                page.text_content = page_json.value("text_content", "");
                page.domain = page_json.value("domain", "");
                page.created_at = page_json.value("created_at", "");
                page.updated_at = page_json.value("updated_at", "");
                m_pages.push_back(page);
                
                if (page.id >= m_next_id) {
                    m_next_id = page.id + 1;
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("載入 JSON 檔案失敗: " + std::string(e.what()));
    }
}

void JsonStorage::saveToFile() {
    try {
        nlohmann::json j;
        j["pages"] = nlohmann::json::array();
        
        for (const auto& page : m_pages) {
            nlohmann::json page_json;
            page_json["id"] = page.id;
            page_json["url"] = cleanUtf8(page.url);
            page_json["title"] = cleanUtf8(page.title);
            page_json["content"] = cleanUtf8(page.content);
            page_json["text_content"] = cleanUtf8(page.text_content);
            page_json["domain"] = cleanUtf8(page.domain);
            page_json["created_at"] = cleanUtf8(page.created_at);
            page_json["updated_at"] = cleanUtf8(page.updated_at);
            j["pages"].push_back(page_json);
        }
        
        std::ofstream file(m_storage_path, std::ios::binary);
        file << j.dump(2); // 格式化輸出
    } catch (const std::exception& e) {
        LOG_ERROR("儲存 JSON 檔案失敗: " + std::string(e.what()));
    }
}

bool JsonStorage::savePage(const StoredPage& page) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 檢查是否已存在
    for (auto& existing_page : m_pages) {
        if (existing_page.url == page.url) {
            // 更新現有頁面
            existing_page.title = cleanUtf8(page.title);
            existing_page.content = cleanUtf8(page.content);
            existing_page.text_content = cleanUtf8(page.text_content);
            existing_page.domain = cleanUtf8(page.domain);
            existing_page.updated_at = cleanUtf8(page.updated_at);
            saveToFile();
            return true;
        }
    }
    
    // 新增頁面
    StoredPage new_page = page;
    new_page.id = m_next_id++;
    new_page.domain = cleanUtf8(extractDomain(new_page.url));
    new_page.url = cleanUtf8(new_page.url);
    new_page.title = cleanUtf8(new_page.title);
    new_page.content = cleanUtf8(new_page.content);
    new_page.text_content = cleanUtf8(new_page.text_content);
    
    // 設定時間戳
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    new_page.created_at = ss.str();
    new_page.updated_at = new_page.created_at;
    
    m_pages.push_back(new_page);
    saveToFile();
    return true;
}

StoredPage JsonStorage::getPage(int page_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& page : m_pages) {
        if (page.id == page_id) {
            return page;
        }
    }
    return StoredPage{};
}

StoredPage JsonStorage::getPageByUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& page : m_pages) {
        if (page.url == url) {
            return page;
        }
    }
    return StoredPage{};
}

std::vector<StoredPage> JsonStorage::getAllPages() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pages;
}

bool JsonStorage::deletePage(int page_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_pages.begin(), m_pages.end(),
                          [page_id](const StoredPage& page) { return page.id == page_id; });
    if (it != m_pages.end()) {
        m_pages.erase(it);
        saveToFile();
        return true;
    }
    return false;
}

bool JsonStorage::deletePageByUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_pages.begin(), m_pages.end(),
                          [&url](const StoredPage& page) { return page.url == url; });
    if (it != m_pages.end()) {
        m_pages.erase(it);
        saveToFile();
        return true;
    }
    return false;
}

size_t JsonStorage::getPageCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pages.size();
}

void JsonStorage::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    saveToFile();
}

std::string JsonStorage::extractDomain(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos) return "";
    start += 3;
    
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.length();
    
    return url.substr(start, end - start);
}

// JsonIndexer 實現
JsonIndexer::JsonIndexer() {}

JsonIndexer::~JsonIndexer() {
    close();
}

bool JsonIndexer::initialize(const std::string& index_path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_index_path = index_path;
    loadFromFile();
    return true;
}

void JsonIndexer::loadFromFile() {
    std::ifstream file(m_index_path);
    if (!file.is_open()) {
        return; // 檔案不存在，使用空的資料
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        if (j.contains("documents") && j["documents"].is_array()) {
            m_documents.clear();
            for (const auto& doc_json : j["documents"]) {
                Document doc;
                doc.id = doc_json.value("id", 0);
                doc.url = doc_json.value("url", "");
                doc.title = doc_json.value("title", "");
                doc.content = doc_json.value("content", "");
                doc.text_content = doc_json.value("text_content", "");
                doc.score = doc_json.value("score", 0.0);
                m_documents.push_back(doc);
                
                if (doc.id >= m_next_id) {
                    m_next_id = doc.id + 1;
                }
            }
        }
        
        if (j.contains("term_index") && j["term_index"].is_object()) {
            m_term_index.clear();
            for (auto& [term, doc_ids] : j["term_index"].items()) {
                if (doc_ids.is_array()) {
                    std::vector<int> ids;
                    for (const auto& id : doc_ids) {
                        ids.push_back(id.get<int>());
                    }
                    m_term_index[term] = ids;
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("載入索引檔案失敗: " + std::string(e.what()));
    }
}

void JsonIndexer::saveToFile() {
    try {
        nlohmann::json j;
        j["documents"] = nlohmann::json::array();
        
        for (const auto& doc : m_documents) {
            nlohmann::json doc_json;
            doc_json["id"] = doc.id;
            doc_json["url"] = cleanUtf8(doc.url);
            doc_json["title"] = cleanUtf8(doc.title);
            doc_json["content"] = cleanUtf8(doc.content);
            doc_json["text_content"] = cleanUtf8(doc.text_content);
            doc_json["score"] = doc.score;
            j["documents"].push_back(doc_json);
        }
        
        j["term_index"] = nlohmann::json::object();
        for (const auto& [term, doc_ids] : m_term_index) {
            j["term_index"][term] = doc_ids;
        }
        
        std::ofstream file(m_index_path, std::ios::binary);
        file << j.dump(2); // 格式化輸出
    } catch (const std::exception& e) {
        LOG_ERROR("儲存索引檔案失敗: " + std::string(e.what()));
    }
}

bool JsonIndexer::addDocument(const Document& doc) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 檢查是否已存在
    for (auto& existing_doc : m_documents) {
        if (existing_doc.url == doc.url) {
            // 更新現有文檔
            existing_doc.title = cleanUtf8(doc.title);
            existing_doc.content = cleanUtf8(doc.content);
            existing_doc.text_content = cleanUtf8(doc.text_content);
            existing_doc.score = doc.score;
            saveToFile();
            return true;
        }
    }
    
    // 新增文檔
    Document new_doc = doc;
    new_doc.id = m_next_id++;
    new_doc.url = cleanUtf8(new_doc.url);
    new_doc.title = cleanUtf8(new_doc.title);
    new_doc.content = cleanUtf8(new_doc.content);
    new_doc.text_content = cleanUtf8(new_doc.text_content);
    m_documents.push_back(new_doc);
    
    // 更新索引
    std::vector<std::string> terms = tokenize(new_doc.text_content);
    for (const auto& term : terms) {
        if (isValidTerm(term)) {
            m_term_index[term].push_back(new_doc.id);
        }
    }
    
    saveToFile();
    return true;
}

bool JsonIndexer::removeDocument(int doc_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_documents.begin(), m_documents.end(),
                          [doc_id](const Document& doc) { return doc.id == doc_id; });
    if (it != m_documents.end()) {
        m_documents.erase(it);
        
        // 從索引中移除
        for (auto& [term, doc_ids] : m_term_index) {
            doc_ids.erase(std::remove(doc_ids.begin(), doc_ids.end(), doc_id), doc_ids.end());
        }
        
        saveToFile();
        return true;
    }
    return false;
}

std::vector<Document> JsonIndexer::search(const std::string& query, int limit, int offset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Document> results;
    
    std::vector<std::string> query_terms = tokenize(query);
    if (query_terms.empty()) {
        return results;
    }
    
    std::map<int, double> doc_scores;
    
    for (const auto& term : query_terms) {
        if (m_term_index.find(term) != m_term_index.end()) {
            for (int doc_id : m_term_index[term]) {
                doc_scores[doc_id] += calculateScore(term, doc_id);
            }
        }
    }
    
    // 排序並取前 N 個結果
    std::vector<std::pair<int, double>> sorted_scores(doc_scores.begin(), doc_scores.end());
    std::sort(sorted_scores.begin(), sorted_scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    for (int i = offset; i < std::min(offset + limit, (int)sorted_scores.size()); ++i) {
        int doc_id = sorted_scores[i].first;
        for (const auto& doc : m_documents) {
            if (doc.id == doc_id) {
                Document result = doc;
                result.score = sorted_scores[i].second;
                
                // 生成摘要
                result.snippet = generateSnippet(result.text_content, query_terms);
                
                // 生成高亮標題
                result.highlighted_title = highlightText(result.title, query_terms);
                
                // 生成高亮摘要
                result.highlighted_snippet = highlightText(result.snippet, query_terms);
                
                results.push_back(result);
                break;
            }
        }
    }
    
    return results;
}

size_t JsonIndexer::getDocumentCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_documents.size();
}

size_t JsonIndexer::getTermCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_term_index.size();
}

void JsonIndexer::clearIndex() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_documents.clear();
    m_term_index.clear();
    m_next_id = 1;
    saveToFile();
}

void JsonIndexer::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    saveToFile();
}

std::vector<std::string> JsonIndexer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
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

bool JsonIndexer::isValidTerm(const std::string& term) {
    return term.length() >= 2 && term.length() <= 50;
}

double JsonIndexer::calculateScore(const std::string& term, int doc_id) {
    // 簡單的 TF 計算
    int term_count = 0;
    for (const auto& doc : m_documents) {
        if (doc.id == doc_id) {
            std::vector<std::string> doc_terms = tokenize(doc.text_content);
            for (const auto& t : doc_terms) {
                if (t == term) {
                    term_count++;
                }
            }
            break;
        }
    }
    
    return term_count > 0 ? 1.0 + std::log(term_count) : 0.0;
}

} // namespace MiniSearch
