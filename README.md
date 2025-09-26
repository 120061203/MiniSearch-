# 迷你搜尋引擎爬蟲系統

一個使用 C++17 開發的分散式網頁爬蟲和搜尋引擎系統，支援多執行緒爬取、Redis 任務隊列、SQLite 儲存和 RESTful API 搜尋。

## 功能特色

- **分散式爬蟲**: 支援多台爬蟲節點共享 Redis 任務隊列
- **多執行緒爬取**: 使用 std::thread 實現並行網頁抓取
- **HTML 解析**: 使用 Gumbo-parser 萃取標題和連結
- **倒排索引**: 建立高效的全文搜尋索引
- **RESTful API**: 提供完整的搜尋 API 介面
- **模組化設計**: 清晰的模組分離，易於維護和擴展

## 系統架構

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   爬蟲節點 1     │    │   爬蟲節點 2     │    │   爬蟲節點 N     │
│  (Crawler)      │    │  (Crawler)      │    │  (Crawler)      │
└─────────┬───────┘    └─────────┬───────┘    └─────────┬───────┘
          │                      │                      │
          └──────────────────────┼──────────────────────┘
                                 │
                    ┌─────────────▼─────────────┐
                    │      Redis 任務隊列       │
                    │    (Task Queue)         │
                    └─────────────┬─────────────┘
                                 │
                    ┌─────────────▼─────────────┐
                    │     SQLite 儲存系統       │
                    │   (Storage + Indexer)   │
                    └─────────────┬─────────────┘
                                 │
                    ┌─────────────▼─────────────┐
                    │    搜尋 API 伺服器        │
                    │      (Search Server)     │
                    └───────────────────────────┘
```

## 依賴套件

### 系統依賴
- **C++17 編譯器** (GCC 7+ 或 Clang 5+)
- **CMake 3.10+**
- **pkg-config**

### 第三方函式庫
- **libcurl**: HTTP 客戶端庫
- **gumbo-parser**: HTML 解析庫
- **SQLite3**: 嵌入式資料庫
- **hiredis**: Redis 客戶端庫
- **cpp-httplib**: HTTP 伺服器庫

### Ubuntu/Debian 安裝依賴
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libcurl4-openssl-dev \
    libgumbo-dev \
    libsqlite3-dev \
    libhiredis-dev \
    git
```

### macOS 安裝依賴
```bash
# 使用 Homebrew
brew install cmake pkg-config curl gumbo-parser sqlite3 hiredis

# 安裝 cpp-httplib
git clone https://github.com/yhirose/cpp-httplib.git
sudo cp -r cpp-httplib /usr/local/include/
```

### CentOS/RHEL 安裝依賴
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake pkgconfig libcurl-devel sqlite-devel git

# 編譯安裝 gumbo-parser
git clone https://github.com/google/gumbo-parser.git
cd gumbo-parser
./autogen.sh
./configure
make
sudo make install

# 編譯安裝 hiredis
git clone https://github.com/redis/hiredis.git
cd hiredis
make
sudo make install
```

## 編譯安裝

### 1. 克隆專案
```bash
git clone <repository-url>
cd MiniSearch++
```

### 2. 建立建置目錄
```bash
mkdir build
cd build
```

### 3. 配置 CMake
```bash
cmake ..
```

### 4. 編譯
```bash
make -j$(nproc)
```

### 5. 安裝 (可選)
```bash
sudo make install
```

## 使用方法

### 啟動 Redis 伺服器
```bash
redis-server
```

### 啟動爬蟲
```bash
# 基本爬蟲模式
./mini_search_engine --crawler

# 指定工作執行緒數量
./mini_search_engine --crawler --workers 8

# 指定 Redis 連接參數
./mini_search_engine --crawler --redis-host 192.168.1.100 --redis-port 6379
```

### 啟動搜尋 API 伺服器
```bash
# 基本伺服器模式
./mini_search_engine --server

