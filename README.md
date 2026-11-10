# STM32MP157 Linux 用户态 IMU 采集与 UDP 遥测原型

基于 STM32MP157 S1 Pro 开发板和板载 MPU6050，实现 Linux 用户态读取三轴加速度，并通过 UDP 将采样数据发送到 Windows PC 接收端进行验证。

> 这是一个设备采集与 UDP 遥测原型，不是 Linux 内核驱动或工业级网关。传感器数据尚未完成校准；项目目前不包含 Web 页面、TCP、陀螺仪采集或姿态估计算法。

## 数据链路

```text
板载 MPU6050
    │ I²C
    ▼
STM32MP157 Linux 用户态 C++ 程序
    │ UDP/IPv4 数据报
    ▼
Windows PC 上的 receiver.py
```

本次联调使用 USB 网络连接。测试时板端 USB Gadget 网卡地址为 `192.168.137.2`，Windows 接收端地址为 `192.168.137.1`。实际使用请按当前网络配置修改板端目标 IP。

## 实现内容

- 使用 Linux `i2c-dev` 接口，通过 `I2C_RDWR` 组合消息读取、写入 MPU6050 寄存器。
- 检查 `WHO_AM_I` 寄存器，验证返回值为 `0x68`。
- 将加速度量程设置并读回确认至 ±2g。
- 从 `ACCEL_XOUT_H` 起连续读取 X/Y/Z 三轴加速度原始值，进行有符号数据转换，并按 ±2g 灵敏度换算为 `g`。
- 使用 `std::chrono::steady_clock` 配合 `sleep_until`，以约 500ms 的目标周期采样。
- 将采样序号、单调时钟毫秒值、状态和三轴加速度组成文本 UDP 数据报发送至 PC。
- PC 端接收程序显示数据报的来源地址和内容。

## 代码结构

```text
msgsend_arm/
├── msgsend.cpp   # 板端 I²C 采集与 UDP 发送程序
├── receiver.py   # PC 端 UDP 接收程序
├── makefile      # x86/ARM 构建规则
└── README.md
```

## 环境要求

### 板端

- STM32MP157 S1 Pro，运行 Linux。
- MPU6050 可通过 I²C 访问。
- `/dev/i2c-0` 存在，当前测试地址为 `0x68`。
- 原生 C++ 编译器或 `arm-linux-gnueabihf-g++` 交叉编译工具链。
- 运行用户需要有访问 I²C 设备节点的权限；当前验证使用 `sudo` 运行。

### PC 端

- Windows 与开发板之间有可达的 IP 网络。本次测试通过 USB Gadget 网络连接。
- Python 3（`receiver.py` 使用标准库，无需额外安装包），或其他能监听 UDP 8080 的接收工具。

## 构建与运行

### 1. 配置 PC 地址

打开 `msgsend.cpp`，根据 PC 当前网卡地址修改接收端 IP 与端口：

```cpp
constexpr const char* receiverIp = "192.168.137.1";
constexpr uint16_t receiverPort = 8080;
```

### 2. 启动 PC 接收程序

在 Windows PowerShell 中，进入项目目录并运行：

```powershell
python receiver.py
```

接收端绑定 `0.0.0.0:8080`，即监听本机所有 IPv4 网卡上的 UDP 8080 端口。若 Windows 防火墙拦截，请仅放行接收程序或相应 UDP 入站端口，不需要关闭整个防火墙。

### 3. 构建板端程序

在有 ARM 交叉工具链的 Linux 构建环境中：

```bash
make ARCH=arm
```

按 Makefile，ARM 可执行文件生成在：

```text
build_arm/msgsend
```

也可在开发板上使用原生编译器构建：

```bash
g++ -std=c++14 -O2 -Wall msgsend.cpp -pthread -o msgsend
```

### 4. 运行板端程序

确认 `receiverIp` 已改成 PC 可达的地址，且 PC 接收程序已启动，然后在板端运行：

```bash
sudo ./build_arm/msgsend
```

如果是在板端原生编译，则运行：

```bash
sudo ./msgsend
```

按 `Ctrl+C` 停止程序。不要通过 `chmod 666 /dev/i2c-0` 长期放开设备权限。

## UDP 数据格式

当前每次采样发送一个 UTF-8 文本 UDP 数据报，例如：

```text
seq=12,mono_ms=3055185,status=ok,ax_g=-0.029,ay_g=0.004,az_g=0.868
```

| 字段 | 含义 |
|---|---|
| `seq` | 采样序号；程序重启后从头计数 |
| `mono_ms` | 板端单调时钟毫秒值，不是日期时间；重启后基准会变化 |
| `status` | 本次 I²C 采集状态，例如 `ok` 或 `i2c_error` |
| `ax_g`、`ay_g`、`az_g` | 三轴加速度，单位为 `g`，当前未做偏置校准 |

当前采用 UDP。每次 `sendto` 发送一个数据报，但 UDP 不保证送达或顺序；序号可帮助接收端观察缺号，当前尚未进行正式丢包率测试。

## 已完成的验证

- MPU6050 `WHO_AM_I` 返回 `0x68`。
- 加速度量程配置读回为 ±2g。
- 板子静置、翻转和晃动时，三轴读数随方向与运动发生变化。
- Windows 接收程序收到从开发板发来的 UDP 数据报，数据中可见序号、时间戳、状态和三轴读数。
- 联调截图中可见序号连续递增、单调时钟值按约 500ms 前进；这只是截图所示片段，不代表完整运行期间零丢包或长期稳定性已验证。

## 当前限制

- 六面测试中观察到 Z 轴正反面静态读数存在不对称，尚未完成偏置/比例校准；目前按未校准数据处理。
- 当前只采集加速度，没有读取陀螺仪数据，也没有实现姿态角、跌落识别或滤波算法。
- 尚未完成长时间运行、CPU/RSS、实际采样间隔抖动及 UDP 丢包率的系统测试。
- 未实现 UDP 确认、重传、自动网络重连或 Web 集成。
- 通过 `i2c-dev` 访问属于 Linux 用户态使用内核 I²C 子系统，不是自行编写内核驱动。
