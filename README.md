<!--

 * @Description: None
 * @version: V1.0.0
 * @Author: LILYGO
 * @Date: 2025-02-20 16:13:14
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2025-06-17 09:00:00
 * @License: GPL 3.0
-->
<h1 align="center">LexiOS — Sovereign RISC-V Mesh Mobile OS for K230</h1>

<p align="center" width="100%">
    <img src="image/k230_display_product.png" alt="LexiOS on T-Display K230">
</p>

## Overview

**LexiOS** is a hard-real-time, MMU-isolated RISC-V mobile operating system built on RT-Smart (`rv64imafdcv`, XuanTie C908) for the **Kendryte K230** (`LILYGO T-Display-K230` 568x1232 RM69A10 AMOLED + Semtech LR2021 Sub-GHz / 2.4 GHz FLRC transceiver).

Designed as a lightweight, instant-on (`< 1.5s` boot), low-power sovereign alternative to Android, **LexiOS** runs the full **SuperLexicon** Layer-1 blockchain node (`superlexicon.elf`), the **LR2021** FLRC mesh radio daemon (`lora_radio.elf`), and a 60 FPS AMOLED mobile shell (`app.elf`) in `< 95 MB` total RAM.

---

## Key Mobile OS Architecture (The 5 Pillars of LexiOS)

### 1. Screen-Off Deep Standby ("Pocket Mode")
- Pressing **`BOOT0` (`GPIO0`)**, tapping **Enter Pocket Standby Now** in **LexiSystem**, or reaching the configurable touch-inactivity timeout (`30s` / `60s` / `120s` / `Never`) blanks the `RM69A10` MIPI-DSI AMOLED panel (`K_VO_OSD3` zeroed + disabled, `GPIO25` LOW) and pins `CPU1` to **Hardware DVFS Profile `5` (`200 MHz` @ `0.62 V`, `-87.5%` dynamic CPU power)**.
- While the screen is off in your pocket, `superlexicon.elf` and `lora_radio.elf` continue validating blocks and relaying FLRC mesh packets in the background.
- Tapping the `GT9895` capacitive touchscreen or pressing `BOOT0` (`GPIO0`) instantaneously wakes the AMOLED panel and restores the hardware `AUTO` DVFS governor.

### 2. RT-Smart Kernel Enhancements for Modern Runtimes (Rust / Go / Wasm / JIT)
- **Hardware-Entropy CSPRNG (`/dev/urandom` & `/dev/random` + `sys_getrandom`)**:
  - Registered `/dev/urandom` and `/dev/random` character devices in [`drv_ts.c`](canmv_k230/src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/interdrv/tsensor/drv_ts.c) mixed with RISC-V `rdcycle`, `rdtime`, and die temperature ADC entropy.
  - Added a direct hardware-entropy SplitMix64 fallback in `sys_getrandom()` ([`lwp_syscall.c`](canmv_k230/src/rtsmart/rtsmart/kernel/rt-thread/components/lwp/lwp_syscall.c)) and made `sys_madvise()` return `0` (`ENOSYS`-free allocator compatibility).
- **Executable Page Mapping (`PROT_EXEC` + `fence.i`)**:
  - Updated `lwp_mmap2()` ([`lwp_user_mm.c`](canmv_k230/src/rtsmart/rtsmart/kernel/rt-thread/components/lwp/lwp_user_mm.c)) to honor `PROT_EXEC` (`MM_AREA_TYPE_TEXT`) and execute `fence rw, rw; fence.i` for RISC-V instruction-cache coherency.

### 3. Hardware-Protected 6-Stage Thermal & Power Guardian
- **Kernel & Userland Power Gating**: Powers down unused `KPU`, `DPU`, and `VPU` hardware domains at boot (`pm_core.c` & `sample_display.cpp`) and keeps the `nRF9151` cellular modem (`GPIO2`) and Wi-Fi (`GPIO45`) rails off unless explicitly requested.
- **6-Stage Progressive Hardware DVFS**: Scales `CPU1` across `AUTO` (`<50°C`), `1188 MHz` (`50°C`), `800 MHz` (`56°C`), `594 MHz` (`62°C`), `400 MHz` (`68°C`), and `200 MHz` (`>=74°C`) with `3°C` hysteresis and automatic `GPIO42` cooling fan control.

