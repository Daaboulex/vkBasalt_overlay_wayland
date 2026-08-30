project('vkBasalt-overlay', ['c', 'cpp'], default_options: ['c_std=c11', 'cpp_std=c++20'])

layer_api_major = 1
layer_api_minor = 3
layer_api_patch = 223
layer_api_version = '@0@.@1@.@2@'.format(layer_api_major, layer_api_minor, layer_api_patch)

cpp = meson.get_compiler('cpp')

library_arch = cpp.sizeof('void*') == 8 ? '64' : '32'

vkBasalt_include_path = ''

lib_dir = get_option('libdir')
ld_lib_dir_vkbasalt = ''

if get_option('append_libdir_vkbasalt')
    lib_dir = join_paths(lib_dir, 'vkbasalt-overlay')
    ld_lib_dir_vkbasalt = get_option('prefix') + '/$LIB/vkbasalt-overlay/'
endif

if get_option('with_so')
    subdir('src')

    test('shaders-compile-and-cache-round-trip', test_shaders_exe,
        args : [
            '--cache-verify',
            meson.project_source_root() / 'test' / 'language',
            meson.project_source_root() / 'test' / 'lut',
            meson.project_source_root() / 'test',
        ],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-xdg-cache'},
        timeout : 300)

    test('live-uniform-lowering', test_shaders_exe,
        args : [
            '--require-live-uniforms',
            '--cache-verify',
            meson.project_source_root() / 'test' / 'live_uniforms',
        ],
        env : {'XDG_CACHE_HOME' : meson.current_build_dir() / 'test-live-uniform-cache'},
        timeout : 60)

    input_map_test = executable('reshade-input-map-test',
        'test/reshade_input_map_test.cpp',
        include_directories : include_directories('src'))
    test('reshade-input-map', input_map_test)

    reshade_pass_utils_test = executable('reshade-pass-utils-test',
        'test/reshade_pass_utils_test.cpp',
        include_directories : include_directories('src'))
    test('reshade-pass-utils', reshade_pass_utils_test)

    reshade_texture_utils_test = executable('reshade-texture-utils-test',
        'test/reshade_texture_utils_test.cpp',
        include_directories : include_directories('src'))
    test('reshade-texture-utils', reshade_texture_utils_test)

    effect_timing_test = executable('effect-timing-test',
        'test/effect_timing_test.cpp',
        include_directories : include_directories('src'))
    test('effect-timing', effect_timing_test)

    reshade_uniform_storage_test = executable('reshade-uniform-storage-test',
        'test/reshade_uniform_storage_test.cpp',
        include_directories : include_directories('src'))
    test('reshade-uniform-storage', reshade_uniform_storage_test)

    live_uniform_policy_test = executable('live-uniform-policy-test',
        'test/live_uniform_policy_test.cpp',
        include_directories : include_directories('src'))
    test('live-uniform-policy', live_uniform_policy_test)

    effect_selection_test = executable('effect-selection-test',
        'test/effect_selection_test.cpp',
        include_directories : include_directories('src'))
    test('effect-selection', effect_selection_test)

    mouse_frame_test = executable('mouse-frame-snapshot-test',
        'test/mouse_frame_snapshot_test.cpp', 'src/mouse_input.cpp',
        include_directories : include_directories('src'),
        dependencies : [x11_dep, xi_dep])
    test('mouse-frame-snapshot', mouse_frame_test)

    summary({
        'wayland-client' : wayland_client_dep.version(),
        'pointer warp event handled' : cpp.has_header_symbol('wayland-client-protocol.h', 'WL_POINTER_WARP_SINCE_VERSION', dependencies : wayland_client_dep),
        'high-resolution scroll handled' : cpp.has_header_symbol('wayland-client-protocol.h', 'WL_POINTER_AXIS_VALUE120_SINCE_VERSION', dependencies : wayland_client_dep),
    }, section : 'Wayland', bool_yn : true)
endif

if get_option('with_json')
    subdir('config')
endif
