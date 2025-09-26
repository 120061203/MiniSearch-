#include "task_queue.h"
#include "logger.h"
#include <hiredis/hiredis.h>
#include <stdexcept>
#include <sstream>

namespace MiniSearch {

const std::string TaskQueue::QUEUE_KEY = "crawler:queue";
const std::string TaskQueue::PROCESSED_KEY = "crawler:processed";

// Redis 客戶端實現類
class TaskQueue::RedisClient {
public:
    RedisClient() : m_context(nullptr) {}
    
    ~RedisClient() {
        disconnect();
    }
    
    bool connect(const std::string& host, int port, const std::string& password) {
        m_context = redisConnect(host.c_str(), port);
        
        if (!m_context || m_context->err) {
            if (m_context) {
                LOG_ERROR("Redis 連接錯誤: " + std::string(m_context->errstr));
                redisFree(m_context);
                m_context = nullptr;
            } else {
                LOG_ERROR("無法分配 Redis 連接");
            }
            return false;
        }
        
        // 如果有密碼，進行認證
        if (!password.empty()) {
            redisReply* reply = static_cast<redisReply*>(
                redisCommand(m_context, "AUTH %s", password.c_str()));
            
            if (!reply || reply->type == REDIS_REPLY_ERROR) {
                LOG_ERROR("Redis 認證失敗");
                if (reply) freeReplyObject(reply);
                return false;
            }
            freeReplyObject(reply);
        }
        
        LOG_INFO("Redis 連接成功: " + host + ":" + std::to_string(port));
        return true;
    }
    
    void disconnect() {
        if (m_context) {
            redisFree(m_context);
            m_context = nullptr;
        }
    }
    
    bool isConnected() const {
        return m_context != nullptr;
    }
    
    bool pushUrl(const std::string& url) {
        if (!m_context) return false;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "LPUSH %s %s", QUEUE_KEY.c_str(), url.c_str()));
        
        bool success = false;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            success = true;
        } else if (reply && reply->type == REDIS_REPLY_ERROR) {
            LOG_ERROR("Redis LPUSH 錯誤: " + std::string(reply->str));
        }
        
        if (reply) freeReplyObject(reply);
        return success;
    }
    
    std::string popUrl() {
        if (!m_context) return "";
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "BRPOP %s 1", QUEUE_KEY.c_str()));
        
        std::string url;
        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
            url = reply->element[1]->str;
        }
        
        if (reply) freeReplyObject(reply);
        return url;
    }
    
    std::vector<std::string> popUrls(int count) {
        std::vector<std::string> urls;
        if (!m_context) return urls;
        
        for (int i = 0; i < count; ++i) {
            std::string url = popUrl();
            if (url.empty()) break;
            urls.push_back(url);
        }
        
        return urls;
    }
    
    bool isUrlProcessed(const std::string& url) {
        if (!m_context) return false;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "SISMEMBER %s %s", PROCESSED_KEY.c_str(), url.c_str()));
        
        bool processed = false;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            processed = (reply->integer == 1);
        }
        
        if (reply) freeReplyObject(reply);
        return processed;
    }
    
    void markUrlProcessed(const std::string& url) {
        if (!m_context) return;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "SADD %s %s", PROCESSED_KEY.c_str(), url.c_str()));
        
        if (reply) freeReplyObject(reply);
    }
    
    size_t getQueueSize() {
        if (!m_context) return 0;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "LLEN %s", QUEUE_KEY.c_str()));
        
        size_t size = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            size = reply->integer;
        }
        
        if (reply) freeReplyObject(reply);
        return size;
    }
    
    size_t getProcessedCount() {
        if (!m_context) return 0;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "SCARD %s", PROCESSED_KEY.c_str()));
        
        size_t count = 0;
        if (reply && reply->type == REDIS_REPLY_INTEGER) {
            count = reply->integer;
        }
        
        if (reply) freeReplyObject(reply);
        return count;
    }
    
    void clearQueue() {
        if (!m_context) return;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "DEL %s", QUEUE_KEY.c_str()));
        
        if (reply) freeReplyObject(reply);
    }
    
    void clearProcessed() {
        if (!m_context) return;
        
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(m_context, "DEL %s", PROCESSED_KEY.c_str()));
        
        if (reply) freeReplyObject(reply);
    }

private:
    redisContext* m_context;
};

TaskQueue::TaskQueue() : m_redis(std::make_unique<RedisClient>()) {
}

TaskQueue::~TaskQueue() {
    disconnect();
}

bool TaskQueue::connect(const std::string& host, int port, const std::string& password) {
    return m_redis->connect(host, port, password);
}

void TaskQueue::disconnect() {
    m_redis->disconnect();
}

bool TaskQueue::pushUrl(const std::string& url) {
    if (!m_redis->isConnected()) {
        LOG_ERROR("Redis 未連接");
        return false;
    }
    
    return m_redis->pushUrl(url);
}

std::string TaskQueue::popUrl() {
    if (!m_redis->isConnected()) {
        LOG_ERROR("Redis 未連接");
        return "";
    }
    
    return m_redis->popUrl();
}

std::vector<std::string> TaskQueue::popUrls(int count) {
    if (!m_redis->isConnected()) {
        LOG_ERROR("Redis 未連接");
        return {};
    }
    
    return m_redis->popUrls(count);
}

bool TaskQueue::isUrlProcessed(const std::string& url) {
    if (!m_redis->isConnected()) {
        return false;
    }
    
    return m_redis->isUrlProcessed(url);
}

void TaskQueue::markUrlProcessed(const std::string& url) {
    if (!m_redis->isConnected()) {
        return;
    }
    
    m_redis->markUrlProcessed(url);
}

size_t TaskQueue::getQueueSize() {
    if (!m_redis->isConnected()) {
        return 0;
    }
    
    return m_redis->getQueueSize();
}

size_t TaskQueue::getProcessedCount() {
    if (!m_redis->isConnected()) {
        return 0;
    }
    
    return m_redis->getProcessedCount();
}

void TaskQueue::clearQueue() {
    if (!m_redis->isConnected()) {
        return;
    }
    
    m_redis->clearQueue();
}

void TaskQueue::clearProcessed() {
    if (!m_redis->isConnected()) {
        return;
    }
    
    m_redis->clearProcessed();
}

} // namespace MiniSearch
