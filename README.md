# ETON

一个用 **原生 Win32 C API**（不依赖 MFC / Qt / .NET / Electron）实现的、类 Notepad++ 的 Windows 文本编辑器。

- 源码可直接用 Visual Studio 生成工具（MSVC）编译为单个 `eton.exe`
- 静态链接运行库（`/MT`）+ Scintilla + Lexilla，编译产物仅依赖系统 DLL，可直接拷贝到其他 Windows 机器运行
- 内嵌 Common Controls v6 清单，自动获得系统主题（含暗色标题栏/控件）

---

## 功能特性

| 类别 | 说明 |
| --- | --- |
| 多标签 | 自定义标签栏（含 × 关闭按钮），可同时编辑多个文件；Ctrl+Tab / Ctrl+Shift+Tab（或 Ctrl+PgDn/PgUp）循环切换；标签放不下自动出现 ◀▶ 滚动按钮并支持滚轮滚动；中键关闭、双击空白新建、悬停显示完整路径、右键菜单（关闭/关闭其他/关闭全部） |
| 界面语言 | 简体中文 / English 双语界面，`帮助` 菜单底部直接勾选切换，运行时即时生效（菜单、对话框、消息、状态栏全部跟随）；选择保存在 session.ini，未选择过时首次启动跟随系统 UI 语言 |
| 语法高亮 | 9 种语言：C / C++ / C# / Java / JavaScript / Python / XML(HTML) / JSON / SQL（基于 Scintilla + Lexilla 词法器）；按扩展名自动识别，也可在语言菜单手动切换；语法语言下显示代码折叠边距（点击 +/− 折叠/展开） |
| 多编码 | ANSI(系统代码页) / UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE，打开时自动探测 BOM，状态栏显示当前编码；另存 ANSI 时若内容含无法表示的字符会提示乱码风险 |
| 行尾转换 | CRLF / LF / CR，可一键"转换为…"；新文档及探测不到行尾的文件默认 Unix (LF)，打开已有文件时按内容自动探测并保持 |
| JSON 工具 | 编辑菜单：JSON 格式化（Ctrl+Shift+F）/ JSON 压缩（Ctrl+Shift+M）；有选区时只处理选区，缩进与行尾跟随文档设置；解析失败提示出错行列并定位到出错字符 |
| 查找 / 替换 / 转到 | 非模态查找对话框（查找时可继续编辑，Enter=查找下一个，Esc=关闭）；支持区分大小写、全词匹配、正则表达式、向上/向下、循环查找；命中项全部高亮显示；`替换` 支持单个替换与全部替换（正则支持  分组引用）；`转到行` |
| 书签 | Ctrl+F2 切换当前行书签、F2 / Shift+F2 在书签间跳转，书签行整行高亮 |
| 最近文件 | 自动记录最近打开的文件，菜单可一键重新打开 |
| 行号槽 | Scintilla 内建行号 margin，随编辑区滚动实时同步；当前行自动高亮 |
| 缩放 | Ctrl+= 放大 / Ctrl+- 缩小 / Ctrl+0 复位，状态栏显示缩放比例 |
| 自动换行 | 视图菜单可切换软换行 |
| 状态栏 | 实时显示 字符数 / 行 / 列 / 编码 / 行尾 / 缩放 |
| 主题 | 亮色与暗色两套配色，标题栏、菜单（含下拉/右键弹窗）、对话框、标签栏、编辑区、滚动条、状态栏全部跟随；系统 MessageBox 仍为系统样式 |
| 设置记忆 | 主题 / 自动换行 / 行号 / 字号 / 界面语言随退出保存、启动恢复 |
| 启动参数 | 可 `eton.exe 文件1 文件2 …` 直接打开多个文件 |
| 文件拖放 | 从资源管理器拖文件到窗口即可打开，支持一次拖入多个；已打开的文件切换到对应标签，目录自动忽略 |
| 会话恢复 | 按原顺序记住上次的全部标签（文件与未命名草稿的相对位置不变）及激活标签，下次启动自动恢复（带命令行参数启动时不恢复）；已删除的文件自动跳过 |
| 草稿与备份 | 未命名文档内容每 10 秒保存草稿，崩溃后可恢复；已保存文件的未保存修改也每 10 秒自动备份（autoback），异常退出后再次打开该文件时询问是否恢复；关闭标签或保存后备份自动清除 |
| 外部修改检测 | 保存前检测文件是否被其他程序修改并提示覆盖风险；切回窗口时检测磁盘变化并询问是否重新加载（仅限未编辑的文档） |
| 大文件支持 | 流式加载/保存（分块转码直通 Scintilla、临时文件+原子替换保存），可打开最高 1 GB 的文件，大文件加载/保存带进度框可取消；超过 1 GB 拒绝打开并提示；JSON 格式化等整篇内存操作对超过 100 MB 的文档禁用 |

