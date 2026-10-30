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

## v3.0 圖片明暗與機身按鍵

- 官方資料核對：BOOT=GPIO0、PWR=GPIO18，皆為低電位觸發；電池保持供電為 GPIO17 高電位。
- 新增五級圖片明暗（−40、−20、0、+20、+40）、網頁原圖預覽與直接套用、BOOT／PWR 短按、BOOT 長按還原及 PWR 長按電池關機。
- 原圖保存改用 LittleFS 的 120,000-byte RGB 原圖與 CRC-32；NVS 僅保存小型設定與對應原圖 CRC。原子替換檔案，重啟不重畫螢幕。
- C++ 測試在 ASan／UBSan 下通過：圖片協定、明暗上下限、五級平均明暗單調性、標準 CRC-32、短按防彈跳、長按只觸發一次。
- JavaScript 測試通過：色階裁切、五級明暗、RGB／RGBA 往返與 120,000-byte 原圖格式。
- 本機模擬瀏覽器確認：+20 預覽與傳送、−20 直接套用目前圖片、重新整理自動載入原圖及明暗值。
- v2.1 現有韌體已備份在本機，SHA-256 與先前裝置紀錄一致。
- v3.0 編譯通過：1,249,907 bytes（37%）；靜態 RAM 60,864 bytes（18%）。初次實機寫入校驗通過；啟動狀態 `version=3.0`、`ok=true`、兩顆按鍵均為放開狀態。升級後需先重傳圖片，啟用新的原圖保存功能。
- 真實 Wi-Fi 原圖保存已確認：裝置回報 `has_source=true`、`source_saved=true`。
- 使用者配合實測 BOOT 短按：捕捉到 `boot_pressed=true`，明暗 `0 → +20`，job 5 刷新完成，`applied_brightness=20`。
- 使用者配合實測 PWR 短按：明暗 `+20 → 0`，job 6 刷新完成，`applied_brightness=0`。
- 兩次都收到 `DONE`；按鍵調整需等待電子紙約 20 秒刷新。BOOT 按住 1 秒會還原標準，故操作時應輕點後放開。
- 重啟實測：`job=0`、`has_source=true`、`source_saved=true`、`brightness=0`、`applied_brightness=0`，原圖 CRC 驗證與明暗設定恢復成功，未重新刷新目前畫面。
