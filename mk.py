#!/usr/bin/env python3
# mk.py - 跨平台构建脚本

import os
import sys
import subprocess
import platform
import shutil
import time
from pathlib import Path
from datetime import datetime

class Colors:
  """颜色支持（Windows和Unix）"""
  RED = '\033[91m'
  GREEN = '\033[92m'
  YELLOW = '\033[93m'
  BLUE = '\033[94m'
  PURPLE = '\033[95m'
  CYAN = '\033[96m'
  NC = '\033[0m'
  
  @staticmethod
  def disable():
    if platform.system() == 'Windows':
      Colors.RED = Colors.GREEN = Colors.YELLOW = ''
      Colors.BLUE = Colors.PURPLE = Colors.CYAN = Colors.NC = ''

# 检测是否支持颜色
if platform.system() == 'Windows':
  os.system('color')

class BuildManager:
  def __init__(self):
    self.target_name = "com2tcp_server"
    self.extra_script = "fileCopy.bat" if platform.system() == "Windows" else "fileCopy.sh"
    self.start_time = None  # 将在构建开始时设置
    
    # 平台特定配置
    self.is_windows = platform.system() == "Windows"
    self.exe_ext = ".exe" if self.is_windows else ""
    
    # ARM默认架构（与原脚本一致）
    self.default_arm_target_arch = "64"  # 可选: "32" 或 "64"
    
    # ARM编译器配置
    self.arm32_compiler = "/usr/local/arm/arm-linux-gnueabihf_4.9.4/bin/arm-linux-gnueabihf-gcc"
    self.arm64_compiler = ""
    
    # 构建目录（稍后根据平台设置）
    self.build_dir = None
    self.cmake_generator = "MinGW Makefiles" if self.is_windows else "Unix Makefiles"
    
    # 默认构建参数
    self.build_flags = {
      "development": {"CMAKE_BUILD_TYPE": "Debug", "ENABLE_MONITOR": "ON", "ENABLE_ASAN": "OFF", "BUILD_STATIC": "ON"},
      "debug": {"CMAKE_BUILD_TYPE": "Debug", "ENABLE_MONITOR": "OFF", "ENABLE_ASAN": "ON", "BUILD_STATIC": "ON"},
      "release": {"CMAKE_BUILD_TYPE": "Release", "ENABLE_MONITOR": "OFF", "ENABLE_ASAN": "OFF", "BUILD_STATIC": "ON"}
    }
    
    # 当前构建配置
    self.current_arch = None  # None表示本地架构，'arm32', 'arm64'
    self.build_type = "development"
    
    # 初始化构建目录
    self._init_build_dir()
    
    # 获取CPU核心数用于并行构建
    self.cpu_cores = self._get_cpu_cores()
  
  def _get_cpu_cores(self):
    """获取CPU核心数"""
    try:
      if self.is_windows:
        # Windows: 使用NUMBER_OF_PROCESSORS环境变量
        cores = os.cpu_count()
        if cores is None:
          # 备用方法
          result = subprocess.run(['wmic', 'cpu', 'get', 'NumberOfCores'], 
                                 capture_output=True, text=True)
          if result.returncode == 0:
            lines = result.stdout.strip().split('\n')
            cores = int(lines[1].strip()) if len(lines) > 1 else 4
          else:
            cores = 4
        return cores
      else:
        # Linux: 使用nproc命令
        result = subprocess.run(['nproc'], capture_output=True, text=True)
        if result.returncode == 0:
          return int(result.stdout.strip())
        else:
          return os.cpu_count() or 4
    except:
      return 4
  
  def _get_linux_arch(self):
    """获取Linux系统架构"""
    arch = platform.machine()
    if arch in ["aarch64", "arm64"]:
      return "ARM64"
    elif arch in ["armv7l", "armv6l"]:
      return "ARM32"
    elif arch == "x86_64":
      return "X64"
    else:
      return "X86"
  
  def _init_build_dir(self):
    """初始化构建目录（根据当前架构设置）"""
    if self.is_windows:
      self.build_dir = Path("build/WinX64")
    else:
      if self.current_arch == "arm32":
        self.build_dir = Path("build/LinuxARM32")
      elif self.current_arch == "arm64":
        self.build_dir = Path("build/LinuxARM64")
      else:
        system_arch = self._get_linux_arch()
        self.build_dir = Path(f"build/Linux{system_arch}")
  
  def _find_arm_compiler(self, arch):
    """查找ARM编译器"""
    if arch == "arm32":
      # 优先使用配置的编译器
      if self.arm32_compiler and Path(self.arm32_compiler).exists():
        return self.arm32_compiler
      # 尝试系统路径
      try:
        result = subprocess.run(['which', 'arm-linux-gnueabihf-gcc'], 
                               capture_output=True, text=True)
        if result.returncode == 0:
          return result.stdout.strip()
      except:
        pass
    elif arch == "arm64":
      # 优先使用配置的编译器
      if self.arm64_compiler and Path(self.arm64_compiler).exists():
        return self.arm64_compiler
      # 尝试系统路径
      try:
        result = subprocess.run(['which', 'aarch64-linux-gnu-gcc'], 
                               capture_output=True, text=True)
        if result.returncode == 0:
          return result.stdout.strip()
      except:
        pass
    return None
  
  def _setup_arm_build(self, arch):
    """设置ARM交叉编译"""
    compiler = self._find_arm_compiler(arch)
    if not compiler:
      print(f"{Colors.RED}错误: 未找到 {arch} 交叉编译器{Colors.NC}")
      if arch == "arm32":
        print(f"{Colors.YELLOW}请安装: sudo apt-get install gcc-arm-linux-gnueabihf{Colors.NC}")
      else:
        print(f"{Colors.YELLOW}请安装: sudo apt-get install gcc-aarch64-linux-gnu{Colors.NC}")
      return False
    
    os.environ['CMAKE_C_COMPILER'] = compiler
    arch_name = "ARM32" if arch == "arm32" else "ARM64"
    print(f"{Colors.CYAN}ARM交叉编译模式: {arch_name}{Colors.NC}")
    print(f"{Colors.CYAN}编译器: {compiler}{Colors.NC}")
    return True
  
  def _run_command(self, cmd, cwd=None, check=False, capture_output=False, silent=False):
    """执行命令并显示输出"""
    if not silent:
      print(f"{Colors.CYAN}执行: {cmd}{Colors.NC}")
    
    if capture_output:
      result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
    else:
      result = subprocess.run(cmd, shell=True, cwd=cwd)
    
    if check and result.returncode != 0:
      return False
    return result
  
  def _format_duration(self, seconds):
    """格式化时间"""
    if seconds >= 3600:
      hours = int(seconds // 3600)
      minutes = int((seconds % 3600) // 60)
      secs = int(seconds % 60)
      return f"{hours}小时{minutes}分钟{secs}秒"
    elif seconds >= 60:
      minutes = int(seconds // 60)
      secs = int(seconds % 60)
      return f"{minutes}分钟{secs}秒"
    else:
      return f"{seconds:.2f}秒"
  
  def kill_process(self):
    """终止正在运行的进程（仅Windows）"""
    if not self.is_windows:
      return
    
    print(f"\n{Colors.BLUE}[INFO] 检查运行中的 {self.target_name} 进程...{Colors.NC}")
    cmd = f'tasklist /FI "IMAGENAME eq {self.target_name}.exe" 2>NUL'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    
    if self.target_name in result.stdout:
      print(f"{Colors.YELLOW}[INFO] 终止 {self.target_name} 进程...{Colors.NC}")
      subprocess.run(f'taskkill /F /IM "{self.target_name}.exe" >NUL 2>&1', shell=True)
      time.sleep(1)
      print(f"{Colors.GREEN}[SUCCESS] 进程已终止{Colors.NC}")
    else:
      print(f"{Colors.GREEN}[INFO] 未找到运行中的进程{Colors.NC}")
  
  def clean(self, full_clean=False):
    """清理构建目录
    
    Args:
      full_clean: True=完全删除构建目录并重新创建，False=仅make clean
    """
    print(f"\n{Colors.BLUE}[INFO] 清理构建目录...{Colors.NC}")
    
    if not self.build_dir or not self.build_dir.exists():
      print(f"{Colors.YELLOW}[WARNING] 构建目录不存在{Colors.NC}")
      return
    
    if full_clean:
      # 完全清理：直接删除整个构建目录
      print(f"{Colors.CYAN}[INFO] 执行完全清理（删除整个构建目录）...{Colors.NC}")
      shutil.rmtree(self.build_dir)
      print(f"{Colors.GREEN}[SUCCESS] 完全清理完成{Colors.NC}")
    else:
      # 普通清理：仅make clean
      if self.is_windows:
        original_dir = os.getcwd()
        os.chdir(self.build_dir)
        self._run_command('make clean', silent=True)
        os.chdir(original_dir)
      else:
        self._run_command('make clean', cwd=self.build_dir, silent=True)
      print(f"{Colors.GREEN}[SUCCESS] 清理完成{Colors.NC}")
  
  def remove(self):
    """删除构建目录"""
    print(f"\n{Colors.BLUE}[INFO] 删除构建目录...{Colors.NC}")
    if self.build_dir and self.build_dir.exists():
      shutil.rmtree(self.build_dir)
      print(f"{Colors.GREEN}[SUCCESS] 删除完成{Colors.NC}")
    else:
      print(f"{Colors.YELLOW}[WARNING] 构建目录不存在{Colors.NC}")
  
  def build(self, force_reconfigure=False):
    """执行构建
    
    Args:
      force_reconfigure: True=强制重新CMake配置并重新编译
    """
    # 记录开始时间
    self.start_time = time.time()
    
    # 重新初始化构建目录（根据当前架构）
    old_build_dir = self.build_dir
    self._init_build_dir()
    
    # 检查构建目录是否改变（架构切换）
    if old_build_dir != self.build_dir and old_build_dir and old_build_dir.exists():
      print(f"{Colors.YELLOW}[INFO] 架构已改变，清理旧的构建目录...{Colors.NC}")
      print(f"{Colors.YELLOW}[INFO] 旧目录: {old_build_dir}{Colors.NC}")
      print(f"{Colors.YELLOW}[INFO] 新目录: {self.build_dir}{Colors.NC}")
      # 删除旧的构建目录
      shutil.rmtree(old_build_dir)
    
    # 如果是ARM架构，设置交叉编译环境
    if self.current_arch and not self.is_windows:
      if not self._setup_arm_build(self.current_arch):
        return False
    
    # 如果需要强制重新配置，删除构建目录
    if force_reconfigure and self.build_dir.exists():
      print(f"{Colors.CYAN}[INFO] 强制重新配置，删除现有构建目录...{Colors.NC}")
      shutil.rmtree(self.build_dir)
    
    # 终止进程
    self.kill_process()
    
    # 获取构建标志
    flags = self.build_flags.get(self.build_type, self.build_flags["development"])
    build_name = self.build_type.capitalize()
    
    # 构建CMake命令
    cmake_flags = f'-DCMAKE_BUILD_TYPE={flags["CMAKE_BUILD_TYPE"]} '
    cmake_flags += f'-DENABLE_MONITOR={flags["ENABLE_MONITOR"]} '
    cmake_flags += f'-DENABLE_ASAN={flags["ENABLE_ASAN"]} '
    
    if self.is_windows:
      cmake_flags += f'-DBUILD_STATIC={flags["BUILD_STATIC"]} '
    
    # 构建类型提示
    build_tips = {
      "development": "(信号处理+符号解析)",
      "debug": "(ASAN内存检测，性能较慢)",
      "release": "(最优性能，无调试信息)"
    }

    # 根据构建类型选择颜色
    build_type_colors = {
      "development": Colors.CYAN,
      "debug": Colors.YELLOW,      # DEBUG 使用黄色
      "release": Colors.GREEN
    }
    build_type_color = build_type_colors.get(self.build_type, Colors.CYAN)

    print(f"\n{Colors.CYAN}[INFO] 构建类型: {build_type_color}{build_name} {build_tips.get(self.build_type, '')}{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] 构建目录: {self.build_dir}{Colors.NC}")
    
    # 确保构建目录存在
    self.build_dir.mkdir(parents=True, exist_ok=True)
    
    # 配置CMake
    print(f"{Colors.CYAN}[INFO] 配置CMake...{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] CMake命令: cmake -B {self.build_dir} -G \"{self.cmake_generator}\" {cmake_flags}{Colors.NC}")
    
    cmake_config = f'cmake -B "{self.build_dir}" -G "{self.cmake_generator}" {cmake_flags}'
    result = self._run_command(cmake_config, check=True)
    if not result:
      print(f"{Colors.RED}[ERROR] CMake配置失败!{Colors.NC}")
      return False
    
    # 构建项目 - 使用并行构建加速
    print(f"{Colors.CYAN}[INFO] 构建项目...{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] 使用 {self.cpu_cores} 核心并行构建{Colors.NC}")
    
    # 构建命令：Windows和Linux都支持 --parallel
    build_cmd = f'cmake --build "{self.build_dir}"'
    if self.cpu_cores > 1:
      build_cmd += f' --parallel {self.cpu_cores}'
    
    result = self._run_command(build_cmd, check=True)
    if not result:
      print(f"{Colors.RED}[ERROR] 构建失败!{Colors.NC}")
      return False
    
    # 验证可执行文件
    executable = self.build_dir / f"{self.target_name}{self.exe_ext}"
    if not executable.exists():
      print(f"{Colors.RED}[ERROR] 可执行文件未生成!{Colors.NC}")
      return False
    
    # 显示文件信息
    file_size = executable.stat().st_size // 1024
    print(f"\n{Colors.GREEN}[SUCCESS] 构建成功!{Colors.NC}")
    print(f"\n{Colors.CYAN}[INFO] 构建类型: {build_type_color}{build_name} {build_tips.get(self.build_type, '')}{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] 程序位置: {executable}{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] 程序大小: {file_size} KB{Colors.NC}")
    
    # 显示文件架构信息（Linux）
    if not self.is_windows:
      try:
        file_info = subprocess.run(['file', str(executable)], capture_output=True, text=True)
        if file_info.returncode == 0:
          info_line = file_info.stdout.strip()
          if "ELF" in info_line:
            if "x86-64" in info_line:
              arch_info = "x86_64"
            elif "ARM aarch64" in info_line:
              arch_info = "ARM64"
            elif "ARM" in info_line:
              arch_info = "ARM32"
            else:
              arch_info = "Unknown"
            print(f"{Colors.CYAN}[INFO] 目标架构: {arch_info}{Colors.NC}")
      except:
        pass
    
    # 计算构建时间
    duration = time.time() - self.start_time
    start_time_str = datetime.fromtimestamp(self.start_time).strftime('%H:%M:%S')
    end_time_str = datetime.now().strftime('%H:%M:%S')
    print(f"{Colors.CYAN}[INFO] 构建用时: {self._format_duration(duration)}{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] 构建时间: {start_time_str} - {end_time_str}{Colors.NC}")
    print(f"{Colors.CYAN}[INFO] 当前时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}{Colors.NC}")
    
    # 执行额外脚本（仅在ARM架构下执行）
    extra_path = Path(self.extra_script)
    if extra_path.exists() and self.current_arch and not self.is_windows:
      print(f"\n{Colors.CYAN}[INFO] 构建ARM平台，执行 {self.extra_script}...{Colors.NC}")
      extra_path.chmod(0o755)
      subprocess.run(f'./{self.extra_script}', shell=True)
      print(f"{Colors.GREEN}[SUCCESS] 脚本执行完成{Colors.NC}")
    elif extra_path.exists() and self.is_windows and not self.current_arch:
      # Windows平台总是执行fileCopy.bat（与原bat脚本行为一致）
      print(f"\n{Colors.CYAN}[INFO] 执行 {self.extra_script}...{Colors.NC}")
      subprocess.run(self.extra_script, shell=True)
      print(f"{Colors.GREEN}[SUCCESS] 脚本执行完成{Colors.NC}")
    
    return True

def parse_arguments():
  """解析命令行参数（不区分大小写，支持任意顺序）"""
  args = sys.argv[1:]
  
  # 参数映射（不区分大小写）
  param_map = {
    'cleanbuild': 'cleanbuild',
    'cb': 'cleanbuild',
    'release': 'release',
    'debug': 'debug',
    'clean': 'clean',
    'rm': 'rm',
    'help': 'help',
    '-h': 'help',
    '/?': 'help',
    'arm': 'arm',
    'arm32': 'arm32',
    'arm64': 'arm64'
  }
  
  result = {
    'action': 'build',  # 默认动作
    'build_type': 'development',  # 默认构建类型
    'target_arch': None,  # 目标架构
    'do_cleanbuild': False,  # 是否cleanBuild（完全清理+构建）
    'do_clean': False,  # 是否仅清理
    'do_remove': False  # 是否删除
  }
  
  # 解析参数
  for arg in args:
    normalized = arg.lower()
    mapped = param_map.get(normalized, normalized)
    
    if mapped == 'cleanbuild':
      result['do_cleanbuild'] = True
      result['action'] = 'build'
    elif mapped == 'clean':
      result['action'] = 'clean'
      result['do_clean'] = True
    elif mapped == 'rm':
      result['action'] = 'remove'
      result['do_remove'] = True
    elif mapped == 'help':
      result['action'] = 'help'
    elif mapped == 'release':
      result['build_type'] = 'release'
      result['action'] = 'build'
    elif mapped == 'debug':
      result['build_type'] = 'debug'
      result['action'] = 'build'
    elif mapped in ['arm', 'arm32', 'arm64']:
      # 处理ARM参数
      if mapped == 'arm':
        result['target_arch'] = None  # 稍后使用默认值
      else:
        result['target_arch'] = mapped
    elif normalized == 'build':
      result['action'] = 'build'
  
  # 如果指定了arm但没有指定具体架构，使用默认值
  if result['target_arch'] is None and any(a.lower() in ['arm'] for a in args):
    # 需要获取默认架构（从BuildManager）
    temp_manager = BuildManager()
    result['target_arch'] = f"arm{temp_manager.default_arm_target_arch}"
  
  return result

def show_help():
  """显示帮助信息"""
  print("""
╔═════════════════════════════════════════════════════════════════╗
║                    跨平台构建脚本使用方法                       ║
╠═════════════════════════════════════════════════════════════════╣
║  快速构建命令:                                                  ║
║    python3 mk.py              - 开发版本 (信号处理+符号解析)    ║
║    python3 mk.py debug        - 调试版本 (ASAN内存检测)         ║
║    python3 mk.py release      - 发布版本 (最优性能)             ║
║                                                                 ║
║  组合命令 (参数不区分先后顺序):                                 ║
║    python3 mk.py cleanBuild debug arm    - 完全清理并构建ARM调试║
║    python3 mk.py cb debug arm            - 完全清理并构建ARM调试║
║    python3 mk.py arm release cleanBuild  - 完全清理并构建ARM发布║
║    python3 mk.py arm32 debug             - 构建ARM32调试版本    ║
║    python3 mk.py debug arm64             - 构建ARM64调试版本    ║
║                                                                 ║
║  其他命令:                                                      ║
║    clean          - 清理构建目录（仅make clean）                ║
║    rm             - 删除整个构建目录                            ║
║    cleanBuild     - 完全清理并重新构建 
║    cb             - 完全清理并重新构建                         ║
║    help           - 显示此帮助信息                              ║
║                                                                 ║
║  ARM交叉编译说明:                                               ║
║    arm      - 使用默认架构 (当前: 64位)                         ║
║    arm32    - 构建ARM32位版本                                   ║
║    arm64    - 构建ARM64位版本                                   ║
║                                                                 ║
║  注意: 所有参数不区分大小写，支持任意顺序                       ║
║        cleanBuild会删除整个构建目录，强制完全重新编译           ║
╚═════════════════════════════════════════════════════════════════╝
  """)

def main():
  # 解析参数
  config = parse_arguments()
  
  # 处理帮助
  if config['action'] == 'help':
    show_help()
    return
  
  # 创建构建管理器
  manager = BuildManager()
  
  # 设置构建类型
  manager.build_type = config['build_type']
  
  # 设置目标架构
  if config['target_arch']:
    manager.current_arch = config['target_arch']
    print(f"{Colors.CYAN}目标平台: {config['target_arch'].upper()}{Colors.NC}")
  else:
    print(f"{Colors.CYAN}目标平台: 本地架构{Colors.NC}")
  
  # 执行动作
  if config['action'] == 'clean':
    # 普通清理：仅make clean
    manager.clean(full_clean=False)
  elif config['action'] == 'remove':
    manager.remove()
  elif config['action'] == 'build':
    # 如果是cleanBuild，删除整个构建目录再构建
    if config['do_cleanbuild']:
      # 先删除构建目录（如果存在）
      if manager.build_dir and manager.build_dir.exists():
        print(f"\n{Colors.BLUE}[INFO] cleanBuild: 删除构建目录 {manager.build_dir}{Colors.NC}")
        shutil.rmtree(manager.build_dir)
        print(f"{Colors.GREEN}[SUCCESS] 构建目录已删除{Colors.NC}")
      # 然后构建（force_reconfigure会自动创建新目录）
      manager.build(force_reconfigure=True)
    else:
      manager.build(force_reconfigure=False)
  else:
    print(f"{Colors.RED}[ERROR] 未知操作: {config['action']}{Colors.NC}")
    sys.exit(1)

if __name__ == '__main__':
  try:
    main()
  except KeyboardInterrupt:
    print(f"\n{Colors.YELLOW}构建被用户中断{Colors.NC}")
    sys.exit(1)
  except Exception as e:
    print(f"{Colors.RED}发生错误: {e}{Colors.NC}")
    import traceback
    traceback.print_exc()
    sys.exit(1)