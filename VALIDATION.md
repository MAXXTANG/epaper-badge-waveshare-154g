# 驗證紀錄（2026-10-30）

目標板：Waveshare ESP32-S3-ePaper-1.54G。

- 實機識別：ESP32-S3-PICO-1、8 MB Flash、8 MB PSRAM。
- 原始 Flash：8,388,608 bytes 已備份；esptool verify-flash 顯示 digest matched。
- 備份 SHA-256 與裝置資訊：`backups/manifest.json`。
- 編譯：Arduino-ESP32 3.3.12，OPI PSRAM / 8 MB Flash / USB Hardware CDC。
- 程式大小：627,915 bytes（18%）；靜態 RAM：40,012 bytes（12%）。
- 主機測試：AddressSanitizer 與 UndefinedBehaviorSanitizer 下通過 RGB565／RGB888、1/20/244/512/65536 bytes 分段、CRC、尺寸上限、取消重試、雙線性縮放與四色像素封裝。
- 燒錄：bootloader、partition table、boot_app0、application 均完成寫入與校驗。
- 正常啟動：USB 紀錄 `READY`、`BLE advertising as Paper154G`、`DISPLAY=ok`。
- 實際 BLE 連線：USB 紀錄 `CONNECTED`、`BLE=connected`。
- iPhone 圖片接收與刷新：等待使用者在 Bluefruit Connect 傳送第一張圖片；尚未宣稱完成。

原始測試紀錄在 `.dev/`。螢幕刷新完成是依韌體 BUSY 訊號與狀態確認；實際畫面內容仍需由使用者目視核對。

## Wi-Fi ＋ BLE v2.0

- 編譯：1,323,083 bytes（39%）；靜態 RAM 71,272 bytes（21%）。
- 寫入與 Flash hash 校驗通過，使用 watchdog reset 正常啟動。
- 電子紙操作卡：實機回報 `DONE`，`busy=false`、`ok=true`。
- BLE：新版實機回報 `ble=true`，保留原 Nordic UART Service 及圖片解析協定。
- Wi-Fi：實機回報 1 台客戶端連線；使用者確認 iPhone Safari 已看到選照片與預覽頁面。
- 瀏覽器 UI：以 240 × 240 四角標記測試圖確認整張圖都進入預覽；滿版 120% 縮放、重設與模擬傳送完成流程通過。模擬 API 測試不等於真實板子上傳驗證。
- JavaScript：正方形／長方形完整顯示與滿版、旋轉、平移限界、四色封裝與 10,000-byte 格式測試通過。
- 使用者的原圖附件本機路徑已不存在，無法以該原檔完成自動化選圖測試。
- 開發診斷 `HTTPTEST` 的 ESP32 自回連 HTTP 請求回報 -1；這條測試路徑未通過。手機從外部連線可讀取網頁，由使用者確認；外部實際上傳結果另記。

### 實際 iPhone 傳圖結果

- 外部 Wi-Fi 上傳收到完整 10,000 bytes，job 2。
- 刷新期間，USB 狀態持續回覆 `REFRESHING`，BLE 保持連線。
- 電子紙 BUSY 訊號完成，`busy=false`、`ok=true`。
- 使用者確認：**「已完整顯示」**。
- v2.0 同時回報 `DONE SAVE FAILED`：重複更新 10 KB NVS 圖片容量不足。v2.1 改為只儲存已有圖片標記，並移除無法回連的開發診斷；利用電子紙本身保留最後畫面。

### v2.1 最後檢查

- 編譯通過：1,200,739 bytes（35%）；靜態 RAM 70,648 bytes（21%）。
- 燒錄及 hash 比對成功。
- 重啟後實機狀態：`version=2.1`、`busy=false`、`ok=true`、`has_image=true`、`job=0`（未重新刷新目前圖片）。
- BLE 仍連線，Wi-Fi 有 1 台客戶端連線。
- 本機 UI 測試伺服器與臨時瀏覽器分頁已關閉；正式頁面由 ESP32 提供。
