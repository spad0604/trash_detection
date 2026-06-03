#!/usr/bin/env bash
set -euo pipefail

WS_DIR="${TRASH_WS_DIR:-/home/giap/trash_ws}"
CONFIG_FILE="${TRASH_CONFIG_FILE:-$WS_DIR/src/trash_sorting_ros/config/pipeline.yaml}"
PIXI_DIR="${TRASH_PIXI_DIR:-/home/giap/robot/Pi/ros2_env}"
PIXI_BIN="${PIXI_BIN:-/home/giap/.pixi/bin/pixi}"

export WS_DIR CONFIG_FILE

if [ -x "$PIXI_BIN" ] && [ -d "$PIXI_DIR" ]; then
    cd "$PIXI_DIR"
    exec "$PIXI_BIN" run bash -lc 'source "$WS_DIR/install/setup.bash"; exec ros2 launch trash_sorting_ros bringup.launch.py config:="$CONFIG_FILE"'
fi

if [ -f /opt/ros/humble/setup.bash ]; then
    source /opt/ros/humble/setup.bash
fi
source "$WS_DIR/install/setup.bash"

exec ros2 launch trash_sorting_ros bringup.launch.py config:="$CONFIG_FILE"