# 指定伺服器參數
./mini_search_engine --server --server-host 0.0.0.0 --server-port 8080
```

### 同時啟動爬蟲和伺服器
```bash
./mini_search_engine --both --workers 4 --server-port 8080
```

### 命令列參數說明

| 參數 | 說明 | 預設值 |
|------|------|--------|
| `--crawler` | 啟動爬蟲模式 | - |
| `--server` | 啟動搜尋 API 伺服器 | - |
| `--both` | 同時啟動爬蟲和伺服器 | - |
| `--workers N` | 設定爬蟲工作執行緒數量 | 4 |
| `--redis-host HOST` | Redis 主機位址 | localhost |
| `--redis-port PORT` | Redis 連接埠 | 6379 |
| `--server-host HOST` | 伺服器主機位址 | localhost |
| `--server-port PORT` | 伺服器連接埠 | 8080 |
| `--db-path PATH` | 資料庫檔案路徑 | ./search.db |
| `--log-level LEVEL` | 日誌等級 (DEBUG\|INFO\|WARN\|ERROR) | INFO |

## API 文件

### 搜尋 API
```
GET /search?q=關鍵字&limit=10&offset=0
```

**參數:**
- `q`: 搜尋關鍵字 (必需)
- `limit`: 結果數量限制 (1-100, 預設: 10)
- `offset`: 結果偏移量 (預設: 0)

**回應範例:**
```json
{
  "query": "搜尋關鍵字",
  "total_count": 25,
  "search_time_ms": 12.5,
  "limit": 10,
  "offset": 0,
  "documents": [
    {
      "id": 1,
      "url": "https://example.com",
      "title": "範例頁面",
      "score": 0.95,
      "preview": "這是頁面內容的預覽..."
    }
  ]
}
```

### 統計 API
```
GET /stats
```

**回應範例:**
```json
{
  "document_count": 1000,
  "term_count": 5000,
  "page_count": 1000
}
```

### 頁面詳情 API
```
GET /page/{id}
```

### 網域統計 API
```
GET /domain/{domain}
```

### 健康檢查 API
```
GET /health
```

## 系統配置

### Redis 配置
確保 Redis 伺服器正在運行，預設連接參數：
- 主機: localhost
- 連接埠: 6379
- 密碼: (無)

### 資料庫配置
系統使用 SQLite 資料庫儲存頁面和索引：
- 預設路徑: `./search.db`
- 自動建立資料表結構
- 支援備份和恢復

## 分散式部署

### 多爬蟲節點部署
1. 在每台機器上安裝依賴和編譯程式
2. 確保所有節點能連接到同一個 Redis 伺服器
3. 啟動各節點的爬蟲：
   ```bash
   ./mini_search_engine --crawler --redis-host <redis-server-ip>
   ```

### 搜尋伺服器部署
1. 在專用伺服器上啟動搜尋 API：
   ```bash
   ./mini_search_engine --server --server-host 0.0.0.0 --server-port 8080
   ```

## 監控和日誌

### 日誌等級
- `DEBUG`: 詳細的除錯資訊
- `INFO`: 一般資訊 (預設)
- `WARN`: 警告訊息
- `ERROR`: 錯誤訊息

### 效能監控
- 使用 `/stats` API 端點監控系統狀態
- 檢查 Redis 隊列長度
- 監控資料庫大小和效能

## 故障排除

### 常見問題

1. **編譯錯誤**
   - 確認所有依賴套件已正確安裝
   - 檢查 CMake 版本是否 >= 3.10
   - 確認 C++17 編譯器支援

2. **Redis 連接失敗**
   - 確認 Redis 伺服器正在運行
   - 檢查網路連接和防火牆設定
   - 驗證 Redis 連接參數

3. **爬蟲無法抓取網頁**
   - 檢查網路連接
   - 確認目標網站可訪問
   - 檢查 User-Agent 和請求頭設定

4. **搜尋結果不準確**
   - 檢查索引是否正確建立
   - 確認文字處理和分詞邏輯
   - 調整 TF-IDF 計算參數

## 開發指南

### 專案結構
```
MiniSearch++/
├── CMakeLists.txt          # CMake 配置
├── README.md               # 專案說明
├── include/                # 頭文件目錄
│   ├── crawler.h          # 爬蟲模組
│   ├── parser.h           # HTML 解析模組
│   ├── task_queue.h       # 任務隊列模組
│   ├── indexer.h          # 索引模組
│   ├── storage.h          # 儲存模組
│   ├── server.h           # 搜尋 API 模組
│   └── logger.h           # 日誌模組
└── src/                    # 源文件目錄
    ├── main.cpp           # 主程式
    ├── crawler.cpp        # 爬蟲實現
    ├── parser.cpp         # HTML 解析實現
    ├── task_queue.cpp     # 任務隊列實現
    ├── indexer.cpp        # 索引實現
    ├── storage.cpp        # 儲存實現
    ├── server.cpp         # 搜尋 API 實現
    └── logger.cpp         # 日誌實現
```

### 擴展開發
1. **新增爬蟲策略**: 修改 `crawler.cpp` 中的抓取邏輯
2. **改進文字處理**: 更新 `parser.cpp` 中的解析演算法
3. **優化搜尋演算法**: 調整 `indexer.cpp` 中的 TF-IDF 計算
4. **新增 API 端點**: 在 `server.cpp` 中添加新的路由處理

## 授權條款

本專案採用 MIT 授權條款，詳見 LICENSE 文件。

## 貢獻指南

歡迎提交 Issue 和 Pull Request 來改進這個專案！

## 聯絡資訊

如有問題或建議，請透過以下方式聯絡：
- 提交 GitHub Issue
- 發送 Email 至專案維護者

---

**注意**: 請確保遵守目標網站的 robots.txt 和使用條款，負責任地使用爬蟲功能。