---

## 项目结构

```
eton/
├── build.bat          # 编译脚本：直接运行为交互模式；"build.bat auto" 为无交互模式（自动化/CI）
├── app.rc             # 将 app.manifest 编为 RT_MANIFEST 资源（视觉样式用）
├── app.manifest       # Common-Controls v6 清单（主题/暗色）+ Per-Monitor V2 DPI 感知
├── eton.exe           # 编译产物（单文件可移植）
├── eton.wxs           # MSI 安装包定义（WiX v7；CI 按发布标签构建）
├── msix/              # MSIX 打包：pack-msix.ps1 + AppxManifest.template.xml
├── deps/              # Scintilla + Lexilla 依赖（内置，编译不再依赖外部目录）
│   ├── scintilla/     #   libscintilla.lib + 头文件
│   └── lexilla/       #   liblexilla.lib + 头文件
├── res/
│   ├── app.png        # 程序图标源图（1080×1080）
│   ├── app.ico        # 程序图标（由 app.png 生成，16–256px 多尺寸）
│   └── make_ico.py    # 从 app.png 生成 app.ico 的脚本（可选，需 Pillow）
└── src/
    ├── common.h       # 全局结构、枚举、跨模块函数声明（含 resource.h、i18n.h）
    ├── resource.h     # 菜单/命令/控件/对话框 的所有 ID 常量
    ├── i18n.h         # 界面多语言：字符串 ID 枚举与接口
    ├── i18n.c         # 中/英文字符串表、T() 取词、主菜单构建、对话框文字覆盖
    ├── version.h      # 版本号定义（VERSIONINFO 资源用；CI 按发布标签生成）
    ├── eton.rc        # 加速键、对话框、图标、版本信息资源（菜单由代码构建）
    ├── main.c         # 程序入口、主窗口过程、命令分发、最近文件、拖放、界面语言切换
    ├── editor.c       # 多标签/文档管理、Scintilla 控件、缩放、词法器
    ├── tabbar.c       # 自绘标签栏（绘制与点击处理）
    ├── session.c      # 会话/草稿持久化与恢复（含界面语言选择）
    ├── fileio.c       # 编码探测、读写、行尾规范化、UTF-8 转换
    ├── jsonfmt.c      # JSON 校验 + 格式化/压缩（单遍解析，RFC 8259）
    └── dialogs.c      # 查找/替换/转到/关于/打开编码 对话框
```

> 编译依赖已内置：`deps/scintilla/libscintilla.lib` + `deps/lexilla/liblexilla.lib`（预编译的静态库及头文件随项目分发，无需外部目录）。

---

## 编译

### 前置条件
- **Visual Studio 生成工具（Build Tools for Visual Studio）** 或完整版 Visual Studio，包含 MSVC 与 Windows SDK。
- 本机验证环境：Visual Studio 生成工具 2026（v19.x，`vcvarsall.bat x64`），Windows SDK。

