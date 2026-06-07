#!/bin/sh

set -eu

: "${ALTAIR_APPS_ROOT:=/opt/altair/Apps}"
: "${ALTAIR_ENV_FILE:=/opt/altair/runtime/altair_env.txt}"
: "${ALTAIR_DISKS_DIR:=/opt/altair/disks}"
: "${ALTAIR_DRIVE_A:=cpm63k.dsk}"
: "${ALTAIR_DRIVE_B:=bdsc-v1.60.dsk}"
: "${ALTAIR_DRIVE_C:=escape-posix.dsk}"
: "${ALTAIR_DRIVE_D:=blank.dsk}"

drive_a_path=${ALTAIR_DRIVE_A_PATH:-$ALTAIR_DISKS_DIR/$ALTAIR_DRIVE_A}
drive_b_path=${ALTAIR_DRIVE_B_PATH:-$ALTAIR_DISKS_DIR/$ALTAIR_DRIVE_B}
drive_c_path=${ALTAIR_DRIVE_C_PATH:-$ALTAIR_DISKS_DIR/$ALTAIR_DRIVE_C}
drive_d_path=${ALTAIR_DRIVE_D_PATH:-$ALTAIR_DISKS_DIR/$ALTAIR_DRIVE_D}

# Optional Raspberry Pi Sense HAT front panel. Enable by setting
# ALTAIR_SENSE_HAT=1 and starting the container with access to the I2C bus:
#   docker run -d --privileged --device=/dev/i2c-1 ... -e ALTAIR_SENSE_HAT=1 ...
# Requires a real Sense HAT; ignored gracefully if the hardware is absent.
sense_hat_arg=""
case "${ALTAIR_SENSE_HAT:-}" in
    1 | true | yes | on | TRUE | YES | ON)
        sense_hat_arg="--sense-hat"
        ;;
esac

# The environment file may hold secrets (API keys), so it is deliberately NOT
# baked into the image. Supply it at runtime by mounting a file at
# $ALTAIR_ENV_FILE, e.g.:
#   docker run -v "$PWD/altair_env.txt:/opt/altair/runtime/altair_env.txt:ro" ...
# When absent, the runner simply starts with an empty environment store.
env_file_arg=""
if [ -f "$ALTAIR_ENV_FILE" ]; then
    env_file_arg="--env-file $ALTAIR_ENV_FILE"
fi

# The container always serves the browser terminal (intended to run detached
# with `docker run -d` / `docker compose up -d`). Connect from a browser at
# http://<host>:${ALTAIR_WEB_PORT}/. ALTAIR_WEB_PORT selects the port (default
# 8080) and must match the published `-p` mapping.
exec altair-local \
    --apps-root "$ALTAIR_APPS_ROOT" \
    ${env_file_arg} \
    --drive-a "$drive_a_path" \
    --drive-b "$drive_b_path" \
    --drive-c "$drive_c_path" \
    --drive-d "$drive_d_path" \
    --web "${ALTAIR_WEB_PORT:-8080}" \
    ${sense_hat_arg} \
    "$@"
