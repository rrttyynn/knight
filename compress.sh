#!/bin/bash
set -e

mkdir -p build

# 1. 合并源码为单个致密流 (剥离冗余包含)
cat src/chess.cpp > build/amalgamation.cpp
grep -v '#include "chess.cpp"' src/eval.cpp >> build/amalgamation.cpp
grep -v '#include "eval.cpp"' src/board.cpp >> build/amalgamation.cpp
grep -v '#include "board.cpp"' src/search.cpp >> build/amalgamation.cpp
grep -v '#include "search.cpp"' src/main.cpp >> build/amalgamation.cpp

# 2. 移除空行与行首空格缩进
sed -i '/^[[:space:]]*$/d' build/amalgamation.cpp
sed -i 's/^[[:space:]]\+//' build/amalgamation.cpp

RAW_SIZE=$(wc -c < build/amalgamation.cpp)
echo "[1] 致密化源码体积: $RAW_SIZE 字节"

# 3. 构造自解压执行体头 (初次运行自动编译并缓存至 /tmp)
cat << 'HEADER' > k8e_release
#!/bin/sh
B=/tmp/k8e_bin.$USER
[ -x $B ]||{ sed '1,/^#PKG_DATA$/d' "$0"|xz -d|g++ -O3 -std=c++20 -march=native -DNDEBUG -fno-rtti -x c++ - -o $B&&strip $B;}
exec $B "$@"
#PKG_DATA
HEADER

# 4. 使用低内存占用的 xz 预设 (-6 仅需约 80MB 内存) 进行极限压缩
xz -6 --memlimit-compress=200MiB -c build/amalgamation.cpp >> k8e_release
chmod +x k8e_release

FINAL_SIZE=$(wc -c < k8e_release)
echo "[2] 最终打包产物体积 (k8e_release): $FINAL_SIZE 字节"

if [ $FINAL_SIZE -le 8192 ]; then
    echo "===> 成功达成目标：产物严格小于等于 8192 字节 (8KB)！"
else
    echo "===> 超出 8192 字节限制，需进一步精简。"
fi