### 方式一：双击脚本（最简单）
直接双击 `build.bat`（或在命令行运行）。脚本会：
1. 自动定位 MSVC 环境：优先用环境变量 `VCVARS` 指定的 `vcvarsall.bat`，其次用 `vswhere` 查找（支持任意盘符 / 版本 / 发行版，含 Build Tools），最后回退扫描常见安装路径；
2. 调用 `vcvarsall.bat x64` 初始化 MSVC 环境；
3. 用 `rc.exe` 编译资源 `eton.rc` → `build\eton.res`；
4. 用 `cl.exe` 编译 8 个 `.c` 并链接为 `eton.exe`（含 Scintilla + Lexilla 静态库）。

自动化 / CI 场景用无交互模式：`build.bat auto`——不暂停，成功输出 `BUILD_OK` 且退出码为 0，失败输出 `RCFAIL` / `CLFAIL` / `VCVARSFAIL` 且退出码非 0。

> 极少数情况下脚本找不到 MSVC 时，会提示设置环境变量 `VCVARS` 指向 `vcvarsall.bat` 后重试，例如：
> `set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat`

### 方式二：开发者命令提示符
1. 打开 "Developer Command Prompt for VS" 或手动执行 `vcvarsall.bat x64`；
2. 进入项目目录，执行：

```bat
rc /nologo /fo build\eton.res src\eton.rc
rc /nologo /fo build\app.res app.rc
cl /nologo /W3 /utf-8 /MT /O2 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   /I"deps\scintilla" /I"deps\lexilla" ^
   /Fo"build/" /Fe:eton.exe ^
   src\main.c src\editor.c src\tabbar.c src\fileio.c src\dialogs.c src\jsonfmt.c src\session.c src\i18n.c build\eton.res build\app.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /LIBPATH:"deps\scintilla" /LIBPATH:"deps\lexilla" ^
   libscintilla.lib liblexilla.lib ^
   user32.lib gdi32.lib comctl32.lib kernel32.lib shell32.lib shlwapi.lib comdlg32.lib imm32.lib ole32.lib oleaut32.lib
```

### 关于编译告警
本项目编译**无告警**。清单（含 Common-Controls v6 视觉样式）通过 `app.rc` + `app.manifest` 作为 `RT_MANIFEST` 资源编译进 exe（`/MANIFEST:NO` 关闭链接器自带的清单生成，因此**不依赖 `mt.exe`**），既干净又可移植。

### 关键编译选项说明
- `/utf-8`：源码按 UTF-8 读取，避免中文与 `C4819` 编码告警。
- `/MT`：静态链接 C 运行库，生成的 exe 不依赖 `VCRUNTIME*.dll` / `MSVCP*.dll`，便于分发。
- `/DUNICODE /D_UNICODE`：使用宽字符（Unicode）API。
- `libscintilla.lib` + `liblexilla.lib`：Scintilla 编辑器内核 + Lexilla 词法器，静态链接进 exe。
- `app.rc` + `app.manifest`（`RT_MANIFEST` 资源 ID 1）：让程序启用系统视觉样式（主题）与 Per-Monitor V2 DPI 感知（高缩放比下文字清晰、跨屏拖动自动重排），无需 `mt.exe`。

---

## 打包（安装包）

程序本身免安装（单文件 `eton.exe` 可直接拷贝运行）。需要安装包时用以下两种形式，均在 `build.bat` 成功之后进行。

### MSI（WiX，本机 / CI）

```bat
dotnet tool install --global wix
wix build -arch x64 eton.wxs -d Version=0.0.1 -acceptEula wix7 -o eton-0.0.1-x64.msi
```

- 需要 .NET SDK 与 WiX v7 CLI（上面的 `dotnet tool install` 一次性安装）。
- 版本号为**三段式**（如 `0.0.1`），安装到 Program Files 并创建开始菜单快捷方式，per-machine 范围。
- 发布 GitHub Release（tag `v0.0.1`）时 CI 自动构建 exe + MSI 并附加到 Release；配置了签名证书 secrets 时自动用 signtool 签名。本地分发的 MSI 建议自签：
  `signtool sign /fd SHA256 /td SHA256 /tr http://timestamp.digicert.com /f 证书.pfx /p 密码 eton-0.0.1-x64.msi`

### MSIX（微软商店 / Win10 1809+ 侧载）

