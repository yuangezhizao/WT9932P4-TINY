import argparse
import json
import os
import shutil
import subprocess
import sys


def run(command, cwd=None):
    subprocess.run(command, cwd=cwd, check=True)


def resolve_dep_path(deps_dir, rel_path):
    """限制依赖只能落在 dependencies/ 内, 防止 ../ 或绝对路径逃逸。"""
    deps = os.path.realpath(deps_dir)
    full = os.path.realpath(os.path.join(deps, rel_path))
    if full == deps or os.path.commonpath([full, deps]) != deps:
        raise ValueError(f"非法的依赖路径(必须位于 dependencies/ 内): {rel_path}")
    return full


def clone_or_update_repo(repo_url, path, branch):
    if os.path.isdir(os.path.join(path, ".git")):
        # 已是 git 仓库: 强制对齐远程(应对远程 push -f 重写历史)
        run(["git", "-C", path, "fetch", "origin", "--prune"])
        if branch:
            run(["git", "-C", path, "checkout", "-B", branch, f"origin/{branch}"])
            run(["git", "-C", path, "reset", "--hard", f"origin/{branch}"])
        else:
            # 无 branch: 先检测上游是否存在, 给出友好错误信息
            result = subprocess.run(
                ["git", "-C", path, "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"],
                capture_output=True, text=True
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"'{path}' 无远程追踪分支, 建议在 repos.json 中显式写 branch"
                )
            run(["git", "-C", path, "reset", "--hard", "@{u}"])
        print(f"  已更新: dependencies/{os.path.basename(path)}")
    else:
        # 目录存在但非 git 仓库(残留/中断/手动创建): 删除 dependencies/<repo> 下全部内容后重新 clone
        if os.path.exists(path):
            print(f"  警告: '{path}' 存在但不是 git 仓库, 即将删除该目录下全部内容...")
            shutil.rmtree(path)
        command = ["git", "clone"]
        if branch:
            command += ["-b", branch]
        command += [repo_url, path]
        run(command)
        print(f"  已克隆: dependencies/{os.path.basename(path)}")


def main():
    parser = argparse.ArgumentParser(description="拉取 repos.json 中配置的依赖仓库")
    parser.add_argument("--yes", "-y", action="store_true", help="跳过交互确认(CI 使用)")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    deps_dir = os.path.join(script_dir, "dependencies")

    # 确保 dependencies/ 目录存在(不依赖 CI 的 mkdir 或 .gitkeep)
    os.makedirs(deps_dir, exist_ok=True)

    with open(os.path.join(script_dir, "repos.json")) as f:
        repos = json.load(f)

    if not repos:
        print("repos.json 中未配置任何依赖, 无需处理。")
        return

    # 解析全部路径, 提前检测逃逸(确认前即报错)
    resolved = []
    for repo in repos:
        repo_path = resolve_dep_path(deps_dir, repo["path"])
        resolved.append((repo, repo_path))

    print("待拉取的依赖:")
    for repo, repo_path in resolved:
        branch_info = f" (branch: {repo['branch']})" if repo.get("branch") else ""
        # 如果目标存在但不是 git 仓库, 在确认阶段就明确提示将删除
        if os.path.exists(repo_path) and not os.path.isdir(os.path.join(repo_path, ".git")):
            note = " [⚠ 目录存在但非 git 仓库, 确认后将删除其全部内容]"
        else:
            note = ""
        print(f"  - {repo['url']} -> dependencies/{repo['path']}{branch_info}{note}")

    if not args.yes:
        try:
            answer = input("确认执行? [y/N] ").strip().lower()
        except EOFError:
            print("检测到非交互式环境, 请使用 --yes / -y 跳过确认。")
            sys.exit(1)
        if answer not in ("y", "yes"):
            print("已取消。")
            sys.exit(0)

    for repo, repo_path in resolved:
        clone_or_update_repo(repo["url"], repo_path, repo.get("branch"))

    print(f"\n全部依赖拉取完成(共 {len(resolved)} 个)。")


if __name__ == "__main__":
    print("开始拉取依赖仓库 ...")
    main()
