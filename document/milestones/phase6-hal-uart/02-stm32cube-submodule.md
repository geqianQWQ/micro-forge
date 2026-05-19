# Phase 6.1 · STM32CubeF1 Submodule

## 目标

HAL 库必须以 recursive Git submodule 引入，不使用 zip、手动拷贝或 CMake `FetchContent`。

默认路径：

```text
third_party/STM32CubeF1
```

## 默认命令

优先固定 `v1.8.6`：

```bash
git submodule add --branch v1.8.6 https://github.com/STMicroelectronics/STM32CubeF1.git third_party/STM32CubeF1
git submodule update --init --recursive third_party/STM32CubeF1
```

如果 `v1.8.6` tag 不可用，使用当前 `master` commit 并记录 SHA：

```bash
git submodule add https://github.com/STMicroelectronics/STM32CubeF1.git third_party/STM32CubeF1
git submodule update --init --recursive third_party/STM32CubeF1
git -C third_party/STM32CubeF1 rev-parse HEAD
```

## Clone / Build 要求

任何 clone/build 文档都必须提醒：

```bash
git clone --recursive <repo-url>
git submodule update --init --recursive
```

## CMake 行为

- HAL fixture 只有在 `third_party/STM32CubeF1` 存在且初始化完成时启用。
- submodule 缺失时，CMake 必须输出明确提示。
- 不允许把缺失 HAL fixture 伪装成 E2E 通过。

## 验收

- 仓库包含 `.gitmodules`。
- `third_party/STM32CubeF1` 是 submodule，不是普通目录。
- `git submodule status --recursive` 能看到 STM32CubeF1 及内部依赖状态。
- 本地和 CI 均使用 recursive init/update。