用 `msix\pack-msix.ps1`（需要 Windows SDK 的 makeappx，图标资产自动从 `res\app.png` 生成）：

```powershell
# 商店提交包：用 Partner Center 的 Package/Identity 值，保持未签名（上架时商店统一签名）
powershell -File msix\pack-msix.ps1 -Name A99C7AF7.ETON -Publisher "CN=15387737-D26A-48FB-9636-57EAD340851A" -Version 0.0.2.0

# 本机测试包：默认测试标识 + 自签名证书（首次会自动创建证书并导出 .cer）
powershell -File msix\pack-msix.ps1 -Sign
```

- 版本号为四段式，且**第 4 段（修订号）必须为 0**——微软商店上传校验会拒绝形如 `0.0.1.1` 的版本（报"包接受验证错误"），只能用 `0.0.2.0` 这类格式；每次提交还需大于上一次已发布的版本。本机侧载不受此限制。
- **Identity（Name + Publisher）必须与已发布版本保持一致**，否则 Windows 视为不同应用、无法覆盖升级。
- 测试包安装：先把脚本导出的 `build\msix\eton-test-signing.cer` 导入 `本地计算机 → 受信任人`，再双击或 `Add-AppxPackage` 安装。

### 版本号来源

`eton.exe` 内嵌版本取自 `src\version.h`（CI 发布时按 tag 自动重新生成）；MSI / MSIX 的版本在打包命令里单独传入。

---

## 使用

