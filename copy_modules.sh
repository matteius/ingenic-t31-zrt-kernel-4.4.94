#!/bin/bash

# Define source and target base paths
SOURCE_BASE_PATH="./drivers"
TARGET_BASE_PATH="/home/matteius/output/cinnado_d1_t31l_sc2336_pru/images/squashfs-root/usr/lib/modules/4.4.94"

# Replacement map: source -> target
declare -A REPLACEMENTS=(
  ["$SOURCE_BASE_PATH/net/wireless/atbm_60xx/hal_apollo/atbm603x_wifi_sdio.ko"]="$TARGET_BASE_PATH/extra/hal_apollo/atbm6031.ko"
  ["$SOURCE_BASE_PATH/video/ingenic_isp/t31/tx-isp-t31.ko"]="$TARGET_BASE_PATH/ingenic/tx-isp-t31.ko"
  ["$SOURCE_BASE_PATH/video/ingenic_sensors/t31/sc2336/sc2336.ko"]="$TARGET_BASE_PATH/ingenic/sensor_sc2336_t31.ko"
  ["$SOURCE_BASE_PATH/video/avpu/jz_avpu/avpu.ko"]="$TARGET_BASE_PATH/ingenic/avpu.ko"
  # Assuming sensor_info.ko needs to be added to a relevant directory, adjust as necessary.
  ["$SOURCE_BASE_PATH/video/ingenic_sinfo/sensor_info.ko"]="$TARGET_BASE_PATH/ingenic/"
)

# Copy and replace files
for src in "${!REPLACEMENTS[@]}"; do
  dst="${REPLACEMENTS[$src]}"
  echo "Copying $src to $dst"
  cp -v "$src" "$dst"
done

echo "All replacements and additions have been made."

