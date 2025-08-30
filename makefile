# 编译器设置
CC = gcc
CFLAGS = -Wall -Wextra -O2 -MMD -MP
LDFLAGS = -lws2_32 -lsetupapi -luuid
TARGET = com2tcp_server

# 目录设置
BUILD_DIR = build
BIN_DIR = bin
SRC_DIR = ./src
INC_DIR = ./inc  # 自定义头文件目录
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

# 添加头文件搜索路径
CFLAGS += -I$(INC_DIR)

# PowerShell 命令定义
PS_FIND_PROC = powershell -Command "if (Get-Process '$(TARGET)' -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }"
PS_KILL_PROC = powershell -Command "Stop-Process -Name '$(TARGET)' -Force -ErrorAction SilentlyContinue"
PS_SLEEP = powershell -Command "Start-Sleep -Milliseconds 500"
PS_MKDIR = powershell -Command "if (-not (Test-Path '$(1)')) { New-Item -ItemType Directory -Path '$(1)' -Force | Out-Null }"
PS_COPY = powershell -Command "if (Test-Path '$(1)') { Copy-Item -Path '$(1)' -Destination '$(2)' -Force }"
PS_REMOVE_FILE = powershell -Command "if (Test-Path '$(1)') { Remove-Item -Force '$(1)' }"
PS_MAKE_SHORTCUT = powershell -Command "$$WshShell = New-Object -ComObject WScript.Shell; $$Shortcut = $$WshShell.CreateShortcut('$(1)'); $$Shortcut.TargetPath = '$(2)'; $$Shortcut.Save()"
PS_CLEAR_SCREEN = powershell -Command "Clear-Host"
PS_GET_TIME = powershell -Command "Get-Date -Format 'HH:mm:ss.fff'"
PS_MEASURE_TIME = powershell -Command "$$Start=Get-Date; $$End=Get-Date; $$Duration=$$End-$$Start; Write-Host 'Build time: ' -NoNewline; if ($$Duration.TotalSeconds -ge 60) { Write-Host (\"{0:F0}m {1:F2}s\" -f [math]::Floor($$Duration.TotalMinutes), ($$Duration.TotalSeconds % 60)) } elseif ($$Duration.TotalSeconds -ge 1) { Write-Host (\"{0:F2}s\" -f $$Duration.TotalSeconds) } else { Write-Host (\"{0:F0}ms\" -f $$Duration.TotalMilliseconds) }"

.PHONY: all clean kill check install uninstall shortcut copy-to-root

all: clear-screen check build-timer  # 添加构建计时器

build-timer: $(EXE) copy-to-root
	@echo Build completed at: $$($(PS_GET_TIME))
	@$(PS_MEASURE_TIME)

$(EXE): $(OBJS) 
	@$(call PS_MKDIR,$(BIN_DIR))
	@echo Linking $@...
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译规则（包含头文件依赖）
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@$(call PS_MKDIR,$(BUILD_DIR))
#	@echo Compiling $<...
	$(CC) $(CFLAGS) -c $< -o $@

# 包含自动生成的依赖关系
-include $(DEPS)

# 清屏目标
clear-screen:
	@$(PS_CLEAR_SCREEN)
	@echo Build started at: $$($(PS_GET_TIME))

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
	@echo Cleaning build artifacts...
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