# myshell 閱讀與測試結果

對象：[alanlai1234/myshell](https://github.com/alanlai1234/myshell)，
基準 commit `105c37a5b14933361b75da7ff2437db8bc4b736d`。
環境：macOS ARM64，Apple clang 21，MacOSX15.4 SDK，Python 3.9。
這份報告記錄該快照的結果，之後改版應重新執行，不能當成新版本的測試成績。

## 已檢查的檔案

| 檔案 | 檢查內容 |
| --- | --- |
| README.md | 只有 `# myshell`，沒有正式規格 |
| myshell.c | 161 行；read/逐字解析、指令節點、互動模式、cleanup |
| execute.c | 334 行；launcher、builtins、redirection、pipeline、wildcard |
| myshell.h | 23 行；重複的 include、結構與全域宣告；目前實作沒有 include 它 |
| makefile | 目標依賴 myshell.c 與 execute.c；只編譯 myshell.c，因其直接 include execute.c |
| test | 只有 `cd ..`、`ls`、`pwd`，沒有 assertions |
| myshell | 檢查格式與 hash：已編譯的 Mach-O ARM64；測試使用重新編譯版本 |

Safari 下載快照的全部 7 個 Git blob hash 都與 GitHub 指定 commit 的完整 tree 相符；
檔案清單沒有截斷。未下載或檢查其他分支、歷史版本與課程平台上的規格。

## 執行結果

| 測試方式 | 通過 | 失敗 |
| --- | ---: | ---: |
| core，一般編譯版本 | 75 | 5 |
| stress，一般編譯版本 | 4 | 2 |
| interactive，一般編譯版本 | 3 | 0 |
| review，一般編譯版本 | 0 | 8 |
| memory，一般編譯版本 smoke check | 3 | 0 |
| **一般版本合計** | **85** | **15** |
| **另外以 ASan + UBSan 重跑 memory** | **0** | **3** |

100 項是不同名稱的測試案例；sanitizer 的 3 項是重新執行相同 memory 案例，不另外算成 103 個不同案例。
記憶體未定義行為可能因配置器與平台而改變一般版本的結果。
另外以 `/bin/bash` 驗證 10 項萬用字元案例，全部通過；以 `/bin/sh` 驗證 6 項 stress 案例，全部通過。
這是針對測試預期的交叉檢查，不表示所有課堂 shell 功能都必須與 Bash 相同。

## 優先修正

### 1. 未初始化的鏈結串列節點會造成非法讀取或 free

位置：`myshell.c:22`、`myshell.c:69`、`myshell.c:71`；`execute.c:277`、`execute.c:298`。
`malloc` 後的 `next` 和 `arg` 沒有設成 NULL；之後立即讀取它們。
空輸入在 cleanup 也可能碰到未初始化的 sentinel。

重現：依 README 編譯 sanitizer 版，再執行 memory 組。
三項都失敗；單一 echo 捕捉到 `myshell.c:25` 的 misaligned member access，地址呈 `0xbebebebebebebebe`，
接著 ASan 報 SEGV。一般編譯版本碰巧通過不能排除此問題。

建議：每個新節點明確初始化；處理 command 與 pipe 節點完整的所有權及釋放。
靜態閱讀也發現 add_arg 覆蓋舊 arg 與 pipe/input 配置後未完整回收；本次沒有對這些洩漏逐一計量。

### 2. 外部程式回傳大於 1，會被當成 shell 的 exit

位置：`myshell.c:95` 與 `execute.c:330`。
`execute()` 混用 child exit status 與「要不要結束 shell」訊號。
子程序正常回傳 7 時，main 看見 `ret > 1` 就結束，而且 shell 最後回傳 0。

對應：`nonzero_external_status_does_not_stop_shell`。
測試 helper 回傳 7 後執行 `echo alive`，實際完全沒有輸出。
可自行編譯 helper 後重現：

```sh
cc tests/probe.c -o /tmp/myshell-probe
printf '/tmp/myshell-probe status 7\necho alive\n' | ./myshell
```

預期輸出 `alive`。建議將 shell exit 請求與外部程式狀態分開表示。
同時檢查 waitpid 回傳值與 `WIFEXITED`，再使用 `WEXITSTATUS`；後兩項是靜態審查建議，尚未單獨故障注入。

### 3. 萬用字元 `*` 無法正確匹配零個字元

位置：`execute.c:263`。
DP 的星號轉移沒有處理從同一列左方狀態而來的零字元匹配。
`a*.txt` 漏掉 `a.txt`；`a*l*.txt` 也漏掉應匹配的 `alpha.txt`。

對應：`glob_star_matches_zero_chars`、`glob_middle`。
預期星號可匹配零個以上字元；修正時同時驗證 prefix、suffix、多個星號與無匹配。

### 4. 絕對路徑的 wildcard 被截斷

位置：`execute.c:230`。
為了 opendir 將原始 token 的斜線改成 NUL，後續匹配與輸出仍使用同一個 token。
`/tmp/.../glob/al*.txt` 最後變成只有 `/tmp/.../glob`，匹配失敗時也沒有還原。

對應：`glob_absolute_directory`；`glob_absolute_unmatched_preserves_token` 放在 review 組確認規格。
建議保留完整 token，以另外的目錄字串做 opendir。

### 5. 管線描述符持續洩漏

位置：`execute.c:run()`，特別是 `212–215` 附近。
父程序在下一階段啟動後未關閉之前的 read end，最後的 read end 也持續保留。
對應：`fd_growth_after_pipelines`、`fd_growth_in_long_pipeline`。

實測：連續 40 條管線後，後續外部程式繼承的額外 fd 從 **0 變成 40**；
長管線的最後階段從 **0 變成 24**。
建議父程序及 child 都只保留目前所需的端點，使用後關閉。
另外，redirection 的 open fd 在 dup2 後也未關閉；此點來自靜態閱讀，未在這兩個計數案例單獨測量。

### 6. 管線最後一個 cd 修改父 shell 的 cwd

位置：`execute.c:208`。
`!is_pipe` 只表示「後面沒有管線階段」，不能表示整條指令不是管線。
`echo ignored | cd sub` 之後的 `pwd` 會變成 sub。

對應：`final_cd_isolated_from_parent`。
建議只有單獨執行的 cd 才在父程序改變 cwd，管線階段則在 child 執行。
這是通常的 pipeline 隔離語意；仍應與教授規格核對。

## review 組的 8 項失敗

| 案例 | 實際觀察 |
| --- | --- |
| eof_without_newline | 沒有最後換行的指令直接被丟棄 |
| tab_separates_arguments | Tab 被視為指令名稱中的字元 |
| crlf_batch_lines | 輸出包含額外 CR |
| arguments_after_redirection | `echo first > out second` 的 second 被丟棄 |
| cd_redirection_changes_parent_directory | `cd sub > out` 被父程序當成 too many arguments |
| exit_in_pipeline_preserves_parent | pipeline 中的 exit 結束整個 shell |
| missing_home_is_handled | 未設定 HOME 時發生 SIGSEGV，源於對 NULL 做 strlen |
| glob_absolute_unmatched_preserves_token | 無匹配時原始完整 token 被截成目錄 |

缺少 HOME 的 crash 與 token 被截斷本身值得修正；其餘需求及應採哪種行為需看完整作業規格。
目前沒有把這 8 項全數當成老師一定會扣分的項目。

## 已通過的代表項目

一般版的 stdout/檔案比對已涵蓋 `>` 截斷、`>>` 附加、`<`、同時輸入輸出、相鄰操作符、
pipeline 與 redirection 優先關係、pwd/which、cd 相對／絕對／HOME、未知指令後恢復、
語法錯誤後恢復、4097 字元引數、100 個引數、10-byte read buffer 邊界及 PTY 互動。
1 MiB 管線與多階段管線本次能完成；這不排除長期執行因 fd 或 heap 洩漏耗盡資源。

完整逐項結果見 `baseline-105c37a.json`；記憶體診斷見 `sanitizer-105c37a.json`。
報告內的使用者路徑及隨機暫存路徑已改成佔位字串；數值、案例名稱與診斷保留。