启动后：
- **新建 / 打开 / 保存 / 关闭**：文件菜单或工具栏，对应 `Ctrl+N / O / S / W`。
- **切换语言高亮**：语言菜单选择（打开 `.c/.py/.json` 等文件时会按扩展名自动猜测，也可手动改）。
- **切换编码 / 行尾**：编码、行尾菜单；修改后状态栏会更新。新文档默认 LF (Unix) 行尾，已有文件按内容探测（CRLF 优先）。注意：把含非 ANSI 字符的文件"另存为 ANSI"时程序会提示潜在乱码风险。
- **查找替换**：`Ctrl+F` / `Ctrl+H` 打开**非模态**对话框（查找时可继续编辑；Enter=查找下一个，Esc=关闭）；`F3` 查找下一个，`Shift+F3` 查找上一个；可勾选"正则表达式"（替换支持 `` 分组引用），命中的全部匹配会高亮显示。
- **转到行**：`Ctrl+G`。
- **JSON 格式化 / 压缩**：编辑菜单，或 `Ctrl+Shift+F` / `Ctrl+Shift+M`。无选区时处理整个文档，有选区时只处理选区；内容非法 JSON 时弹窗提示出错行列并跳转选中出错字符。
- **缩放**：`Ctrl+=` 放大、`Ctrl+-` 缩小、`Ctrl+0` 复位。
- **自动换行**：视图菜单切换。
- **暗色主题**：在"视图 → 主题"切换（标题栏/标签栏/状态栏/语法高亮配色随之改变）。
- **书签**：`Ctrl+F2` 切换当前行书签，`F2` / `Shift+F2` 上下跳转。
- **代码折叠**：打开语法语言文件后，行号旁出现折叠边距，点击 +/− 折叠或展开。
- **右键菜单**：编辑区右键=剪切/复制/粘贴/全选/打开所在文件夹；标签右键=关闭/关闭其他/关闭全部；标签中键关闭、双击空白新建。
- **切换标签**：`Ctrl+Tab` / `Ctrl+Shift+Tab`（或 `Ctrl+PgDn` / `Ctrl+PgUp`）循环切换。
- **自动备份**：已保存文件的未保存修改每 10 秒备份一次，程序异常退出后再次打开该文件会询问是否恢复；正常保存或关闭后备份自动删除。
- **切换界面语言**：`帮助 → 界面语言` 选择 简体中文 / English，即时生效（无需重启）；下次启动记住上次选择，首次启动跟随系统语言。
- **命令行打开**：`eton.exe path\to\file.txt`，支持多个文件。
- **拖放打开**：把文件从资源管理器拖到窗口上，一次可拖多个；已打开的文件会切换到对应标签页，拖入目录会被忽略。
- **会话恢复**：退出后再启动，自动打开上次编辑的文件标签并回到最后激活的标签；带文件参数启动时只打开参数指定的文件。会话保存在 `%APPDATA%\eton\session.ini`，每次标签变化即落盘，异常退出也不丢。
- **草稿**：在"未命名"标签里输入的内容会自动保存草稿（`%APPDATA%\eton\drafts\`，每 10 秒定时落盘），程序崩溃或直接退出后，下次启动会以"未命名 N"标签恢复。草稿不弹"未保存"提示：关闭标签即丢弃该草稿；"另存为"后草稿自动转为正式文件；清空内容后草稿自动移除。

---

## 实现要点（给想改代码的同学）

- **编辑器内核**：使用 Scintilla 5.4.3 + Lexilla 5.3.3（Notepad++ 同款内核），静态链接。Scintilla 用样式 ID（0-255）给字符着色，不碰选区、不滚动、不触发重绘风暴，大文件性能优秀。
- **标签栏**：自绘窗口类（`NPPTabBar`），通过 `WM_PAINT` 绘制标签 + 关闭按钮。
- **行号**：Scintilla 内建 `SC_MARGIN_NUMBER`，不需要自绘 Gutter。
- **语法着色**：`editor.c` 的 `ApplyLexer` 用 `CreateLexer("cpp"/"python"/...)` 创建词法器，`SCI_SETILEXER` 设给 Scintilla，再按主题设各样式 ID 的颜色。
- **编码与行尾**：`fileio.c` 负责 BOM 探测、各编码与 UTF-8 的转换（Scintilla 内部用 UTF-8）、以及 CRLF/LF/CR 规范化与转换。
- **大文件流式 IO**：`fileio.c` 的 `StreamLoadToDoc` 分块读取并按"换行/字符边界"安全切分（不拆 UTF-8 多字节字符、UTF-16 代理对、DBCS 双字节），逐块转码 `SCI_APPENDTEXT` 进 Scintilla；`StreamSaveFromDoc` 用 `SCI_GETTEXTRANGEFULL` 分块取出转换后写同目录临时文件，`MoveFileEx` 原子替换（取消/失败不破坏原文件）。注意 `char*` 字节比较须转 `unsigned char`（有符号 `char` 下 `>= 0xC0` 永远为假）。
- **JSON 工具**：`jsonfmt.c` 用单遍递归下降解析器边校验（RFC 8259 严格语法）边输出——格式化按嵌套深度缩进、压缩则剔除全部空白；字符串/数字按原文透传（保留 `\uXXXX` 等转义写法）。替换通过 Scintilla 的 target + `SCI_REPLACETARGET` 完成，单步可撤销。
- **配色主题**：`editor.c` 的 `Editor_ApplyThemeColors` 统一设置编辑区与高亮颜色，亮/暗两套。
- **界面多语言**：所有用户可见文字收进 `i18n.c` 的字符串表，经 `T(STR_xxx)` 取词；主菜单由 `I18n_BuildMainMenu` 运行时构建（不再用 .rc 菜单资源），对话框沿用 .rc 模板、`WM_INITDIALOG` 时用 `I18n_ApplyDialog` 覆盖文字；切换语言重建菜单并刷新状态栏/未命名标题，选择写入 `session.ini [settings] uilang`。新增语言 = 在 `kStr` 加一列译文 + 在 `I18n_BuildMainMenu` 的界面语言子菜单加一项。

---

## 已知限制 / 后续可扩展

- 查找/替换为单文件（无跨文件/文件夹搜索）；正则语法为 Scintilla 内建（类 POSIX）。
- 未实现：列块选择、宏、插件体系、打印。Scintilla 原生支持折叠（已启用）、打印（SCI_FORMATRANGE，可按需接入）。

---

## 许可证

本项目为示例代码，可自由学习、修改、再分发。Scintilla 与 Lexilla 遵循其各自的 License.txt（HPND 许可证）。
