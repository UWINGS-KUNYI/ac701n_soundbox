#!/bin/sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEFAULT_POST_BUILD="$(cd "$SCRIPT_DIR/../../../../../../postbuild" && pwd)"
: "${POST_BUILD_TOOLS_DIR:=$DEFAULT_POST_BUILD}"
POST_BUILD_TOOLS_DIR="$(cd "$POST_BUILD_TOOLS_DIR" 2>/dev/null && pwd)"

# 切換到 download.sh 所在目錄
cd "$SCRIPT_DIR"

TOOLS_DIR=../..

if [ ! -d "$POST_BUILD_TOOLS_DIR" ]; then
    echo "Error: Post-build tools directory not found: $POST_BUILD_TOOLS_DIR"
    exit 1
fi

rm -f jl_isd.fw jl_isd.ufw update.ufw
# 複製需要的檔案到當前目錄（對應 bat 的 copy）
cp $TOOLS_DIR/tone.cfg .
cp $TOOLS_DIR/p11_code.bin .
cp $TOOLS_DIR/br28loader.bin .
cp $TOOLS_DIR/ota.bin .

# 燒錄主程式（對應 bat 的 isd_download.exe）
"$POST_BUILD_TOOLS_DIR/isd_download" $TOOLS_DIR/isd_config.ini \
    -tonorflash -dev br28 -boot 0x120000 -div8 -wait 300 \
    -uboot $TOOLS_DIR/uboot.boot \
    -app $TOOLS_DIR/app.bin \
    -res tone.cfg $TOOLS_DIR/cfg_tool.bin $TOOLS_DIR/eq_cfg_hw.bin p11_code.bin \
    -uboot_compress

# 生成 OTA 升級韌體（對應 bat 的 fw_add.exe）
"$POST_BUILD_TOOLS_DIR/fw_add" -noenc -fw jl_isd.fw -add $TOOLS_DIR/ota.bin -type 100 -out jl_isd.fw

# 如果有 script.ver 才加入版本資訊
if [ -f $TOOLS_DIR/script.ver ]; then
    cp $TOOLS_DIR/script.ver .
    "$POST_BUILD_TOOLS_DIR/fw_add" -noenc -fw jl_isd.fw -add script.ver -out jl_isd.fw
    rm -f script.ver
fi

# 生成 .ufw 升級檔（對應 bat 的 ufw_maker.exe）
"$POST_BUILD_TOOLS_DIR/ufw_maker" -fw_to_ufw jl_isd.fw
cp jl_isd.ufw update.ufw
rm -f jl_isd.ufw

# 清理暫存檔（對應 bat 的 del）
rm -f tone.cfg p11_code.bin br28loader.bin ota.bin
