#!/usr/bin/env python3
"""
通用第三方库抓取脚本（共享文件夹安全版 + 显式 tmp 清理）。
- 在本地 /tmp 下创建独立工作目录解压 deb/tar
- 解压出的整棵树（不再按 KEEP_PREFIXES 过滤）全部复制到 third_party/<arch>/
- 符号链接解引用为实体文件（绕开 hgfs 不支持 symlink 的限制）
- 每个归档处理完在 finally 中主动 rmtree 清理 tmp，不依赖 TemporaryDirectory
- 单个归档失败不中断，最后汇总
- 参数中任意位置出现 clean 时，先删除对应架构目录，然后继续下载（可跟架构名限定，不跟则清理全部后再全部下载）
- 架构 triplet 不再从清单读取，改为解压后在 tmp 中探测 usr/lib/<triplet> 目录
- 清单格式：架构名 -> URL 列表（不再有 name/entries 字段）
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(SCRIPT_DIR, "third_party_manifest.json")
THIRD_PARTY = os.path.join(SCRIPT_DIR)

# tmp 工作根目录：优先用系统 TMPDIR，否则 /tmp
TMP_BASE = os.environ.get("TMPDIR", "/tmp")

# 形如 arm-linux-gnueabihf / aarch64-linux-gnu / x86_64-linux-gnu 的 triplet 目录名
TRIPLET_RE = re.compile(r"^[a-z0-9_]+-[a-z0-9_]+-[a-z0-9_]+$")


def log(msg: str) -> None:
    print(msg, flush=True)


def make_tmp_dir(tag: str) -> str:
    """显式创建唯一的 tmp 工作目录，不依赖 TemporaryDirectory 的自动清理。"""
    os.makedirs(TMP_BASE, exist_ok=True)
    path = tempfile.mkdtemp(prefix=f"fetch_third_party_{tag}_", dir=TMP_BASE)
    return path


def cleanup_tmp_dir(path: str) -> None:
    """主动删除 tmp 工作目录，失败重试一次（应对文件句柄未释放）。"""
    if not path or not os.path.isdir(path):
        return
    for attempt in range(2):
        try:
            shutil.rmtree(path)
            log(f"    已清理 tmp: {path}")
            return
        except Exception as e:
            if attempt == 0:
                time.sleep(0.3)
            else:
                log(f"    警告: 清理 tmp 失败 {path} -> {e}")


def download(url: str, dest: str) -> None:
    log(f"    下载: {url}")
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"}
    )
    with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)


def extract_deb(archive: str, tmp_root: str) -> None:
    """解压 deb 到本地 tmp，符号链接失败不视为致命（复制时会解引用）。"""
    subprocess.run(
        ["dpkg-deb", "--extract", archive, tmp_root],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def extract_tar(archive: str, tmp_root: str) -> None:
    with tarfile.open(archive) as t:
        t.extractall(tmp_root)


def _dir_has_libs(path: str) -> bool:
    """目录内是否含 .so / .so.* / .a 实体文件或指向这些文件的符号链接。"""
    try:
        for entry in os.scandir(path):
            n = entry.name.lower()
            if n.endswith((".so", ".a")) or ".so." in n:
                return True
    except OSError:
        return False
    return False


def detect_triplets(extract_root: str) -> set:
    """
    在 tmp 解压树的 usr/lib/ 下探测 triplet 目录。
    判定条件（满足任一）：
      1. 目录名形如 <arch>-<vendor>-<os>
      2. 目录内直接含 .so / .so.* / .a 文件
    只在 usr/lib 及 usr/lib64 下探测，避免误伤其它同形目录。
    """
    found = set()
    for lib_parent in ("usr/lib", "usr/lib64"):
        lib_dir = os.path.join(extract_root, lib_parent)
        if not os.path.isdir(lib_dir):
            continue
        try:
            children = list(os.scandir(lib_dir))
        except OSError:
            continue
        for child in children:
            if not child.is_dir(follow_symlinks=True):
                continue
            name = child.name
            if TRIPLET_RE.match(name) or _dir_has_libs(child.path):
                found.add(name)
    return found


def copy_tree_deref(src_root: str, dst_root: str) -> int:
    """
    把 src_root 下整棵解压树复制到 dst_root（不再按前缀过滤）。
    符号链接解引用为实体文件（hgfs 不支持 symlink）。返回复制的文件数。
    """
    count = 0
    for dirpath, _dirnames, filenames in os.walk(src_root, followlinks=True):
        rel = os.path.relpath(dirpath, src_root)
        dst_dir = dst_root if rel == "." else os.path.join(dst_root, rel)
        os.makedirs(dst_dir, exist_ok=True)
        for fn in filenames:
            s = os.path.join(dirpath, fn)
            d = os.path.join(dst_dir, fn)
            try:
                if os.path.islink(s):
                    real = os.path.realpath(s)
                    if not os.path.isfile(real):
                        continue
                    shutil.copy2(real, d)
                elif os.path.isfile(s):
                    shutil.copy2(s, d)
                else:
                    continue
                count += 1
            except Exception as e:
                log(f"      警告: 复制失败 {s} -> {e}")
    return count


def process_url(arch_name: str, url: str, target_root: str) -> tuple:
    """
    处理单个归档 URL。
    返回 (成功数, 失败数, 探测到的 triplet 集合)。
    """
    fname = os.path.basename(url)
    ok, fail = 0, 0
    triplets = set()
    tmp_dir = make_tmp_dir(f"{arch_name}_{fname}")
    try:
        local = os.path.join(tmp_dir, fname)
        extract_dir = os.path.join(tmp_dir, "extract")
        os.makedirs(extract_dir, exist_ok=True)

        download(url, local)
        lower = fname.lower()
        if lower.endswith(".deb"):
            extract_deb(local, extract_dir)
        elif lower.endswith((".tar.gz", ".tgz", ".tar.xz", ".tar.bz2", ".tar")):
            extract_tar(local, extract_dir)
        else:
            raise ValueError(f"不支持的格式: {fname}")

        # 在 tmp 解压树里探测 triplet
        found = detect_triplets(extract_dir)
        if found:
            triplets |= found
            log(f"    探测到 triplet: {', '.join(sorted(found))}")

        n = copy_tree_deref(extract_dir, target_root)
        log(f"    {fname}: 复制 {n} 个文件")
        ok += 1
    except Exception as e:
        log(f"    失败: {fname} -> {e}")
        fail += 1
    finally:
        cleanup_tmp_dir(tmp_dir)

    return ok, fail, triplets


def process_arch(arch_name: str, urls: list) -> tuple:
    """
    返回 (成功数, 失败数, 本架构探测到的 triplet 集合)。
    """
    target_root = os.path.join(THIRD_PARTY, arch_name)
    os.makedirs(target_root, exist_ok=True)
    log(f"\n=== 架构: {arch_name} -> {target_root} ===")

    total_ok, total_fail = 0, 0
    triplets = set()
    log(f"  [{arch_name}] {len(urls)} 个归档")
    for url in urls:
        ok, fail, found = process_url(arch_name, url, target_root)
        total_ok += ok
        total_fail += fail
        triplets |= found
    return total_ok, total_fail, triplets


def clean_arch(arch_name: str) -> None:
    """删除 third_party/<arch>/ 目录。"""
    target_root = os.path.join(THIRD_PARTY, arch_name)
    if os.path.isdir(target_root):
        try:
            shutil.rmtree(target_root)
            log(f"已删除架构目录: {target_root}")
        except Exception as e:
            log(f"警告: 删除失败 {target_root} -> {e}")
    else:
        log(f"架构目录不存在，跳过: {target_root}")


def main() -> None:
    if not os.path.exists(MANIFEST):
        sys.exit(f"找不到清单: {MANIFEST}")
    with open(MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)

    args = sys.argv[1:]
    # 任意位置出现 clean 即进入清理模式；其余参数视为架构名限定
    do_clean = "clean" in args
    arch_args = [a for a in args if a != "clean"]

    # 需要处理的架构集合
    targets = arch_args or list(manifest.keys())

    # 先清理（若指定 clean），然后继续下载，不再直接退出
    if do_clean:
        log("\n=== 清理阶段 ===")
        for arch_name in targets:
            if arch_name not in manifest:
                log(f"警告: 清单中无架构 {arch_name}，仍尝试删除目录")
            clean_arch(arch_name)
        time.sleep(1)

    only = arch_args or None
    summary = {}  # arch -> (ok, fail, triplets)
    for arch_name, urls in manifest.items():
        if only and arch_name not in only:
            continue
        summary[arch_name] = process_arch(arch_name, urls)

    log("\n=== 汇总 ===")
    grand_ok = grand_fail = 0
    for arch_name, (ok, fail, triplets) in summary.items():
        log(f"  {arch_name}: 成功 {ok}，失败 {fail}")
        grand_ok += ok
        grand_fail += fail
    log(f"  合计: 成功 {grand_ok}，失败 {grand_fail}")

    if grand_fail:
        log("\n有归档失败，请检查上面的失败信息（常见原因：文件名不存在、网络中断）。")
        sys.exit(1)

    log("\n完成。编译时按架构使用：")
    for arch_name, (_ok, _fail, triplets) in summary.items():
        inc = f"{THIRD_PARTY}/{arch_name}/usr/include"
        base_lib = f"{THIRD_PARTY}/{arch_name}/usr/lib"
        if triplets:
            for t in sorted(triplets):
                log(f"  [{arch_name}] -I{inc} -L{base_lib}/{t}")
        else:
            # 没探测到 triplet 目录时退回 usr/lib（单架构 deb 常见）
            log(f"  [{arch_name}] -I{inc} -L{base_lib}")


if __name__ == "__main__":
    main()