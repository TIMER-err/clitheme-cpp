# clitheme-cpp

CLItheme 的 C++ 实现，提供主题定义文件 (`.ctdef.txt`) 的解析/生成器和 exec 模式输出替换。

## 依赖

- C++17 编译器 (GCC 7+ / Clang 5+)
- CMake 3.14+
- SQLite3
- zlib
- PCRE2

### Arch Linux

```bash
sudo pacman -S cmake sqlite zlib pcre2
```

### Debian/Ubuntu

```bash
sudo apt install cmake libsqlite3-dev zlib1g-dev libpcre2-dev
```

## 构建

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

构建产物为 `build/clitheme-cpp`。

## 安装

```bash
cmake --install build
```

二进制文件将安装到 `~/.local/share/clitheme/`。

## 使用方式

### 1. generate 模式

解析 `.ctdef.txt` 主题定义文件，生成文件层级结构和 SQLite 数据库。

```bash
clitheme-cpp generate <file> [options]
```

**选项：**

| 选项 | 说明 |
|---|---|
| `--output-path <path>` | 输出目录（默认自动生成临时目录） |
| `--overlay` | 叠加模式 |
| `--infofile-name <name>` | theme-info 子目录名（默认 `"1"`） |

**示例：**

```bash
# 生成到指定目录
clitheme-cpp generate mytheme.ctdef.txt --output-path ./output

# 生成到临时目录（路径输出到 stdout）
clitheme-cpp generate mytheme.ctdef.txt
```

**输出结构：**

```
output/
├── theme-info/
│   ├── current_theme_index
│   └── 1/
│       ├── file_content
│       ├── clithemeinfo_name
│       ├── clithemeinfo_version
│       ├── clithemeinfo_description
│       ├── clithemeinfo_filepath
│       ├── clithemeinfo_locales_v2
│       └── clithemeinfo_supported_apps_v2
├── theme-data/
│   └── <domain>/<app>/<entry_name>
├── manpages/
│   └── <manpage_files>.gz
└── subst-data.db          # 仅当定义文件含 {substrules} 时
```

### 2. exec 模式

通过 PTY 执行子进程，捕获 stdout 和 stderr，根据数据库中的替换规则进行实时匹配和替换。

```bash
clitheme-cpp exec [options] <command> [args...]
```

**选项：**

| 选项 | 说明 |
|---|---|
| `--db-path <path>` | 数据库路径（默认 `~/.local/share/clitheme/subst-data.db`） |

**示例：**

```bash
# 在 exec 模式下运行命令
clitheme-cpp exec --db-path ./output/subst-data.db ls -la

# 运行交互式 shell
clitheme-cpp exec --db-path ./output/subst-data.db fish

# 使用默认数据库路径
clitheme-cpp exec some_command --arg1 --arg2
```

**特性：**

- 通过 PTY 同时捕获 stdout 和 stderr
- 支持交互式程序（终端 raw 模式）
- 正确转发信号（Ctrl+C、Ctrl+Z、窗口大小调整）
- 保留子进程退出码

### 为Fish Shell配置

将以下内容添加到 `~/.config/fish/config.fish` 的末尾：

```fish
set -g commands_to_ignore \
    cd pushd popd exit \
    vim nvim nano emacs \
    less more man cat \
    top htop btop \
    fg bg jobs disown \
    clear \
    sudo su ssh fish

function transform_output_on_enter
    set -l original_command (commandline)

    if test -z "$original_command"
        commandline -f execute
        return
    end

    set -l first_word (string split ' ' -- $original_command)[1]

    if contains -- $first_word $commands_to_ignore
        commandline -f execute
    else
        set -l hist_file ~/.local/share/fish/fish_history
        set -l now (date +%s)
        echo "- cmd: $original_command" >> $hist_file
        echo "  when: $now" >> $hist_file
        history --merge

        echo ''
        eval ~/.local/share/clitheme/clitheme-cpp exec "$original_command"
        set -l go_back_prev_line 'printf "\e[1A"'
        commandline -r $go_back_prev_line
        commandline -f execute

        history delete --exact --case-sensitive -- "$go_back_prev_line"
    end
end

bind \r transform_output_on_enter
```
> [!IMPORTANT]
> 此配置默认数据库文件位于~/.local/share/clitheme/subst-data.db

如果你使用其他shell，需要自行编写配置

## 数据库 Schema

与 Python 版本完全兼容（版本 8）：

```sql
CREATE TABLE clitheme_subst_data (
    match_pattern TEXT NOT NULL,
    match_is_multiline INTEGER NOT NULL,
    substitute_pattern TEXT NOT NULL,
    is_regex INTEGER NOT NULL,
    effective_locale TEXT,
    effective_command TEXT,
    command_match_strictness INTEGER NOT NULL,
    command_is_regex INTEGER NOT NULL,
    foreground_only INTEGER NOT NULL,
    end_match_here INTEGER NOT NULL,
    stdout_stderr_only INTEGER NOT NULL,
    unique_id TEXT NOT NULL,
    file_id TEXT NOT NULL
);
```

## 项目结构

```
src/
├── main.cpp                     # 入口：dispatch generate/exec 子命令
├── globalvar.hpp/cpp             # 全局常量（路径名、DB 表名、版本号等）
├── string_utils.hpp              # 字符串工具函数
├── sanity_check.hpp/cpp          # 路径合法性检查
├── locale_detect.hpp/cpp         # 环境变量 locale 检测
├── options.hpp                   # 选项定义和辅助函数
├── data_handlers.hpp/cpp         # 文件操作（mkdir、写入 entry/infofile/manpage）
├── db_interface.hpp/cpp          # SQLite 数据库接口
├── generator_object.hpp/cpp      # 主解析器状态对象
├── entry_block.hpp/cpp           # [entry]/[subst_*] 块处理
├── section_header.hpp/cpp        # {header} section 处理
├── section_entries.hpp/cpp       # {entries} section 处理
├── section_substrules.hpp/cpp    # {substrules} section 处理
├── section_manpages.hpp/cpp      # {manpages} section 处理
├── exec_handler.hpp/cpp         # exec 模式：PTY fork/exec 和 I/O 转发
└── substrules_processor.hpp/cpp  # 替换规则匹配引擎
```

## 与 Python 版本的差异

| 方面 | Python 版本 | C++ 版本 |
|---|---|---|
| 错误消息 | 支持国际化（FetchDescriptor） | 硬编码英文 |
| UUID 生成 | `hashlib.shake_128` 确定性 | `std::random_device` 随机 UUID v4 |
| 正则引擎 | Python `re` | PCRE2 |
| 执行模式 | `clitheme-exec` | `clitheme-cpp exec` |

## License

GPLv3+