### 4. First-Party LexiOS Mobile Applications
- **`SuperLexicon`**: Native 5-tab AMOLED node, mesh radio, subnet, and block explorer dashboard.
- **`LexiSystem`** ([`lexios_apps.cpp`](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/lexios_apps.cpp)): Real-time die temperature, 6-stage `CPU1` DVFS bar, `GPIO42` fan mode (`Auto` / `Force ON` / `Force OFF`), RF & power-gated domain status, `nRF9151` & physical `TCA8418` keyboard status, and Pocket Standby controls.
- **`LexiChat`** ([`lexios_apps.cpp`](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/lexios_apps.cpp)): Off-grid encrypted FLRC mesh messenger over `superlexicon` JSON-RPC (`127.0.0.1:8545`) with channel switching (`#general`, `#mesh-ops`, `#emergency`), quick-beacon buttons, and seamless physical (`TCA8418`) + touch keyboard input.
- **`LexiSat`** ([`lexios_apps.cpp`](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/lexios_apps.cpp)): Unified **3GPP Release 17 Satellite NTN (`NB-IoT NTN`)**, **GNSS/GPS positioning (`#XGPS` + automatic system `CLOCK_REALTIME` sync)**, **LTE-M/NB-IoT cellular**, and **Physical QWERTY Keyboard (`TCA8418` + `PWM4` backlight)** control center for the LILYGO `nRF9151` keyboard dock. Includes a one-tap **`TX SOS+GPS`** beacon that broadcasts live GPS coordinates simultaneously over the `nRF9151` satellite/cellular link (`AT#XSEND`) and the local **SuperLexicon FLRC Mesh (`#emergency`)**.
- **`LexiApps`** ([`lexios_apps.cpp`](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/lexios_apps.cpp)): MMU-isolated `lwp` process supervisor (`Start` / `Restart` / `Stop` for `superlexicon.elf` & `lora_radio.elf`) and `/sdcard/apps/` `.wasm` / `.elf` package launcher.
- **Curated Utilities**: `LoRa`, `Settings`, `Camera`, `Clock`, and `Calculator`.

### 5. Hardware-Arbitrated `nRF9151` Satellite/GPS & `TCA8418` Keyboard Dock Support
- **Safe `GPIO5` Pin-Mux Arbitration (`LR2021` `NRESET` vs `nRF9151` `UART2_TXD`)**:
  - On T-Display-K230, `GPIO5` is shared between `LR2021` `NRESET` (active-low) and `nRF9151` `UART2_TXD`.
  - When `nRF9151` is powered **OFF** (`0 mW` standby), `lexios_set_nrf9151_power(false)` locks `GPIO5` as a `GPIO` output `HIGH` *before* cutting `GPIO2` (`3.3V` rail), preventing unpowered `nRF9151` ESD clamping diodes from dragging `LR2021` into reset.
  - When `nRF9151` is powered **ON**, `lexios_set_nrf9151_power(true)` raises `GPIO2` (`3.3V` rail) first, waits `150 ms` for the `nRF9151` I/O domain to stabilize, and then muxes `GPIO5`/`GPIO6` to `UART2_TXD`/`UART2_RXD` (where `UART2_TXD` idles `HIGH` at `3.3V`), keeping the `LR2021` FLRC mesh radio online concurrently.
- **System-Wide Physical QWERTY Keyboard (`TCA8418` + `XL9555`)**:
  - Registered as an LVGL `LV_INDEV_TYPE_KEYPAD` device ([`lv_port_indev.c`](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/port/lv_port_indev.c)) with default focus group routing (`lv_group_set_default`) so any focused text field (`LexiChat`, `LexiSat`, `LoRa`, `Settings`) accepts physical keystrokes and wakes the display from **Pocket Standby** on key press.

---


