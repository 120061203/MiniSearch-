# 🔍 迷你搜尋引擎前端使用指南

## 📋 概述

現在你已經有了一個完整的搜尋引擎系統，包括：
- **後端 API 伺服器** (C++17) - 提供搜尋、統計等功能
- **前端搜尋介面** (HTML/CSS/JavaScript) - 美觀的搜尋介面
- **靜態檔案伺服器** (Python) - 提供前端頁面

## 🚀 快速開始

### 1. 啟動完整系統
```bash
# 一鍵啟動所有服務
./start_search_engine.sh
```

### 2. 訪問搜尋介面
- **搜尋介面**: http://localhost:3000
- **API 文件**: http://localhost:3000/api-docs.html
- **後端 API**: http://localhost:8080

### 3. 停止系統
```bash
# 停止所有服務
./stop_search_engine.sh
```

## 🎯 功能特色

### 🔍 搜尋功能
- **即時搜尋**: 輸入關鍵字立即搜尋
- **結果高亮**: 搜尋結果中的關鍵字會高亮顯示
- **相關度評分**: 顯示每個結果的相關度分數
- **分頁支援**: 支援大量搜尋結果的分頁顯示

### 📊 統計資訊
- **文檔數量**: 已索引的文檔總數
- **詞彙數量**: 搜尋索引中的詞彙總數
- **頁面數量**: 已爬取的網頁總數

### 🎨 使用者介面
- **響應式設計**: 支援桌面和行動裝置
- **現代化 UI**: 使用漸層色彩和陰影效果
- **載入動畫**: 搜尋過程中的視覺回饋
- **錯誤處理**: 友善的錯誤訊息顯示

## 🛠️ 技術架構

### 前端技術
- **HTML5**: 語義化標記
- **CSS3**: 現代化樣式，包含 Flexbox 和 Grid
- **JavaScript ES6+**: 模組化程式設計
- **Fetch API**: 與後端 API 通訊

### 後端技術
- **C++17**: 高效能後端服務
- **httplib**: HTTP 伺服器框架
- **JSON 儲存**: 輕量級資料儲存
- **多執行緒**: 並行搜尋處理

## 📁 檔案結構

```
MiniSearch++/
├── build/                          # 編譯輸出目錄
│   ├── mini_search_engine         # 後端可執行檔案
│   ├── index.html                  # 前端搜尋介面
│   ├── api-docs.html              # API 文件頁面
│   ├── static_server.py           # 靜態檔案伺服器
│   ├── search.db.json             # 頁面資料庫
│   └── search.db_index.json       # 搜尋索引
├── start_search_engine.sh         # 啟動腳本
├── stop_search_engine.sh          # 停止腳本
└── logs/                          # 日誌目錄
    ├── backend.log                # 後端日誌
    ├── frontend.log               # 前端日誌
    ├── backend.pid                # 後端進程 ID
    └── frontend.pid                # 前端進程 ID
```

## 🔧 開發指南

### 自訂前端樣式
編輯 `build/index.html` 中的 CSS 部分來自訂外觀：

```css
/* 修改主色調 */
.header {
    background: linear-gradient(135deg, #your-color-1 0%, #your-color-2 100%);
}

/* 修改搜尋框樣式 */
.search-box {
    border-radius: 25px; /* 調整圓角 */
    box-shadow: 0 10px 30px rgba(0,0,0,0.1);
}
```

### 新增 API 端點
在 `src/server.cpp` 中新增路由：

```cpp
m_server->Get("/your-endpoint", [this](const httplib::Request& req, httplib::Response& res) {
    // 處理邏輯
    res.set_content("回應內容", "application/json");
});
```

### 修改搜尋演算法
編輯 `src/json_storage.cpp` 中的 `calculateScore` 函數：

```cpp
double JsonIndexer::calculateScore(const std::string& term, int doc_id) {
    // 自訂評分演算法
    return your_custom_score;
}
```

## 🐛 故障排除

### 常見問題

1. **Port 被佔用**
   ```bash
   # 檢查 port 使用情況
   lsof -i :8080
   lsof -i :3000
   
   # 停止佔用的進程
   kill -9 <PID>
   ```

2. **後端啟動失敗**
   ```bash
   # 檢查編譯是否成功
   cd build && make
   
   # 查看後端日誌
   cat logs/backend.log
   ```

3. **前端無法連接後端**
   - 檢查後端是否在 port 8080 運行
   - 確認防火牆設定
   - 檢查 CORS 設定

4. **搜尋結果為空**
   - 確認已爬取資料
   - 檢查索引是否建立
   - 查看後端日誌

### 日誌檔案
- `logs/backend.log`: 後端 API 伺服器日誌
- `logs/frontend.log`: 前端靜態檔案伺服器日誌

## 📈 效能優化

### 前端優化
- 使用 CDN 載入外部資源
- 實作搜尋結果快取
- 使用虛擬滾動處理大量結果

### 後端優化
- 實作搜尋結果快取
- 使用連線池管理資料庫連線
- 實作非同步搜尋處理

## 🔒 安全考量

- 實作 API 速率限制
- 驗證搜尋輸入
- 使用 HTTPS 加密通訊
- 實作存取控制

## 📞 支援

如果遇到問題，請檢查：
1. 所有服務是否正常啟動
2. 日誌檔案中的錯誤訊息
3. 網路連線是否正常
4. 防火牆設定是否正確

---

**享受你的迷你搜尋引擎！** 🎉
