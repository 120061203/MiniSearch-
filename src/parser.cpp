#include "parser.h"
#include "logger.h"
#include <gumbo.h>
#include <regex>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace MiniSearch {

// 前向聲明輔助函數
GumboNode* findTitleNode(GumboNode* node);

Parser::Parser() {
    // 初始化 Gumbo-parser
}

Parser::~Parser() {
    // Gumbo-parser 不需要特別的清理
}

ParsedPage Parser::parse(const std::string& html, const std::string& base_url) {
    ParsedPage result;
    
    try {
        parseGumbo(html, result, base_url);
    } catch (const std::exception& e) {
        LOG_ERROR("HTML 解析錯誤: " + std::string(e.what()));
    }
    
    return result;
}

std::string Parser::extractText(const std::string& html) {
    ParsedPage parsed = parse(html);
    return normalizeText(parsed.text_content);
}

std::string Parser::normalizeText(const std::string& text) {
    std::string result = text;
    
    // 移除多餘的空白字符
    result = std::regex_replace(result, std::regex(R"(\s+)"), " ");
    
    // 移除前後空白
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    return result;
}

void Parser::parseGumbo(const std::string& html, ParsedPage& result, const std::string& base_url) {
    GumboOutput* output = gumbo_parse(html.c_str());
    
    if (!output) {
        throw std::runtime_error("無法解析 HTML");
    }
    
    // 提取標題
    if (output->root) {
        extractTextFromNode(output->root, result.text_content);
        extractLinksFromNode(output->root, result.links, base_url);
        
        // 尋找 title 標籤
        GumboNode* title_node = findTitleNode(output->root);
        if (title_node && title_node->v.element.children.length > 0) {
            GumboNode* title_text = static_cast<GumboNode*>(title_node->v.element.children.data[0]);
            if (title_text->type == GUMBO_NODE_TEXT) {
                result.title = title_text->v.text.text;
                result.title = normalizeText(result.title);
            }
        }
    }
    
    // 清理
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}

void Parser::extractTextFromNode(void* node_ptr, std::string& text) {
    GumboNode* node = static_cast<GumboNode*>(node_ptr);
    
    if (node->type == GUMBO_NODE_TEXT) {
        text += node->v.text.text;
        text += " ";
    } else if (node->type == GUMBO_NODE_ELEMENT) {
        // 跳過 script 和 style 標籤
        if (node->v.element.tag != GUMBO_TAG_SCRIPT && 
            node->v.element.tag != GUMBO_TAG_STYLE) {
            
            GumboVector* children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i) {
                extractTextFromNode(children->data[i], text);
            }
        }
    }
}

void Parser::extractLinksFromNode(void* node_ptr, std::vector<std::string>& links, const std::string& base_url) {
    GumboNode* node = static_cast<GumboNode*>(node_ptr);
    
    if (node->type == GUMBO_NODE_ELEMENT && node->v.element.tag == GUMBO_TAG_A) {
        GumboAttribute* href = gumbo_get_attribute(&node->v.element.attributes, "href");
        if (href) {
            std::string url = href->value;
            std::string normalized_url = normalizeUrl(url, base_url);
            
            if (isValidUrl(normalized_url)) {
                links.push_back(normalized_url);
            }
        }
    }
    
    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboVector* children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            extractLinksFromNode(children->data[i], links, base_url);
        }
    }
}

std::string Parser::normalizeUrl(const std::string& url, const std::string& base_url) {
    if (url.empty()) {
        return "";
    }
    
    // 如果是絕對 URL，直接返回
    if (url.find("http://") == 0 || url.find("https://") == 0) {
        return url;
    }
    
    // 如果是相對 URL，需要與 base_url 組合
    if (!base_url.empty()) {
        // 簡單的 URL 組合邏輯
        if (url[0] == '/') {
            // 絕對路徑，需要提取域名
            size_t protocol_end = base_url.find("://");
            if (protocol_end != std::string::npos) {
                size_t domain_end = base_url.find('/', protocol_end + 3);
                if (domain_end != std::string::npos) {
                    // 有路徑部分，提取域名
                    return base_url.substr(0, domain_end) + url;
                } else {
                    // 沒有路徑部分，直接組合
                    return base_url + url;
                }
            }
        } else {
            // 相對路徑
            if (base_url.back() == '/') {
                return base_url + url;
            } else {
                return base_url + "/" + url;
            }
        }
    }
    
    return url;
}

bool Parser::isValidUrl(const std::string& url) {
    if (url.empty()) {
        return false;
    }
    
    // 檢查是否為有效的 HTTP/HTTPS URL
    return (url.find("http://") == 0 || url.find("https://") == 0) &&
           url.length() > 7; // 至少要有 "http://" 或 "https://"
}

// 輔助函數：尋找 title 節點
GumboNode* findTitleNode(GumboNode* node) {
    if (node->type == GUMBO_NODE_ELEMENT && node->v.element.tag == GUMBO_TAG_TITLE) {
        return node;
    }
    
    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboVector* children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            GumboNode* result = findTitleNode(static_cast<GumboNode*>(children->data[i]));
            if (result) {
                return result;
            }
        }
    }
    
    return nullptr;
}

} // namespace MiniSearch
