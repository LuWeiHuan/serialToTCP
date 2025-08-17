# 编译器设置
CC = gcc
CFLAGS = -Wall -Wextra -O2 -MMD -MP
LDFLAGS = -lws2_32 -lsetupapi -luuid
TARGET = com2tcp_server

# 目录设置
BUILD_DIR = build
BIN_DIR = bin
SRC_DIR = ./src
INSTALL_DIR = ./  # 自定义安装目录
EXE = $(BIN_DIR)/$(TARGET).exe

# =================================================
# 是否复制可执行文件到顶层目录 (取消注释启用)
COPY_TO_ROOT = 1
# =================================================

# 源文件和头文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

# PowerShell 命令定义
PS_FIND_PROC = powershell -Command "if (Get-Process '$(TARGET)' -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }"
PS_KILL_PROC = powershell -Command "Stop-Process -Name '$(TARGET)' -Force -ErrorAction SilentlyContinue"
PS_SLEEP = powershell -Command "Start-Sleep -Milliseconds 500"
PS_MKDIR = powershell -Command "if (-not (Test-Path '$(1)')) { New-Item -ItemType Directory -Path '$(1)' -Force | Out-Null }"
PS_COPY = powershell -Command "if (Test-Path '$(1)') { Copy-Item -Path '$(1)' -Destination '$(2)' -Force }"
PS_REMOVE_FILE = powershell -Command "if (Test-Path '$(1)') { Remove-Item -Force '$(1)' }"
PS_MAKE_SHORTCUT = powershell -Command "$$WshShell = New-Object -ComObject WScript.Shell; $$Shortcut = $$WshShell.CreateShortcut('$(1)'); $$Shortcut.TargetPath = '$(2)'; $$Shortcut.Save()"

.PHONY: all clean kill check install uninstall shortcut copy-to-root

all: check $(EXE) copy-to-root

$(EXE): $(OBJS)
	@$(call PS_MKDIR,$(BIN_DIR))
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译规则（包含头文件依赖）
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@$(call PS_MKDIR,$(BUILD_DIR))
	$(CC) $(CFLAGS) -c $< -o $@

# 包含自动生成的依赖关系
-include $(DEPS)

# 检查并终止正在运行的进程
check:
	@$(call PS_MKDIR,$(BIN_DIR))
	@$(PS_FIND_PROC) || ($(PS_KILL_PROC) && echo Terminated running $(TARGET) process && $(PS_SLEEP))

# 复制可执行文件到顶层目录（可选）- 添加文件存在检查
copy-to-root: $(EXE)
ifdef COPY_TO_ROOT
	@$(call PS_COPY,$(EXE),./$(TARGET).exe)
	@if exist ./$(TARGET).exe ( \
		echo Copied executable to top-level directory \
	) else ( \
		echo Failed to copy executable. File not found: $(EXE) \
	)
endif

# 强制终止进程
kill:
	@$(PS_KILL_PROC)

# 清理所有生成的文件（包括顶层目录的复制）
clean:
# 清理构建目录
	@if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR) 2>NUL || echo.
	
# 清理二进制目录
	@if exist $(BIN_DIR) rmdir /S /Q $(BIN_DIR) 2>NUL || echo.
	
# 清理顶层目录的复制文件
	@$(call PS_REMOVE_FILE,./$(TARGET).exe)
	
	@echo Clean completed (including top-level copy)

# 安装到指定目录
install: $(EXE)
	@echo Installing to $(INSTALL_DIR)...
	@$(call PS_MKDIR,$(INSTALL_DIR))
	@$(call PS_COPY,$(EXE),$(INSTALL_DIR))
	@echo Installed successfully to $(INSTALL_DIR)

# 创建桌面快捷方式
shortcut:
	@$(call PS_MAKE_SHORTCUT,"%USERPROFILE%\Desktop\$(TARGET).lnk","$(INSTALL_DIR)\$(TARGET).exe")
	@echo Shortcut created on Desktop

# 卸载（删除安装目录）
uninstall:
	@powershell -Command "if (Test-Path '$(INSTALL_DIR)') { Remove-Item -Recurse -Force '$(INSTALL_DIR)' }"
	@echo Uninstalled from $(INSTALL_DIR)