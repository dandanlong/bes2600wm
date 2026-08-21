# bes2600wm — BES2600 波表合成器

本仓库是 **BES2600WM / BEST2003（板级 `aos_evb_ax4d`）** 吉他波表合成器工程，
只保存**自研代码、产品配置、SDK 适配补丁和烧录/打包脚本**。

供应商 SDK 由 `repo` 管理，并与本仓库目录同级（目录名默认是 `bes`），**不放进本仓库**。
本仓库通过 `scripts/prepare-sdk.sh` 把源码、配置注入 SDK，并对 SDK 打补丁，然后用
`scripts/build.sh` 调用 SDK 原生 `build.sh` 编译。

> **保密提醒**：`patches/` 里的 diff 包含 BES（恒玄）非公开 SDK 的上下文代码，
> 来源是 `partner-gerrit.bestechnic.com`，受合作协议约束。
> **本仓库必须保持 private**，不要公开或转发 `patches/` 内容。

## 目录结构

本仓库检出目录叫什么都可以，只要和 SDK 目录 `bes` 同级：

```
projects/
├── bes/                     # 供应商 SDK（repo 管理，不在本仓库内）
└── bes2600wm/               # 本仓库
    ├── app/
    │   ├── wavetable_synth/     # 合成器主程序（跑在 A7 音频核）
    │   └── daile_btsink/        # 蓝牙 sink 演示（同板衍生）
    ├── board/
    │   ├── configs/             # 新增板级配置：a7_wtsynth / ap_wtsynth / a7_dzq / wavetable
    │   ├── src/                 # daile_i2s_mirror.c/.h（I2S1 镜像输出）
    │   └── common/              # boards/common/src 下的新增头文件
    ├── patches/
    │   ├── sdk-root.patch           # SDK 根仓（boards/chips）已跟踪文件的修改
    │   ├── framework-services.patch # framework/services 的修改（A7 频率/DMA/FX PSRAM/EQ）
    │   └── BASE_COMMITS.txt         # 补丁对应的 SDK 基线提交
    ├── scripts/
    │   ├── prepare-sdk.sh       # 把本仓库内容注入 SDK 并打补丁（幂等）
    │   ├── build.sh             # 调 SDK build.sh 按顺序编译
    │   ├── flash.sh             # 烧录（封装 SDK tools/flash_wtsynth.sh）
    │   ├── serial.sh            # 串口调试（封装 SDK tools/serial_debug.sh）
    │   ├── export-firmware.sh   # 把编译出的固件导出到 out/
    │   └── sdk-tools/           # 注入到 SDK tools/ 的脚本源
    ├── packaging/               # Windows / Linux 烧录打包脚本
    ├── docs/build-and-flash.md  # 详细编译 & 烧录说明（原 SDK 根 README）
    └── out/                     # 固件输出，git 忽略
```

## 快速开始

```bash
# 1. 把本项目注入同级的 ../bes（拷贝源码/配置 + 打补丁，可重复执行）
./scripts/prepare-sdk.sh

# 2. 编译（默认 bootloader → ota → a7_wtsynth → ap_wtsynth）
./scripts/build.sh

# 3. 导出固件到 out/firmware/<时间戳>/
./scripts/export-firmware.sh

# 4. 烧录（需接好下载串口）
./scripts/flash.sh --hw-reset both
```

如果 SDK 不叫 `bes` 或不在同级目录，可临时用 `BES_SDK` 覆盖；正常布局不需要设置任何绝对路径。

## 声音路径

电脑 USB MIDI → AP 的 `usbmidid` → RPMSG → A7 的 `wtsynth` 合成 →
`/dev/audio/pcm0p` 回 AP 声卡 / I2S1 镜像输出（IO20/21/22/23）。

三套引擎：波表（≤4 音，调试）、PCM 采样（≤64 音，正式音色）、64 音压测。

详见 `docs/build-and-flash.md`。

## 与 SDK 的关系

- 本仓库**不改动 SDK 的 `.git`**；所有对 SDK 已跟踪文件的改动都以补丁形式保存在 `patches/`。
- 新增文件（合成器源码、新板级配置、I2S 镜像源）由 `prepare-sdk.sh` 直接拷入 SDK。
- `apps/Kconfig` 由 SDK 构建时自动扫描生成，**无需手工维护**。
- SDK 升级后若补丁冲突，`prepare-sdk.sh` 会尝试 3-way 合并并给出提示。
