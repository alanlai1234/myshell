# myshell 自動測試

這套測試對應 `alanlai1234/myshell` 的 commit
`105c37a5b14933361b75da7ff2437db8bc4b736d`，共有 **100 項**。
只新增測試與報告，沒有修改 shell 實作。

## 執行

在 repository 根目錄執行；需要 POSIX 系統、Python 3.9 以上與 C compiler。
測試只使用 Python 標準函式庫，不需要 pip install。

```sh
make -B
python3 tests/run_tests.py --shell ./myshell --suite all --json /tmp/myshell-results.json
```

`make -B` 強制從原始碼重新編譯，避免誤用 repository 內既有的 macOS ARM64 執行檔。
測試失敗時 runner 回傳 1；成功回傳 0；設定或 helper 編譯錯誤回傳 2。

可以單獨執行一組，或用名稱篩選：

```sh
python3 tests/run_tests.py --shell ./myshell --suite core
python3 tests/run_tests.py --shell ./myshell --suite stress
python3 tests/run_tests.py --shell ./myshell --suite interactive
python3 tests/run_tests.py --shell ./myshell --suite review
python3 tests/run_tests.py --shell ./myshell --suite all --filter glob
python3 tests/run_tests.py --suite all --list
```

| 組別 | 項數 | 範圍 |
| --- | ---: | --- |
| core | 80 | 指令與引數、cd/pwd/which/exit、批次、註解、重新導向、管線、萬用字元、錯誤恢復、buffer 邊界 |
| stress | 6 | 1 MiB 管線、10 層管線、連續指令／管線、檔案描述符洩漏 |
| interactive | 3 | 真實 PTY 下的歡迎訊息、prompt、cd、exit、Ctrl-D |
| review | 8 | 作業說明未提供，需對照教授規格確認的行為 |
| memory | 3 | 空輸入、單一指令、大量萬用字元展開；應搭配 sanitizer 執行 |

README 原本只有專案標題，沒有作業要求。`core` 的預期根據現有功能與通常的指令語意制定，
**不是老師的隱藏測試或評分標準**。`review` 包含 EOF 最後一行、Tab、CRLF、
重新導向後仍有引數、cd 的重新導向、pipeline 裡的 exit、缺少 HOME、絕對路徑無匹配時的保留。
這些測試照樣回報實際失敗，不會用 skip 或 expected failure 隱藏問題；是否屬於必修功能須另確認。
不要求 quotes、環境變數展開、工作控制、`&&`、`||`、`?`、`[]` 等未確認功能。

## 記憶體檢查

`myshell.c` 已經 `#include "execute.c"`，編譯時只給 `myshell.c`。

```sh
cc -Wall -Wextra -g -fsanitize=address,undefined -fno-omit-frame-pointer myshell.c -o /tmp/myshell-asan
python3 tests/run_tests.py --shell /tmp/myshell-asan --suite memory --json /tmp/myshell-asan-results.json
```

一般執行檔的 memory 組通過只表示 smoke check 通過；**不能當作沒有記憶體問題的證明**。
Runner 會把 AddressSanitizer 與 UBSan 診斷視為失敗。沒有停用 sanitizer 的檢查。

## macOS 編譯環境

本次機器的預設 MacOSX27.0 SDK 與 linker 不相容，原始 `make` 報 `unknown architecture arm64e.x1-macos`。
這是當地工具鏈問題；本次改用機器已安裝的 MacOSX15.4 SDK 編譯，沒有改動專案程式碼。
如果你也遇到相同錯誤，而且有該 SDK，可以使用：

```sh
clang -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk -Wall -Wextra -g myshell.c -o myshell
python3 tests/run_tests.py --shell ./myshell --suite all --cc 'clang -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk'
```

Sanitizer 的編譯命令也可加入同樣的 `-isysroot`。不需要更改系統設定。
`--cc` 只用來編譯 `tests/probe.c`；shell 需先自行編譯。也可透過 `CC` 和 `CFLAGS` 指定 helper compiler。

## 測試如何隔離

每項測試建立自己的暫存目錄及 HOME，不會讀寫你的真實 HOME。
`probe.c` 提供固定引數輸出、指定 exit code、有限長度資料及 fd 數量，避免依賴 echo option、locale 或 ls 排序。
萬用字元結果比對集合，不強制 readdir 的順序。正常測試比對 stdout、stderr、狀態與檔案內容；
錯誤測試另外確認下一個指令仍能執行。每個 shell 都有獨立 process group 和 timeout，
失敗或結束後清除同組子程序。

JSON 含測試名稱、組別、狀態、耗時、失敗原因與 stdout/stderr transcript。
單次 stdout/stderr transcript 只保留前 8,000 字元，避免大資料測試撐大報告。
測試只在 macOS ARM64 實跑；Linux 還沒有實測，平台差異應以實際執行結果為準。

詳細結果與重現方式：[review-105c37a.md](review-105c37a.md)。