## Version iteration:
| Version                              | Update date                       |Update description|
| :-------------------------------: | :-------------------------------: | :-------------------------------: |
| T-Display-K230_canmv_rt_V1.0     | 2025-02-20      |Original version      |
| T-Display-K230_canmv_rt_V1.1     | 2025-03-25       | update lora model（sx1262） |
| T-Display-K230_canmv_rt_V1.2     | 2025-06-17       |  add ble  |
| T-Display-K230_canmv_rt_V1.3     | 2026-02-10       | add lr2021 battery keyboard gps lte-m fan speaker aht20  |
## PurchaseLink

| Product                     | SOC           |  FLASH  |  LPDDR   | Link                   |
| :------------------------: | :-----------: |:-------: | :---------: | :------------------: |
| T-Display-K230_canmv_rt| k230 |   SD Card   | 1GiB | [LILYGO Mall](https://lilygo.cc/products/t-display-k230?_pos=2&_psq=k230&_ss=e&_v=1.0&variant=45197449953461)  |

## Directory
- [Describe](#describe)
- [Preview](#preview)
- [Module](#module)
- [QuickStart](#quickstart)
- [PinOverview](#pinoverview)
- [RelatedTests](#RelatedTests)
- [FAQ](#faq)
- [Project](#project)
- [Information](#information)
- [DependentLibraries](#dependentlibraries)

## Description

T-Display-K230 is a development board featuring a high-definition AMOLED display, based on the k230. <!--designed for standalone battery connectivity.-->

### Preview

#### Actual Product Image

<p align="center" width="100%">
    <img src="image/k230_display_product.png" alt="">
</p>


## Module

### 1. MCU

* Chip: k230
* For more details, please visit    [k230 Datasheet](datasheet/K230_datasheet.pdf)

### 2. Screen

* Size: 4.1-inch AMOLED  screen
* Resolution: 568x1232px
* Screen type: AMOLED
* Driver chip: RM69A10

### 3. Touch

* Chip: GT9895
* Bus communication protocol: IIC

## QuickStart

### Examples Support



|                    Firmware                                                |                                    Description                               |
| ---------------------------------------------------------------------------| -----------------------------------------------------------------------------|
|   [firmware_V1.2](firmware/CanMV-K230-V3P0_rtsmart_releaseV1.2.zip)        |   update touch  add ble                                                      |
|   [firmware_V1.3](firmware/CanMV-K230-V3P0_rtsmart_releaseV1.3.zip)        |   update i2c  add lr2021 battery keyboard gps lte-m fan speaker aht20        |

|             app_name             |             Description                 |                   path                 |
|----------------------------------| ----------------------------------------| -------------------------------------- |
|  lora                            |   sx1262/lr2021  send and receive                      |     [lora](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/lora)                                   |
|  clock                            |   stopwatch timer                      |     [clock](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/clock)                                   |
|  camera                            |support three  camera    preview                   |     [camera](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/camera)                                   |
|  Face_detect                            | face detect by camera                  |     [Face_detect](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/camera_face_detect)                                   |
|  Setting                            | 1.control screen backlight2.connect wifi3.see version                 |     [setting](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/setting)                                   |
|  music                            | play music    Listen to the sound through the headphones             |     [music](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/music)                                   |
|  calculator                            |   calc（+—*/%）          |     [calculator](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/calculator)                                   |
|  led                            |   control corlor led          |     [led](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/led)                                   |
|  Artboard                            |   test screen color/touch          |     [artboard](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/artboard)                                   |
|  Ble                            |   test ble（send/receive）  commit to your phone by bluebooth          |     [ble](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/ble)                                   |
|  Video                            |   Record and play videos through camera capture         |     [video](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/video)                                   |
|  Keyboard                           |   Inputting text through the keyboard and backlight control        |     [keyboard](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/keyboard)                                   |
|  LiveCall                           |    Realize remote video transmission through LR2021       |     [Livecall](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/live_call)                                   |
|  Gnss                           |     GPS satellite positioning      |     [gnss](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/gnss)                                   |
|  Cellular                           |     SIM information confirmation and acquisition      |     [cellular](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/cellular)                                   |
|  Battery                           |    Monitor the charging and discharging as well as the battery's power level information     |     [battery](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/battery)                                   |
|  game_2048                           |    2048 game     |     [game_2048](canmv_k230/src/rtsmart/mpp/userapps/sample/sample_display/ui_brookesia/src/app_examples/phone/game_2048)                                   |
#### **k230**

## App Compilation

change to current dir canmv_k230


      cd canmv_k230/src/rtsmart/mpp
      source build_env.sh
      cd userapps/sample/sample_display
      make

in the directory sample/elf     generate sample_display.elf

default app: sample_display


rename sample_display.elf to app.elf  copy to sdcard, Power on again and start running by default.

## Advanced - Custom Firmware

### 1. Overview

Note

This chapter introduces how to develop on the K230 CanMV. If you have no custom requirements, you can skip this chapter.

The K230 CanMV is developed based on the K230 SDK 

### 2. Setting Up the Development Environment

| Host Environment            | Description                                                  |
| --------------------------- | ------------------------------------------------------------ |
| Ubuntu 20.04.4 LTS (x86_64) | The K230 CanMV compilation environment is suitable for Ubuntu 20.04 and above. |

Currently, K230 CanMV has only been verified to compile in a Linux environment. Other Linux versions have not been tested, so compatibility with other systems cannot be guaranteed.

#### 2.1 Local Build Environment

- Update APT sources (optional)

```sh
sudo bash -c 'cp /etc/apt/sources.list /etc/apt/sources_bak.list && \
  sed -i "s/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list && \
  sed -i "s/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g" /etc/apt/sources.list'
```

- Install necessary dependencies

```sh
# Add support for i386 architecture
sudo bash -c 'dpkg --add-architecture i386 && \
  apt-get clean all && \
  apt-get update && \
  apt-get install -y --fix-broken --fix-missing --no-install-recommends \
    sudo vim wget curl git git-lfs openssh-client net-tools sed tzdata expect mtd-utils inetutils-ping locales \
    sed make cmake binutils build-essential diffutils gcc g++ bash patch gzip bzip2 perl tar cpio unzip rsync \
    file bc findutils dosfstools mtools bison flex autoconf automake python3 python3-pip python3-dev python-is-python3 \
    lib32z1 scons libncurses5-dev kmod fakeroot pigz tree doxygen gawk pkg-config libyaml-dev libconfuse2 libconfuse-dev \
    libssl-dev libc6-dev-i386 libncurses5:i386'
```



- Update PIP sources (optional)

```sh
pip3 config set global.index-url https://pypi.tuna.tsinghua.edu.cn/simple && \
pip3 config set global.extra-index-url "https://mirrors.aliyun.com/pypi/simple https://mirrors.cloud.tencent.com/pypi/simple"
```



- Install Python dependencies

```sh
pip3 install -U pyyaml pycryptodome gmssl jsonschema jinja2
```


- Install the repo tool

```sh
mkdir -p ~/.bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/.bin/repo
chmod a+rx ~/.bin/repo
echo 'export PATH="${HOME}/.bin:${PATH}"' >> ~/.bashrc
source ~/.bashrc
```

### 3. Compilation Process

#### 3.1 Source Code Download

The source code of CanMV-K230 is hosted on Github. Users can download the source code using the repo tool.

```sh
# It's recommended to create a directory in the user's home directory before downloading the code
mkdir -p ~/canmv_k230_pro && cd ~/canmv_k230_pro

# Download the RT-Smart + CanMV project
git clone https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt.git
```


#### 3.2 Code Preparation

When compiling for the first time, you need to download the toolchain. The following command only needs to be executed once.

```sh
cd canmv_k230
# Download the toolchain when running for the first time
make dl_toolchain
```


#### 3.3 Compilation

Select the corresponding board configuration file according to actual needs, and then start compiling.

```sh
# List available configuration options
make list_def 
# Select the corresponding board configuration file
make k230_canmv_defconfig  # Replace with the appropriate defconfig for your board
# Start compilation
time make log
```


After compilation, the image files will be generated in the `canmv_k230_pro/canmv_k230/output/xxxx/xxx.img` directory.

## Firmware download

### 1. Flashing on Windows Platform

On a Windows system, you can use the Rufus tool to flash the firmware to a TF card. The download address for Rufus is: [Rufus Official Website](http://rufus.ie/downloads/).

1. Insert the TF card into your computer and start the Rufus tool. Click the “Select” button in the interface and choose the firmware file to be flashed. ![rufus-flash-from-file](https://www.kendryte.com/k230_canmv/en/main/_images/rufus_select.png)
2. Click the “Start” button, and Rufus will automatically proceed with the flashing. The progress bar will display the flashing progress, and the system will prompt “Ready” upon completion. ![rufus-flash](https://www.kendryte.com/k230_canmv/en/main/_images/rufus_start.png) ![rufus-sure](https://www.kendryte.com/k230_canmv/en/main/_images/rufus_sure.png) ![rufus-warning](https://www.kendryte.com/k230_canmv/en/main/_images/rufus_warning.png) ![rufus-finish](https://www.kendryte.com/k230_canmv/en/main/_images/rufus_finish.png)

### 2. Flashing on Linux Platform

Before inserting the TF card, first run the following command to check the current storage devices:

```sh
ls -l /dev/sd\*
```


Next, insert the TF card into the host machine and run the same command again to identify the newly added device node, which is the device node for the TF card.

Assuming the device node for the TF card is `/dev/sdc`, you can use the following command to flash the firmware to the TF card:

```sh
sudo dd if=sysimage-sdcard.img of=/dev/sdc bs=1M oflag=sync
```

## PinOverview

| AMOLED Screen Pin  | k230 Pin|
| :------------------: | :------------------:|
| LCD_RST   | GPIO22  |

| Touch Chip Pin  | k230 Pin |
| :------------------: | :------------------:|
| TP_RST   | GPIO24 |
| TP_SCL | GPIO36 |
| TP_SDA | GPIO37 |
| TP_INT | GPIO23 |

| HDMI Pin | k230 Pin |
| :------------------: | :------------------:|
| HDMI_RSTN | GPIO24  |
| HDMI_INT | GPIO22 |
| HDMI_CSCL | GPIO36 |
| HDMI_CSDA | GPIO37 |

| SD Card Pin  | k230 Pin |
| :------------------: | :------------------:|
| TFCARD_CMD  |  GPIO54  |
| TFCARD_CLK  |  GPIO55  |
|  TFCARD_D0  |  GPIO56  |
|  TFCARD_D1  |  GPIO57  |
|  TFCARD_D2  |  GPIO58  |
|  TFCARD_D3  |  GPIO59  |

## RelatedTests

<!--

### Power Dissipation

| Firmware | Program| Description | Picture |
| ------  | ------  | ------ | ------ |
| `[T-Display-k230-AMOLED-1.43_V1.0][Light_Sleep_Wake_Up]_firmware_V1.0.0.bin` | `Light Sleep Wake Up` | Power dissipation: 1282.8uA | <p align="center" width="10%"> <img src="image/13.jpg" alt="example" width="50%"> </p> |
| `[T-Display-k230-AMOLED-1.43_V1.0][Deep_Sleep_Wake_Up]_firmware_V1.0.0.bin` | `Deep Sleep Wake Up` | Power dissipation: 174.2uA |<p align="center" width="10%"> <img src="image/12.jpg" alt="example" width="50%"> </p> |

-->

## FAQ

* Q. After reading the above tutorials, I still don't know how to build a programming environment. What should I do?
* A. If you still don't understand how to build an environment after reading the above tutorials, you can refer to the [LilyGo-Document](https://github.com/Xinyuan-LilyGO/LilyGo-Document) document instructions to build it.

## Project
* [T-Display K230 V1.0.pdf](./schematic/T-Display K230 V1.0.pdf)

## Information
* [sx1262](./datasheet/HPDTEK_HPD16A_Series_规格书_V0.3.pdf)

  

## DependentLibraries
* [lvgl-8.4.1](https://lvgl.io)

