#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

namespace {

volatile std::sig_atomic_t running = 1;

void handleSignal(int) {
    running = 0;
}

bool i2cTransfer(int fd, i2c_msg* messages, uint32_t count) {
    i2c_rdwr_ioctl_data request{};
    request.msgs = messages;
    request.nmsgs = count;

    int result = ioctl(fd, I2C_RDWR, &request);
    if (result < 0) {
        std::perror("ioctl(I2C_RDWR)");
        return false;
    }

    if (static_cast<uint32_t>(result) != count) {
        std::cerr << "I2C 消息未完整传输: "
                  << result << "/" << count << '\n';
        return false;
    }

    return true;
}

bool readRegister(int fd, uint8_t address, uint8_t reg, uint8_t& value) {
    uint8_t data = 0;
    i2c_msg messages[2]{};

    messages[0].addr = address;
    messages[0].flags = 0;
    messages[0].len = 1;
    messages[0].buf = &reg;

    messages[1].addr = address;
    messages[1].flags = I2C_M_RD;
    messages[1].len = 1;
    messages[1].buf = &data;

    if (!i2cTransfer(fd, messages, 2)) {
        return false;
    }

    value = data;
    return true;
}

bool writeRegister(int fd, uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    i2c_msg message{};

    message.addr = address;
    message.flags = 0;
    message.len = 2;
    message.buf = data;

    return i2cTransfer(fd, &message, 1);
}

int32_t decodeSigned16(uint8_t high, uint8_t low) {
    uint16_t raw =
        (static_cast<uint16_t>(high) << 8) |
        static_cast<uint16_t>(low);

    return (raw & 0x8000)
        ? static_cast<int32_t>(raw) - 65536
        : static_cast<int32_t>(raw);
}

bool readAccel(int fd, uint8_t address,
               double& xG, double& yG, double& zG) {
    uint8_t reg = 0x3B; // ACCEL_XOUT_H
    uint8_t data[6]{};
    i2c_msg messages[2]{};

    messages[0].addr = address;
    messages[0].flags = 0;
    messages[0].len = 1;
    messages[0].buf = &reg;

    messages[1].addr = address;
    messages[1].flags = I2C_M_RD;
    messages[1].len = 6;
    messages[1].buf = data;

    if (!i2cTransfer(fd, messages, 2)) {
        return false;
    }

    const int32_t rawX = decodeSigned16(data[0], data[1]);
    const int32_t rawY = decodeSigned16(data[2], data[3]);
    const int32_t rawZ = decodeSigned16(data[4], data[5]);

    // 仅当 ACCEL_CONFIG 的量程是 ±2g 时，灵敏度才是 16384 LSB/g。
    constexpr double lsbPerG = 16384.0;
    xG = rawX / lsbPerG;
    yG = rawY / lsbPerG;
    zG = rawZ / lsbPerG;
    return true;
}

} // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    constexpr uint8_t imuAddress = 0x68;
    constexpr const char* i2cDevice = "/dev/i2c-0";
    constexpr const char* receiverIp = "192.168.137.1";
    constexpr uint16_t receiverPort = 8080;
    constexpr auto period = std::chrono::milliseconds(500);

    int i2cFd = open(i2cDevice, O_RDWR | O_CLOEXEC);
    if (i2cFd < 0) {
        std::perror("open I2C");
        return 1;
    }

    uint8_t whoAmI = 0;
    if (!readRegister(i2cFd, imuAddress, 0x75, whoAmI) || whoAmI != 0x68) {
        std::cerr << "MPU6050 WHO_AM_I 检查失败，读到 0x"
                  << std::hex << static_cast<unsigned>(whoAmI)
                  << std::dec << '\n';
        close(i2cFd);
        return 1;
    }

    // 明确设置加速度量程为 ±2g。
    uint8_t accelConfig = 0;
    if (!readRegister(i2cFd, imuAddress, 0x1C, accelConfig) ||
        !writeRegister(i2cFd, imuAddress, 0x1C,
                       static_cast<uint8_t>(accelConfig & ~0x18))) {
        std::cerr << "设置 MPU6050 ±2g 量程失败\n";
        close(i2cFd);
        return 1;
    }

    if (!readRegister(i2cFd, imuAddress, 0x1C, accelConfig) ||
        (accelConfig & 0x18) != 0) {
        std::cerr << "无法确认 MPU6050 量程为 ±2g\n";
        close(i2cFd);
        return 1;
    }

    // 唤醒传感器：PWR_MGMT_1 的 SLEEP 位清零。
    if (!writeRegister(i2cFd, imuAddress, 0x6B, 0x00)) {
        std::cerr << "唤醒 MPU6050 失败\n";
        close(i2cFd);
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd < 0) {
        std::perror("socket");
        close(i2cFd);
        return 1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(receiverPort);

    if (inet_pton(AF_INET, receiverIp, &server.sin_addr) != 1) {
        std::cerr << "接收端 IP 地址无效: " << receiverIp << '\n';
        close(udpFd);
        close(i2cFd);
        return 1;
    }

    std::cout << "开始发送 UDP 到 "
              << receiverIp << ':' << receiverPort
              << "，每 500ms 采样一次；Ctrl+C 退出\n";

    uint64_t sequence = 0;
    auto nextSample = std::chrono::steady_clock::now();

    while (running) {
        double x = 0.0, y = 0.0, z = 0.0;
        char buffer[160];

        const auto now = std::chrono::steady_clock::now();
        const auto monoMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

        if (readAccel(i2cFd, imuAddress, x, y, z)) {
            std::snprintf(buffer, sizeof(buffer),
                          "seq=%llu,mono_ms=%lld,status=ok,ax_g=%.3f,ay_g=%.3f,az_g=%.3f",
                          static_cast<unsigned long long>(sequence++),
                          static_cast<long long>(monoMs), x, y, z);
        } else {
            std::snprintf(buffer, sizeof(buffer),
                          "seq=%llu,mono_ms=%lld,status=i2c_error",
                          static_cast<unsigned long long>(sequence++),
                          static_cast<long long>(monoMs));
        }

        const size_t length = std::strlen(buffer);
        const ssize_t sent = sendto(
            udpFd, buffer, length, 0,
            reinterpret_cast<sockaddr*>(&server), sizeof(server));

        if (sent < 0) {
            std::perror("sendto");
        } else if (static_cast<size_t>(sent) != length) {
            std::cerr << "UDP 数据报未完整发送\n";
        }

        nextSample += period;
        const auto after = std::chrono::steady_clock::now();
        if (nextSample <= after) {
            nextSample = after + period;
        }
        std::this_thread::sleep_until(nextSample);
    }

    close(udpFd);
    close(i2cFd);
    std::cout << "\n程序退出\n";
    return 0;
}
