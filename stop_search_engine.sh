#!/bin/bash

# 停止迷你搜尋引擎服務

echo "🛑 停止迷你搜尋引擎服務..."

# 檢查 PID 檔案是否存在
if [ -f "logs/backend.pid" ]; then
    BACKEND_PID=$(cat logs/backend.pid)
    if kill -0 $BACKEND_PID 2>/dev/null; then
        echo "🔧 停止後端 API 伺服器 (PID: $BACKEND_PID)..."
        kill $BACKEND_PID
        sleep 2
        if kill -0 $BACKEND_PID 2>/dev/null; then
            echo "⚠️  強制停止後端服務..."
            kill -9 $BACKEND_PID
        fi
        echo "✅ 後端服務已停止"
    else
        echo "ℹ️  後端服務未運行"
    fi
    rm -f logs/backend.pid
else
    echo "ℹ️  找不到後端 PID 檔案"
fi

if [ -f "logs/frontend.pid" ]; then
    FRONTEND_PID=$(cat logs/frontend.pid)
    if kill -0 $FRONTEND_PID 2>/dev/null; then
        echo "🌐 停止前端靜態檔案伺服器 (PID: $FRONTEND_PID)..."
        kill $FRONTEND_PID
        sleep 2
        if kill -0 $FRONTEND_PID 2>/dev/null; then
            echo "⚠️  強制停止前端服務..."
            kill -9 $FRONTEND_PID
        fi
        echo "✅ 前端服務已停止"
    else
        echo "ℹ️  前端服務未運行"
    fi
    rm -f logs/frontend.pid
else
    echo "ℹ️  找不到前端 PID 檔案"
fi

# 清理可能的殘留進程
echo "🧹 清理殘留進程..."
pkill -f "mini_search_engine" 2>/dev/null || true
pkill -f "static_server.py" 2>/dev/null || true

echo "✅ 所有服務已停止"
