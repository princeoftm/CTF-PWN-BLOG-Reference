# Chronicle

你被要求審查一個 Redis ledger module。服務會將短暫的 ledger annotation
排程，並提供封存與還原功能。請取得服務內的 flag。

這是一題 white-box Pwn 題；題目提供的 module 原始碼、建置檔與 Redis
設定都是分析範圍的一部分。

## 連線資訊

主辦提供一個 Redis endpoint：

```text
Host: <organizer-provided-host>
Port: 6379
Authentication: none
```

例如：

```bash
redis-cli -h <host> -p 6379 PING
```

服務不提供 shell、HTTP endpoint 或其他外部 callback service。請使用
Redis RESP-compatible client；`EXPORT` 與 `IMPORT` 涉及 binary-safe 資料，
不建議透過會改寫 binary 的文字工具處理。

## 可用指令

```text
CHRONICLE.NEW <delay_ms> <label> <annotation>
CHRONICLE.SHOW <id>
CHRONICLE.EXPORT <id>
CHRONICLE.IMPORT <archive>
```

輸入限制：

- `delay_ms` 必須介於 `10` 與 `86400000` 之間。
- `label` 最多 31 bytes。
- `annotation` 最多 79 bytes。
- `NEW` 與 `IMPORT` 會建立新的排程項目並回傳 numeric id。
- `SHOW` 回傳 id、狀態、label、ticket、delay 與 result。
- `EXPORT` 回傳一段 binary archive；`IMPORT` 接受一段 binary archive。

Redis 內已有 tenant、ingestion、audit 與 retention 相關資料，可使用被允許
的唯讀 Redis 指令查看。服務狀態是 ephemeral，重新啟動後資料會重新建立。

## 目標

分析 module 並利用服務取得 flag。請勿攻擊主辦基礎設施或其他隊伍的
instance。
