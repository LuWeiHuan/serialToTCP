# 获取执行时间
GET_NOW_TIME = $(shell powershell -Command "Get-Date -UFormat '%s'")
START_TIMER := $(GET_NOW_TIME)

# 基本设置
CC = gcc
CFLAGS = -Wall -Wextra -MMD -MP -O3
LDFLAGS = -static -lws2_32 -lsetupapi -luuid -lwinpthread
TARGET = com2tcp_server

# 是否复制可执行文件到根目录
COPY_TO_ROOT = 1

# 监控功能开关
ENABLE_MONITOR ?= 1

# 根据条件设置调试选项
ifeq ($(ENABLE_MONITOR),1) 	# 调试模式：启用异常捕获和堆栈跟踪
  CFLAGS += -g
  DEBUG_HLEP = -ldbghelp -lpsapi -Wl,-Map=$(TARGET).map
else												# 发布模式：不启用异常监控
  CFLAGS += -DCLOSE_EXCEPTION_MONITOR
  DEBUG_HLEP =  
endif


# 目录设置
BUILD_DIR = build
BIN_DIR = bin
SRC_DIR = ./src
INC_DIR = -I./inc -I./inc/uthash/src
INSTALL_DIR = ./
EXE_DIR = $(BIN_DIR)/$(TARGET).exe


# 多核编译设置
CORES := $(shell powershell -Command "Write-Output $$env:NUMBER_OF_PROCESSORS")
ifneq ($(CORES),)
  JOBS := -j$(CORES)
else
  JOBS := -j4
endif

# 单核编译设置
SINGLE_JOB := -j1


# 源文件和目标文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)
CFLAGS += $(INC_DIR)

# PowerShell 命令定义
PS_FIND_PROC = powershell -Command "if (Get-Process '$(TARGET)' -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }"
PS_KILL_PROC = powershell -Command "Stop-Process -Name '$(TARGET)' -Force -ErrorAction SilentlyContinue"
PS_SLEEP = powershell -Command "Start-Sleep -Milliseconds 500"
PS_MKDIR = powershell -Command "if (-not (Test-Path '$(1)')) { New-Item -ItemType Directory -Path '$(1)' -Force | Out-Null }"
PS_COPY = powershell -Command "if (Test-Path '$(1)') { Copy-Item -Path '$(1)' -Destination '$(2)' -Force }"
PS_REMOVE_FILE = powershell -Command "if (Test-Path '$(1)') { Remove-Item -Force '$(1)' }"
PS_MAKE_SHORTCUT = powershell -Command "$$WshShell = New-Object -ComObject WScript.Shell; $$Shortcut = $$WshShell.CreateShortcut('$(1)'); $$Shortcut.TargetPath = '$(2)'; $$Shortcut.Save()"
PS_CLEAR_SCREEN = powershell -Command "Clear-Host"

.PHONY: all clean kill check install uninstall shortcut copy-to-root build-parallel release debug order

# 默认目标 - 使用多核编译
all: clear-screen check
	@echo Building with $(CORES) parallel jobs...
	@$(MAKE) $(JOBS) build-timer

# 顺序编译目标 - 使用单核编译
order: clear-screen check
	@echo Building with single job (sequential)...
	@$(MAKE) $(SINGLE_JOB) build-timer

# 发布版本（多核）
release:
	@$(MAKE) ENABLE_MONITOR=0

# 调试版本（多核）
debug:
	@$(MAKE) ENABLE_MONITOR=1

# 发布版本（单核）
release-order:
	@$(MAKE) ENABLE_MONITOR=0 order

# 调试版本（单核）
debug-order:
	@$(MAKE) ENABLE_MONITOR=1 order

build-timer: $(EXE_DIR) copy-to-root 
	@$(eval END_TIMER := $(GET_NOW_TIME))
	@$(eval COMPLETE_TIMER := $(shell powershell -Command $(END_TIMER) - $(START_TIMER)))
#	@echo Build start at: $(START_TIMER)
#	@echo Build end   at: $(END_TIMER)
#	@echo Build complete at: $(COMPLETE_TIMER)
	@echo Build duration at: $(shell powershell -Command \"{0:F2}\" -f $(COMPLETE_TIMER)) seconds

# 并行构建（显式多核）
build-parallel: clear-screen check
	@echo Building with $(CORES) parallel jobs...
	@$(MAKE) $(JOBS) build-timer

# 最终目标
$(EXE_DIR): $(OBJS) 
	@$(call PS_MKDIR,$(BIN_DIR))
	@echo Linking $@...
ifeq ($(V),1)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(DEBUG_HLEP)
else
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(DEBUG_HLEP)
endif

# 编译规则
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@$(call PS_MKDIR,$(BUILD_DIR))
ifeq ($(V),1)
	$(CC) $(CFLAGS) -c $< -o $@
else
	@echo Compiling $<...
	@$(CC) $(CFLAGS) -c $< -o $@
endif

# 包含依赖文件
-include $(DEPS)

# 清屏
clear-screen:
	@$(PS_CLEAR_SCREEN)
	@echo Detected $(CORES) CPU cores
	$(eval BUILD_START := $(shell powershell -Command "Get-Date -UFormat '%s'"))

# 检查并终止运行实例
check:
	@$(call PS_MKDIR,$(BIN_DIR))
	@$(PS_FIND_PROC) || ($(PS_KILL_PROC) && echo Terminated running $(TARGET) process && $(PS_SLEEP))

# 复制到根目录
copy-to-root: $(EXE_DIR)
ifdef COPY_TO_ROOT
	@$(call PS_COPY,$(EXE_DIR),./$(TARGET).exe)
	@if exist ./$(TARGET).exe ( \
		echo Copied executable to top-level directory \
	) else ( \
		echo Failed to copy executable. File not found: $(EXE_DIR) \
	)
endif

# 其他目标...
kill:
	@$(PS_KILL_PROC)

clean:
	@echo Cleaning build artifacts...
	@if exist $(BUILD_DIR) rmdir /S /Q $(BUILD_DIR) 2>NUL || echo.
	@if exist $(BIN_DIR) rmdir /S /Q $(BIN_DIR) 2>NUL || echo.
	@$(call PS_REMOVE_FILE,./$(TARGET).exe)
	@$(call PS_REMOVE_FILE,./$(TARGET).map)
	@echo Clean completed

install: $(EXE_DIR)
	@echo Installing to $(INSTALL_DIR)...
	@$(call PS_MKDIR,$(INSTALL_DIR))
	@$(call PS_COPY,$(EXE_DIR),$(INSTALL_DIR))
	@echo Installed successfully to $(INSTALL_DIR)

shortcut:
	@$(call PS_MAKE_SHORTCUT,"%USERPROFILE%\Desktop\$(TARGET).lnk","$(INSTALL_DIR)\$(TARGET).exe")
	@echo Shortcut created on Desktop

uninstall:
	@powershell -Command "if (Test-Path '$(INSTALL_DIR)') { Remove-Item -Recurse -Force '$(INSTALL_DIR)' }"
	@echo Uninstalled from $(INSTALL_DIR)