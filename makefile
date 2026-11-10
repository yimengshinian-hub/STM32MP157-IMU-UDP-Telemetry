# 默认编译 x86 架构（用于本地测试纯软件逻辑）
ARCH ?= x86

# 可执行文件名字 
TARGET = msgsend

# 存放中间文件的路径
BUILD_DIR = build_$(ARCH)

# 源文件文件夹 (当前目录)
SRC_DIR =  .

# 头文件文件夹 (当前目录)
INC_DIR = .

# 列出当前目录下所有的 .cpp 文件
SRCS = $(wildcard $(SRC_DIR)/*.cpp) 

# 替换目标文件
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))

# 指定头文件路径，并加上 C++14 标准
CFLAGS = $(patsubst %,-I%,$(INC_DIR)) -std=c++14 -O2 -Wall

# 链接库参数（极其关键：必须加 -pthread，否则多线程代码会报错）
LDFLAGS = -pthread

# 根据输入的 ARCH 选择编译器
ifeq ($(ARCH),x86)
CC = g++
else 
CC = arm-linux-gnueabihf-g++
endif

# 链接规则（加上了 LDFLAGS）
$(BUILD_DIR)/$(TARGET):$(OBJS) | create_build
	$(CC) -o $@ $^ $(LDFLAGS)

# 编译规则
$(BUILD_DIR)/%.o:$(SRC_DIR)/%.cpp | create_build
	$(CC) -c -o $@ $< $(CFLAGS)

# 伪目标
.PHONY:clean cleanall check create_build

# 调试用
check:
	@echo "Compiler: $(CC)"
	@echo "Sources: $(SRCS)"

# 按架构删除
clean: 
	rm -rf $(BUILD_DIR)

# 全部删除
cleanall:
	rm -rf build_x86 build_arm

# 自动创建构建目录
create_build:
	@mkdir -p $(BUILD_DIR)