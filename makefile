CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lws2_32 -lsetupapi -luuid
TARGET = com2tcp_server
EXE = $(TARGET).exe

# PowerShell 命令定义
PS_FIND_PROC = powershell -Command "if (Get-Process '$(TARGET)' -ErrorAction SilentlyContinue) { exit 1 } else { exit 0 }"
PS_KILL_PROC = powershell -Command "Stop-Process -Name '$(TARGET)' -Force -ErrorAction SilentlyContinue"
PS_SLEEP = powershell -Command "Start-Sleep -Milliseconds 500"

.PHONY: all clean check kill

all: check $(EXE)

$(EXE): src/server.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 检查并终止正在运行的进程
check:
	@$(PS_FIND_PROC) || ($(PS_KILL_PROC) && echo Terminated running $(TARGET) process && $(PS_SLEEP))

# 强制终止进程
kill:
	@$(PS_KILL_PROC)

clean:
	@if exist $(EXE) del $(EXE)
	@echo Clean completed