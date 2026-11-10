import socket

HOST = "0.0.0.0"  # 监听本机所有 IPv4 网卡
PORT = 8080

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))
print(f"监听 UDP {HOST}:{PORT}，按 Ctrl+C 退出")

try:
    while True:
        data, addr = sock.recvfrom(2048)
        print(f"{addr[0]}:{addr[1]}  {data.decode('utf-8', errors='replace')}")
except KeyboardInterrupt:
    print("\n接收结束")
finally:
    sock.close()
