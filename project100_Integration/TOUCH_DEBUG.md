# 觸控除錯與驗證

## 本次修正

`M2_TouchRaw_Init()` 在 GPIO 初始化後，主動完成一次 `0x90`
（PD1:PD0 = 00）轉換，再等待 1 ms，確保 PENIRQ 已啟用。
啟動轉換的資料不算觸控樣本。原本必須先讀到 IRQ LOW 才會送命令；
若觸控 IC 留在 PENIRQ 停用狀態，這個先後相依會阻止後續取樣。
MCU reset 不保證同時重置外部觸控 IC。

保留 PC6 CLK、PC7 CS、PC8 DIN、PC9 DO、PC10 IRQ 與 LCD SPI2 DMA 配置。
`pressed` 仍以 PENIRQ LOW 為按下，HIGH 為放開。
參考 [TI ADS7846 datasheet 的 PENIRQ OUTPUT](https://www.ti.com/document-viewer/ADS7846/datasheet)。
此驅動仍只輸出原始觸控座標，尚未校正或轉成 Dashboard 點擊事件。

## 板上驗證（2026-10-26）

- 使用 STM32CubeIDE 1.19.0 附帶的 ARM GCC 13.3.rel1 編譯、連結成功。
- 使用 STM32CubeProgrammer 2.20.0 燒錄已連接的 STM32F407 Discovery，verify 成功。
- 修正前直接讀 RAM，`polls` 從 13,180 增加至 14,099；因此除錯視窗
  顯示固定 1 並非當時韌體真正停止輪詢。
- 修正後實際按壓：`polls=854`、`samples_ok=111`、
  `pen_down_edges=5`、`pen_up_edges=5`、`invalid_samples=0`。
- 同一快照：`initialized=1`、放開時 `pressed=0`、
  `penirq_arm_commands=1`、`dma_safety_fail=0`。
- 原始 X 範圍 1842–2479，Y 範圍 1440–1965；這是測試按壓範圍，
  不是面板校正邊界。

## 在 CubeIDE 觀察

重新啟動 Debug session，讓符號對應新的 `Debug/Integration.elf`。
按 Resume，使用 **Live Expressions** 觀察；一般 Expressions 在程式
執行時可能保留上次暫停的值。若只用一般 Expressions，先持續執行、
按壓，再 Suspend 查看累計，避免把 CPU 暫停時的固定值當作輪詢故障。

| 變數 | 正常行為 |
|---|---|
| `m2_touch_raw_stats.initialized` | 初始化後固定為 1 |
| `m2_touch_raw_stats.polls` | 有無觸碰都約每秒增加 50 |
| `m2_touch_debug.service_calls` | 主迴圈持續呼叫時增加 |
| `m2_touch_debug.last_service_ms` | 隨 HAL tick 增加 |
| `m2_touch_debug.irq_pin_high` | 放開 1，按下 0（取樣前的電位） |
| `m2_touch_raw_stats.pressed` | 按住 1，放開 0 |
| `m2_touch_raw_stats.pen_down_edges` / `pen_up_edges` | 各次按下／放開累計 |
| `m2_touch_raw_stats.samples_ok` | 按住時增加 |
| `m2_touch_debug.last_median_x` / `last_median_y` | 最近原始座標 |
| `m2_touch_debug.reject_bits` | 最近座標取樣的拒收原因；0 正常，1 X、2 Y 到端點 |

若仍量到「按下 HIGH」，先對照 PC10 與 `irq_pin_high`；不要直接反轉判斷。
本次板上讀值已證實 LOW 路徑能產生按下事件及有效 ADC 資料。
請在 CS HIGH、非轉換期間量測，並確認量測點與共地。

修正前的 ELF 備份在 `Debug/Integration.before-touch-fix.elf`（本機編譯產物，不納入 Git）。
