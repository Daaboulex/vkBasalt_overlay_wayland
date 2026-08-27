#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
build_dir=${VKBASALT_BUILD_DIR:-"$project_root/build"}
layer_library="$build_dir/src/libvkbasalt-overlay.so"
initializer="$build_dir/headless-swapchain-init"
producer="$project_root/test/shared_texture_producer.fx"
consumer="$project_root/test/shared_texture_consumer.fx"

for required in "$layer_library" "$initializer" "$producer" "$consumer"; do
    if [[ ! -f "$required" ]]; then
        printf 'Missing transaction-test dependency: %s\n' "$required" >&2
        exit 1
    fi
done

test_root=$(mktemp -d /tmp/vkbasalt-transaction-test.XXXXXX)
cleanup() {
    rm -rf -- "$test_root"
}
trap cleanup EXIT

layer_dir="$test_root/layer"
config_dir="$test_root/config/vkBasalt-overlay"
mkdir -p -- "$layer_dir" "$config_dir"

cat >"$layer_dir/vkBasalt-transaction-test.json" <<EOF
{
  "file_format_version": "1.2.1",
  "layer": {
    "name": "VK_LAYER_VKBASALT_OVERLAY_post_processing",
    "type": "GLOBAL",
    "library_path": "$layer_library",
    "api_version": "1.3.223",
    "implementation_version": "1",
    "description": "vkBasalt transactional regression build",
    "functions": {
      "vkGetInstanceProcAddr": "vkBasalt_GetInstanceProcAddr",
      "vkGetDeviceProcAddr": "vkBasalt_GetDeviceProcAddr"
    }
  }
}
EOF

cat >"$config_dir/settings.conf" <<'EOF'
overlayBlockInput = false
maxEffects = 10
autoApply = false
enableOnLaunch = true
depthCapture = off
EOF
printf 'effects =\n' >"$config_dir/vkBasalt.conf"

driver_environment=()
if [[ -f /usr/share/vulkan/icd.d/lvp_icd.x86_64.json ]]; then
    driver_environment+=(VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json)
fi

run_probe() {
    local frames=$1
    local fail_stage=$2
    local effect_sequence=$3
    local stale_once=$4
    local allocation_index=${5:-0}
    local profile="$test_root/profile.conf"
    local output="$test_root/output.log"

    {
        printf 'SharedTextureProducer = "%s"\n' "$producer"
        printf 'SharedTextureConsumer = "%s"\n' "$consumer"
        printf 'effects = SharedTextureProducer:SharedTextureConsumer\n'
        if [[ -n "$effect_sequence" ]]; then
            printf 'disabledEffects = SharedTextureConsumer\n'
        fi
    } >"$profile"

    if ! env -u DISPLAY -u WAYLAND_DISPLAY \
        XDG_CONFIG_HOME="$test_root/config" \
        XDG_DATA_HOME="$test_root/data" \
        XDG_CACHE_HOME="$test_root/cache" \
        VK_ADD_LAYER_PATH="$layer_dir" \
        VK_INSTANCE_LAYERS=VK_LAYER_VKBASALT_OVERLAY_post_processing \
        ENABLE_VKBASALT=1 \
        DISABLE_VKBASALT=1 \
        VK_LOADER_LAYERS_DISABLE=VK_LAYER_VKBASALT_post_processing \
        VKBASALT_CONFIG_FILE="$profile" \
        VKBASALT_LOG_LEVEL=debug \
        VKBASALT_HEADLESS_FRAMES="$frames" \
        VKBASALT_TEST_TRANSACTION_RELOAD_EVERY=2 \
        VKBASALT_TEST_TRANSACTION_EFFECT_SEQUENCE="$effect_sequence" \
        VKBASALT_TEST_TRANSACTION_FAIL_STAGE="$fail_stage" \
        VKBASALT_TEST_TRANSACTION_ALLOCATION_FAIL_INDEX="$allocation_index" \
        VKBASALT_TEST_TRANSACTION_STALE_ONCE="$stale_once" \
        "${driver_environment[@]}" \
        "$initializer" >"$output" 2>&1; then
        cat "$output" >&2
        return 1
    fi

    grep -Fq "presented $frames headless frames through the effect chain" "$output"
    if [[ -n "$fail_stage" ]]; then
        grep -Fq "test fault injection at transaction stage: $fail_stage" "$output"
        [[ $(grep -Fc 'effect collection transaction aborted' "$output") -eq 1 ]]
        [[ $(grep -Fc 'effect collection transaction committed successfully' "$output") -eq 2 ]]
        grep -Fq 'previous generation remains live' "$output"
        grep -Fq 'last-good effect collection generation 1 remains active after failed rebuild' "$output"
        grep -Fq 'staged effect memory returned to the pre-transaction baseline' "$output"
        if [[ "$allocation_index" != 0 ]]; then
            grep -Fq "test fault injection will fail tracked staging allocation #$allocation_index" "$output"
            grep -Fq 'test fault injection returned VK_ERROR_OUT_OF_DEVICE_MEMORY before driver allocation' "$output"
        fi
    fi
    if [[ "$stale_once" == 1 ]]; then
        grep -Fq 'discarding stale effect collection generation' "$output"
        grep -Fq 'last-good effect collection generation 1 remains active after failed rebuild' "$output"
        grep -Fq 'staged effect memory returned to the pre-transaction baseline' "$output"
        grep -Fq 'restarting transaction from the newest desired state' "$output"
        [[ $(grep -Fc 'effect collection transaction committed successfully' "$output") -eq 2 ]]
    fi
}

for stage in \
    image-growth-oom \
    after-image-growth \
    after-effects \
    after-command-recording \
    after-fence-creation; do
    printf 'Testing rollback at %s\n' "$stage"
    run_probe 6 "$stage" \
        'SharedTextureProducer;SharedTextureProducer:SharedTextureConsumer' 0 0
done

for allocation_index in 1 2 3; do
    printf 'Testing staged allocation rollback at allocation #%s\n' "$allocation_index"
    run_probe 6 effect-allocation-oom \
        'SharedTextureProducer;SharedTextureProducer:SharedTextureConsumer' 0 "$allocation_index"
done

printf 'Testing stale-build rejection and retry\n'
run_probe 4 '' '' 1 0

printf 'Transaction fault matrix passed: every failed rebuild retained its last-good generation and later recovered.\n'
