#pragma once

#include <string>
#include <vector>

namespace MiniSearch {

struct ParsedPage {
    std::string title;
    std::string content;
    std::vector<std::string> links;
    std::string text_content;  // 純文字內容，用於索引
};

class Parser {
public:
    Parser();
    ~Parser();
    
    // 解析 HTML 內容
    ParsedPage parse(const std::string& html, const std::string& base_url = "");
    
    // 提取純文字內容（去除 HTML 標籤）
    std::string extractText(const std::string& html);
    
    // 清理和正規化文字
    std::string normalizeText(const std::string& text);

private:
    // 使用 Gumbo-parser 解析 HTML
    void parseGumbo(const std::string& html, ParsedPage& result, const std::string& base_url);
    
    // 提取節點文字
    void extractTextFromNode(void* node, std::string& text);
    
    // 提取連結
    void extractLinksFromNode(void* node, std::vector<std::string>& links, const std::string& base_url);
    
    // URL 正規化
    std::string normalizeUrl(const std::string& url, const std::string& base_url);
    
    // 檢查是否為有效 URL
    bool isValidUrl(const std::string& url);
};

} // namespace MiniSearch
