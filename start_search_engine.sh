#!/bin/bash

# 迷你搜尋引擎啟動腳本
# 同時啟動後端 API 伺服器和前端靜態檔案伺服器

echo "🚀 啟動迷你搜尋引擎系統..."

# 檢查是否在正確的目錄
if [ ! -f "build/mini_search_engine" ]; then
    echo "❌ 錯誤: 找不到可執行檔案 build/mini_search_engine"
    echo "💡 請先編譯專案: cd build && make"
    exit 1
fi

# 檢查 Python 是否可用
if ! command -v python3 &> /dev/null; then
    echo "❌ 錯誤: 找不到 Python3"
    echo "💡 請安裝 Python3"
    exit 1
fi

# 建立日誌目錄
mkdir -p logs

echo "📊 系統資訊:"
echo "   - 後端 API: http://localhost:8080"
echo "   - 前端介面: http://localhost:3000"
echo "   - API 文件: http://localhost:3000/api-docs.html"
echo ""

# 啟動後端 API 伺服器
echo "🔧 啟動後端 API 伺服器..."
cd build
./mini_search_engine --server --server-port 8080 > ../logs/backend.log 2>&1 &
BACKEND_PID=$!

# 等待後端啟動
echo "⏳ 等待後端服務啟動..."
sleep 3

# 檢查後端是否成功啟動
if ! kill -0 $BACKEND_PID 2>/dev/null; then
    echo "❌ 後端服務啟動失敗"
    echo "📋 後端日誌:"
    cat ../logs/backend.log
    exit 1
fi

echo "✅ 後端 API 伺服器已啟動 (PID: $BACKEND_PID)"

# 啟動前端靜態檔案伺服器
echo "🌐 啟動前端靜態檔案伺服器..."
python3 static_server.py > ../logs/frontend.log 2>&1 &
FRONTEND_PID=$!

# 等待前端啟動
sleep 2

# 檢查前端是否成功啟動
if ! kill -0 $FRONTEND_PID 2>/dev/null; then
    echo "❌ 前端服務啟動失敗"
    echo "📋 前端日誌:"
    cat ../logs/frontend.log
    kill $BACKEND_PID 2>/dev/null
    exit 1
fi

echo "✅ 前端靜態檔案伺服器已啟動 (PID: $FRONTEND_PID)"
echo ""

# 顯示服務狀態
echo "🎉 迷你搜尋引擎已成功啟動！"
echo ""
echo "📱 使用方式:"
echo "   1. 開啟瀏覽器訪問: http://localhost:3000"
echo "   2. 查看 API 文件: http://localhost:3000/api-docs.html"
echo "   3. 直接使用 API: http://localhost:8080/search?q=關鍵字"
echo ""
echo "📊 服務狀態:"
echo "   - 後端 API: http://localhost:8080 ✅"
echo "   - 前端介面: http://localhost:3000 ✅"
echo ""
echo "🛑 停止服務: 按 Ctrl+C 或執行 ./stop_search_engine.sh"
echo ""

# 儲存 PID 到檔案
echo $BACKEND_PID > ../logs/backend.pid
echo $FRONTEND_PID > ../logs/frontend.pid

# 等待用戶中斷
trap 'echo ""; echo "🛑 正在停止服務..."; kill $BACKEND_PID $FRONTEND_PID 2>/dev/null; echo "✅ 服務已停止"; exit 0' INT

# 監控服務狀態
while true; do
    if ! kill -0 $BACKEND_PID 2>/dev/null; then
        echo "❌ 後端服務意外停止"
        kill $FRONTEND_PID 2>/dev/null
        exit 1
    fi
    
    if ! kill -0 $FRONTEND_PID 2>/dev/null; then
        echo "❌ 前端服務意外停止"
        kill $BACKEND_PID 2>/dev/null
        exit 1
    fi
    
    sleep 5
done
