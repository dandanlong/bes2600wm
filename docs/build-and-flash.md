# BES2600（BEST2003）工程说明

本工程对应芯片 **BES2600WM（内部代号 BEST2003）**，当前产品板是 `aos_evb_ax4d`。  
耳机内部 Codec 继续走 DAC 模拟输出；同时把同一路 PCM **镜像**到 I2S1，给外部功放/Codec 用。

## 当前 I2S 输出引脚（已启用）

配置开关：`CONFIG_DAILE_DAC_I2S_MIRROR`（在 `boards/best2003_ep/aos_evb_ax4d/board_cfg.mk` 里打开）。  
实际复用：I2S1 主机输出，引脚如下。

| 板级 IO | 芯片引脚 | 功能 | 说明 |
| --- | --- | --- | --- |
| IO20 | P2_0 | I2S_MCLK | 主时钟 |
| IO21 | P2_1 | I2S1_SDO0 | 数据线 DOUT |
| IO22 | P2_2 | I2S1_WS | 左右声道时钟 LRCK |
| IO23 | P2_3 | I2S1_SCK | 位时钟 BCLK |

对应代码：`boards/best2003_ep/aos_evb_ax4d/src/daile_i2s_mirror.c`。  
板级宏：`I2S1_O_IOMUX_INDEX=21`、`I2S_MCLK_IOMUX_INDEX=20`。

注意：`boards/best2003_ep/common/src/daile_i2s_mirror.h` 里还留着旧注释（IO2/3/4/7、I2S0），**当前编译不会用那一套**，以 IO20/21/22/23 为准。

## 全量编译

在工程根目录执行（按顺序编 5 个镜像）：

```bash
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/bootloader -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/ota -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/audio -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/apc1 -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/ap -j
```

清理某个配置：在对应命令后面加 `distclean`，例如：

```bash
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/ap distclean
```

## 编译后的固件位置

全部在 `rtos/nuttx/` 目录：

| 文件 | 用途 | 烧录地址（参考） |
| --- | --- | --- |
| `rtos/nuttx/nuttx_bl.bin` | 启动引导 | 主镜像 `-M` |
| `rtos/nuttx/nuttx_ota.bin` | 恢复/OTA | `0x2C040000` 和 `0x2C0C0000` |
| `rtos/nuttx/nuttx_a7.bin` | A7 音频核 | `0x2C850000` |
| `rtos/nuttx/nuttx_apc1.bin` | 连接核 | 按产品脚本 |
| `rtos/nuttx/nuttx_ap.bin` | 应用核（主固件，含 I2S 镜像） | 主镜像 `-M` |

I2S 镜像逻辑在 **AP 固件**（`nuttx_ap.bin`）里。

## 拉到 Windows 本机并烧录

Windows 本机任选一个目录存放烧录脚本和固件。  
现在连的是编译服务器，不能直接写你的 D 盘。请在 **Windows 电脑** 打开 PowerShell，粘贴下面这一行（可能要输入服务器密码）：

```powershell
scp wang@192.168.1.7:projects/bes-wavetable/packaging/windows_flash/first_install.ps1 .; powershell -NoProfile -ExecutionPolicy Bypass -File .\first_install.ps1
```

它会在脚本所在目录放下其余脚本，并从服务器拉回：

- 烧录工具：`dldtool.exe`、`programmer2003.bin`
- 固件：`nuttx_bl.bin`、`nuttx_ota.bin`、`nuttx_a7.bin`、`nuttx_ap.bin`
- 本机脚本：`下载固件.bat`、`烧录.bat`

### 以后每次编译完

1. 到脚本所在目录双击 **`下载固件.bat`**（从服务器重新拉固件）。
2. 设备管理器看 COM 号，改 `config.txt` 里的 `COM=`（COM6 就写 6）。
3. USB 接下载口，双击 **`烧录.bat`**。

只换应用时，双击 **`烧录-只烧AP.bat`**。

本机需要已安装 **OpenSSH 客户端**（设置 → 应用 → 可选功能），并且能访问 `192.168.1.7`。

### 接线

1. USB 接到板子的 **下载串口**（不是日志口）。
2. 板子上电；烧录开始前必要时按一下复位。

手动命令（COM6 举例）：

```
dldtool.exe 6 programmer2003.bin -M nuttx_bl.bin --addr 0x2C040000 nuttx_ota.bin --addr 0x2C0C0000 nuttx_ota.bin --addr 0x2C850000 nuttx_a7.bin -M nuttx_ap.bin --pgm-rate 2000000
```

### 烧不进去时

- COM 号写错（最常见）。
- 接成了日志口，应换下载口。
- 开始瞬间再按一次复位。
- 先关掉占用串口的串口助手。
- 本机没有 scp：先安装 OpenSSH 客户端。

`nuttx_apc1.bin` 这条板子官方默认不烧，先不用管。

## 合成器代码（wavetable / PCM 采样）

合成器程序名是 `wtsynth`，源码在 `apps/wavetable_synth/`，跑在 **A7 音频核**，不在 AP 应用核。

声音路径：电脑 USB MIDI → AP 的 `usbmidid` → RPMSG → A7 的 `wtsynth` 合成 → `/dev/audio/pcm0p` 回 AP 的声卡/I2S。

三套引擎：

| 引擎 | 文件 | 用途 |
| --- | --- | --- |
| 波表（最多 4 音） | `wt_engine.c` | 内置正弦或 TF 卡单周期波形，调试用 |
| PCM 采样（最多 64 音） | `pcm_sample_engine.c` | 正式吉他音色，读 DBNK 采样库 |
| 64 音压测 | `pcm64_engine.c` | 测 A7 CPU，不用于产品发声 |

合成器专用编译（和日常 `audio`+`ap` 不是同一套 A7）：

```bash
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/bootloader -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/ota -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/a7_wtsynth -j && \
./build.sh boards/best2003_ep/aos_evb_ax4d/configs/ap_wtsynth -j
```

A7 上：`wtsynth -pcm /sdcard/banks/xxx.dbnk -midi`  
AP 上要开 `CONFIG_USB_MIDI_BRIDGE`（`ap_wtsynth` 已开）。

## 已知注意点

- 全量编译约 6～10 分钟（视机器而定），必须按 bootloader → ota → audio → apc1 → ap 顺序编，后一个会覆盖 `rtos/nuttx/nuttx.bin`。
- 改完 menuconfig 后要先 `distclean` 再编。
- 旧头文件 `common/src/daile_i2s_mirror.h` 的 IO2/3/4/7 描述容易误导，后续可删掉或改成与现网一致。
