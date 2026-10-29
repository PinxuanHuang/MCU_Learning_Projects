# 五點校正未出現：ST-LINK 檢查紀錄

2026-10-26 檢查確認 ST-LINK 可連線、CPU 正在執行，CFSR/HFSR 為 0。
Flash 讀回內容與當時本機 ELF 轉出的映像一致；不是燒錄了不同版本。

## 原因與修正

`main.c` 原本設定 `ENABLE_TOUCH_CALIBRATION=1`，但
`ENABLE_TOUCH_RAW_TEST=0`。校正入口以兩者的 AND 控制，
因此初始化、校正呼叫與主迴圈觸控服務都被編譯排除。
原 ELF 沒有 `TouchCal_RunBoot`、觸控狀態或校正狀態符號。

已將 RAW 開關改為 1，加入 `#error` 阻止再次使用這個矛盾設定。
目前 raw driver 也缺少先前的 PENIRQ 啟動處理，已補回啟動時
完整送出 `0x90` 轉換並等待 1 ms；這筆啟動資料不列入有效觸控樣本。

ARM GCC 編譯連結成功，ST-LINK Under Reset 燒錄及 verify 成功。
開機後直接讀到 `touch_cal_debug.result=1`（RUNNING）、`point_index=0`、
`touch_raw_stats.initialized=1`、`dma_safety_fail=0`。
稍後未收到觸控而讀到 `result=3`（TIMEOUT），符合既有 12 秒期限。
使用者隨後確認看到十字，並要求重新開始。透過 ST-LINK Reset 後完成五點，
實際讀到 `result=2`（PASS）、`calibrated=1`、`touch_calibration.valid=1`、
`point_index=4`、`samples_seen=40`、`rejected_contacts=0`、
`max_error_px=7`、`dma_safety_fail=0`。
7 px 是五個校正點的最大擬合殘差向上取整，並非獨立全螢幕精度驗證。

## 操作

按 RESET 後依序按住左上、右上、右下、左下、中央的十字約半秒，
每點放開後才會前進。每點的等待期限為 12 秒；逾時會回到 Dashboard。
可再按 RESET 重試。

Live Expressions 可觀察：

- `touch_cal_debug.result`：0 未執行、1 執行中、2 成功、3 超時、4 擬合失敗、5 繪圖或前置條件錯誤。
- `touch_cal_debug.point_index`：0～4，當前校正點。
- `touch_cal_debug.samples_seen`、`rejected_contacts`：取樣與拒收狀態。
- `touch_cal_debug.calibrated`、`touch_calibration.valid`：成功後為 1。

此版本校正係數只存 RAM，Reset 後會重做校正；校正期間是啟動階段的
blocking 流程，Dashboard 主迴圈尚未開始。校正後只做座標映射診斷，
尚未注入 Dashboard 手勢事件。
