# ================================================
# 主目录STM32一键编译脚本
# 功能：自动搜索并编译STM32CubeIDE/VSCode工程
# 用法：1. 在工程目录内直接运行
#       2. 指定工程路径运行：stm32_build.sh /path/to/your_project
# ================================================

# ------------------ 配置区 (可根据需要修改) ------------------
# 默认构建类型：Debug Release RelWithDebInfo
DEFAULT_BUILD_TYPE="Release"
# 你的ARM工具链路径（如果环境变量没设置，可以在此处硬编码）
# ARM_TOOLCHAIN_PATH="/opt/gcc-arm-none-eabi/bin"

# ------------------ 初始化 ------------------
# 颜色定义，用于美化输出
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # 恢复默认颜色

# 打印函数
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 构建类型
BUILD_TYPE="${DEFAULT_BUILD_TYPE}"
# 工程目录
PROJECT_DIR=""

# ------------------ 解析参数 ------------------
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2 # 跳过参数和值
            ;;
        -h|--help)
            echo "用法: $0 [工程路径] [选项]"
            echo "选项:"
            echo "  -t, --type <类型>    设置构建类型 (Debug 或 Release)，默认为 ${DEFAULT_BUILD_TYPE}"
            echo "  -h, --help           显示此帮助信息"
            exit 0
            ;;
        -*)
            print_error "未知选项: $1"
            exit 1
            ;;
        *)
            # 第一个非选项参数视为工程路径
            if [[ -z "$PROJECT_DIR" ]]; then
                PROJECT_DIR=$(realpath "$1")
            else
                print_error "只能指定一个工程路径"
                exit 1
            fi
            shift
            ;;
    esac
done

# ------------------ 1. 定位工程目录 ------------------
if [[ -z "$PROJECT_DIR" ]]; then
    # 如果未提供路径，则在当前目录开始寻找
    START_DIR=$(pwd)
    print_info "未指定工程路径，尝试在当前目录及父目录中搜索STM32工程..."
    
    SEARCH_DIR="$START_DIR"
    while [[ "$SEARCH_DIR" != "/" ]]; do
        # 检查是否有STM32工程标志性文件
        if [[ -f "$SEARCH_DIR/.cproject" ]] || [[ -f "$SEARCH_DIR/CMakeLists.txt" ]] || [[ -f "$SEARCH_DIR/Makefile" ]]; then
            PROJECT_DIR="$SEARCH_DIR"
            print_info "在 $PROJECT_DIR 下找到工程文件"
            break
        fi
        # 向上一级目录搜索
        SEARCH_DIR=$(dirname "$SEARCH_DIR")
    done
fi

# 如果仍未找到，则报错
if [[ -z "$PROJECT_DIR" ]] || [[ ! -d "$PROJECT_DIR" ]]; then
    print_error "未找到有效的STM32工程目录！"
    echo "请确保目录下存在以下任一文件："
    echo "  .cproject, CMakeLists.txt, Makefile"
    echo ""
    echo "你可以："
    echo "  1. 进入工程目录再运行此脚本"
    echo "  2. 将工程路径作为参数传入，例如：$0 ~/my_stm32_project"
    exit 1
fi

print_info "工程目录: $PROJECT_DIR"
print_info "构建类型: $BUILD_TYPE"

# 进入工程目录
cd "$PROJECT_DIR" || { print_error "无法进入目录：$PROJECT_DIR"; exit 1; }

# ------------------ 检测 ARM 工具链并加入 PATH ------------------
if ! command -v arm-none-eabi-gcc &> /dev/null; then
    # 从 CMakeCache 中读取之前配置过的编译器路径
    if [[ -f "build/$BUILD_TYPE/CMakeCache.txt" ]]; then
        CACHED_GCC=$(grep 'CMAKE_C_COMPILER:FILEPATH' "build/$BUILD_TYPE/CMakeCache.txt" | cut -d'=' -f2)
        if [[ -n "$CACHED_GCC" ]]; then
            TOOLCHAIN_DIR=$(dirname "$CACHED_GCC")
            export PATH="$TOOLCHAIN_DIR:$PATH"
        fi
    fi
    # 如果还没找到，搜索常见位置
    if ! command -v arm-none-eabi-gcc &> /dev/null; then
        for tc_dir in \
            "$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/"*"/bin" \
            "/opt/gcc-arm-none-eabi/bin" \
            "/usr/bin"; do
            if [[ -x "$tc_dir/arm-none-eabi-gcc" ]]; then
                export PATH="$tc_dir:$PATH"
                break
            fi
        done
    fi
fi

if ! command -v arm-none-eabi-gcc &> /dev/null; then
    print_error "未找到 arm-none-eabi-gcc，请安装 ARM 工具链或将工具链 bin 目录加入 PATH"
    exit 1
fi

print_info "工具链: $(which arm-none-eabi-gcc)"

# ------------------ 2. 检测并选择构建系统 ------------------
BUILD_SYSTEM=""
BUILD_DIR=""

if [[ -f "CMakeLists.txt" ]]; then
    BUILD_SYSTEM="CMake"
    BUILD_DIR="build/${BUILD_TYPE}" # CMake工程的常见输出结构
    print_info "检测到 CMake 构建系统"
elif [[ -f "Makefile" ]]; then
    BUILD_SYSTEM="Make"
    BUILD_DIR="build" # 或根据 Makefile 确定，这里设为常见值
    print_info "检测到 Makefile 构建系统"
else
    print_error "当前目录下未找到支持的构建系统文件 (CMakeLists.txt 或 Makefile)。"
    exit 1
fi

# ------------------ 3. 执行编译 ------------------
print_info "开始编译，构建目录: $BUILD_DIR"
echo "========================================"

COMPILE_SUCCESS=false
case $BUILD_SYSTEM in
    "CMake")
        # 检查build目录是否存在，若不存在则先配置
        if [[ ! -d "$BUILD_DIR" ]]; then
            print_info "创建并配置构建目录..."
            cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -GNinja
            CONFIG_RESULT=$?
            if [[ $CONFIG_RESULT -ne 0 ]]; then
                print_error "CMake 配置失败！"
                exit $CONFIG_RESULT
            fi
        fi
        
        # 使用 Ninja 或 make 进行编译 (优先 Ninja，更快)
        if command -v ninja &> /dev/null; then
            print_info "使用 Ninja 编译..."
            ninja -C "$BUILD_DIR"
        else
            print_warning "未找到 Ninja，使用 make 编译..."
            cmake --build "$BUILD_DIR"
        fi
        COMPILE_RESULT=$?
        ;;
    "Make")
        # 直接调用 make，可以添加 -j 参数加速并行编译
        CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        print_info "使用 make 编译 (使用 $CORES 个线程)..."
        make -j"$CORES" all
        COMPILE_RESULT=$?
        # 如果默认目标不是 ‘all’，尝试直接 `make`
        if [[ $COMPILE_RESULT -ne 0 ]]; then
            print_warning "‘make all’ 失败，尝试直接运行 ‘make’..."
            make -j"$CORES"
            COMPILE_RESULT=$?
        fi
        ;;
esac

echo "======================================================="
if [[ $COMPILE_RESULT -eq 0 ]]; then
    print_success "编译成功！"
    COMPILE_SUCCESS=true
    
    # 提示生成的文件位置
    case $BUILD_SYSTEM in
        "CMake")
            ELF_PATH="$BUILD_DIR/${PROJECT_DIR##*/}.elf"
            [[ -f "$ELF_PATH" ]] || ELF_PATH="$BUILD_DIR/*.elf"
            ;;
        "Make")
            # 尝试从 Makefile 中推断或查找常见输出文件
            ELF_PATH=$(find . -maxdepth 2 -name "*.elf" -type f | head -1)
            ;;
    esac
    
    if [[ -n "$ELF_PATH" ]] && [[ -f $ELF_PATH ]]; then
        print_info "生成文件: $(realpath "$ELF_PATH")"
        # 同时查找 hex 和 bin 文件
        for ext in hex bin; do
            FILE=$(find "$BUILD_DIR" -name "*.$ext" -type f 2>/dev/null | head -1)
            [[ -n "$FILE" ]] && print_info "        -> $(realpath "$FILE")"
        done
    fi
else
    print_error "编译失败，代码: $COMPILE_RESULT"
    exit $COMPILE_RESULT
fi