{
  description = "vkBasalt overlay — Vulkan post-processing layer with in-game UI (Wayland + X11)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    git-hooks = {
      url = "github:cachix/git-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    std = {
      url = "github:Daaboulex/nix-packaging-standard?ref=v2.19.0";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.git-hooks.follows = "git-hooks";
    };
  };

  outputs =
    inputs@{ flake-parts, self, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      imports = [ inputs.std.flakeModules.base ];

      flake.overlays.default = final: _prev: {
        inherit (self.packages.${final.stdenv.hostPlatform.system}) vkbasalt-overlay;
      };

      perSystem =
        { pkgs, self', ... }:
        let
          mkVkbasaltOverlay =
            pkgs':
            pkgs'.stdenv.mkDerivation {
              pname = "vkbasalt-overlay";
              # This repo IS the source; the version tracks its own git revision.
              version = "0.1.0-unstable-${self.shortRev or "dirty"}";

              src = self;

              # Build tools run on the build host; buildPackages keeps them native
              # when pkgs' is the 32-bit set.
              nativeBuildInputs = with pkgs'.buildPackages; [
                meson
                ninja
                pkg-config
                glslang
                wayland-scanner
              ];

              buildInputs = with pkgs'; [
                vulkan-headers
                vulkan-loader
                spirv-headers
                libx11
                libxi
                wayland
                wayland-protocols
                libxkbcommon
              ];

              mesonBuildType = "release";

              mesonFlags = [
                "-Dappend_libdir_vkbasalt=false"
                "--sysconfdir=/etc"
              ];

              # Fix the layer JSON to use an absolute library path so the Vulkan
              # loader finds it regardless of LD_LIBRARY_PATH.
              postInstall = ''
                substituteInPlace "$out/share/vulkan/implicit_layer.d/vkBasalt-overlay.json" \
                  --replace-fail '"library_path": "libvkbasalt-overlay.so"' \
                                 '"library_path": "'"$out/lib/libvkbasalt-overlay.so"'"'

                # vkbasalt-run wrapper: sets ENABLE_VKBASALT and LD_AUDIT for Wine
                # Wayland input interposition (dlopen RTLD_LOCAL bypass).
                mkdir -p "$out/bin"
                substitute ${./vkbasalt-run.sh} "$out/bin/vkbasalt-run" \
                  --subst-var out
                chmod +x "$out/bin/vkbasalt-run"
              '';

              meta = with pkgs'.lib; {
                description = "Vulkan post-processing layer with real-time overlay UI (Wayland + X11)";
                homepage = "https://github.com/Daaboulex/vkBasalt_overlay_wayland";
                license = licenses.zlib;
                platforms = pkgs'.lib.platforms.linux;
                mainProgram = "vkbasalt-run";
              };
            };
        in
        {
          # Extend the standard's lint config for this fork: src/ vendors
          # third-party markdown (ReShade's LICENSE.md) that we do not relint,
          # and the README uses inline HTML (<details>/<img>) for screenshots.
          pre-commit.settings.hooks.rumdl = {
            excludes = [ "^src/" ];
            settings.configuration.MD033.enabled = false;
          };

          # The corpus baseline is generated and lists third-party shader names
          # verbatim; they are identifiers, not prose we can correct.
          pre-commit.settings.hooks.typos.excludes = [ "^test/shader-corpus-baseline\\.txt$" ];

          checks.settings-writer-owns-its-file = pkgs.runCommand "settings-writer-owns-its-file" { } ''
            src=${./src/config_serializer.cpp}
            writer=$(sed -n '/ConfigSerializer::saveSettings/,/^    }/p' "$src")
            if ! grep -q 'baseDir + "/settings.conf"' <<< "$writer"; then
              echo "saveSettings must write baseDir/settings.conf"
              exit 1
            fi
            if grep -q 'ofstream file(baseDir + "/vkBasalt.conf")\|configPath = baseDir + "/vkBasalt.conf"' <<< "$writer"; then
              echo "saveSettings targets vkBasalt.conf -- that truncates the user's effects config"
              exit 1
            fi
            touch $out
          '';

          checks.blocking-verifies-its-mechanism = pkgs.runCommand "blocking-verifies-its-mechanism" { } ''
            body=$(sed -n '/void setInputBlocked/,/^    }/p' ${./src/input_blocker.cpp})
            grep -q 'waylandInterposeActive()' <<< "$body" \
              || { echo "setInputBlocked must check waylandInterposeActive before claiming Wayland blocking"; exit 1; }
            grep -q 'if (!grabInput())' <<< "$body" \
              || { echo "setInputBlocked must check the X11 grab result before claiming blocking"; exit 1; }
            touch $out
          '';

          checks.pointer-constraints-are-wired = pkgs.runCommand "pointer-constraints-are-wired" { } ''
            grep -q 'confinePointer()' ${./src/overlay/imgui_overlay.cpp} \
              || { echo "confinePointer has no caller -- the constraints module is compiled but dead"; exit 1; }
            grep -q 'releasePointer()' ${./src/overlay/imgui_overlay.cpp} \
              || { echo "releasePointer has no caller -- confinement would never be lifted"; exit 1; }
            touch $out
          '';

          # The compiler emits compute entry points, so the renderer must either
          # dispatch them or refuse them. Doing neither renders nothing at all.
          checks.compute-pass-is-never-silently-ignored =
            pkgs.runCommand "compute-pass-is-never-silently-ignored" { }
              ''
                src=${./src/effects/effect_reshade.cpp}
                if ! grep -q 'CmdDispatch' "$src" && ! grep -q 'cs_entry_point' "$src"; then
                  echo "a compute pass is neither dispatched nor refused -- such an effect would silently render nothing"
                  exit 1
                fi
                touch $out
              '';

          # Compiles a shader exercising every compute feature and validates the
          # SPIR-V it produces, so a silently-wrong emission fails the build.
          # Each file here is a language feature that was once miscompiled or rejected. They are
          # small on purpose: when one fails, the feature it isolates is the one that broke.
          checks.language-features-compile =
            pkgs.runCommand "language-features-compile"
              { nativeBuildInputs = [ self'.packages.vkbasalt-overlay ]; }
              ''
                cp -r ${./test/language} shaders
                vkbasalt-test-shaders shaders > report.txt 2>&1 || true

                expected=$(ls shaders/*.fx | wc -l)
                actual=$(grep -c '^PASS  ' report.txt || true)
                if [ "$expected" != "$actual" ]; then
                  cat report.txt
                  echo "expected all $expected language feature shaders to compile, $actual did"
                  exit 1
                fi
                touch $out
              '';

          checks.same-pack-header-wins =
            pkgs.runCommand "same-pack-header-wins" { nativeBuildInputs = [ self'.packages.vkbasalt-overlay ]; }
              ''
                mkdir -p other/Shaders own/Shaders/inner
                printf '#define SHARED_VALUE 1\n' > other/Shaders/shared_header.fxh
                printf '#define SHARED_VALUE 2\n' > own/Shaders/shared_header.fxh

                cat > own/Shaders/inner/uses_shared.fx <<'EOF'
                #include "shared_header.fxh"

                #if SHARED_VALUE != 2
                    #error "resolved to another pack's copy of the header"
                #endif

                float4 SharedVS(uint id : SV_VertexID) : SV_Position
                {
                    return float4(0.0, 0.0, 0.0, 1.0);
                }

                float4 SharedPS(float4 pos : SV_Position) : SV_Target
                {
                    return float4(SHARED_VALUE, 0.0, 0.0, 1.0);
                }

                technique UsesShared
                {
                    pass
                    {
                        VertexShader = SharedVS;
                        PixelShader  = SharedPS;
                    }
                }
                EOF

                vkbasalt-test-shaders other/Shaders own/Shaders own/Shaders/inner > report.txt 2>&1 || true
                if ! grep -q '^PASS  uses_shared' report.txt; then
                  cat report.txt
                  echo "a header shipped by the shader's own pack must win over another pack's copy"
                  exit 1
                fi
                touch $out
              '';

          checks.compute-spirv-is-valid =
            pkgs.runCommand "compute-spirv-is-valid"
              {
                nativeBuildInputs = [
                  self'.packages.vkbasalt-overlay
                  pkgs.spirv-tools
                ];
              }
              ''
                mkdir -p shaders
                cp ${./test/compute_smoke.fx} shaders/compute_smoke.fx

                vkbasalt-test-shaders --dump-spirv "$PWD" shaders > report.txt 2>&1 || true
                grep -q '^PASS  compute_smoke' report.txt || { cat report.txt; echo "the compute smoke shader stopped compiling"; exit 1; }

                for spv in compute_smoke__*.spv; do
                  spirv-val --target-env vulkan1.1 "$spv" \
                    || { echo "the compute SPIR-V this compiler emits is invalid: $spv"; exit 1; }
                done

                cat compute_smoke__*.spv > /dev/null
                spirv-dis compute_smoke__*SmokeCS*.spv > dis.txt
                grep -q 'OpEntryPoint GLCompute' dis.txt \
                  || { echo "no GLCompute entry point -- the compute function was compiled as a graphics stage"; exit 1; }
                grep -qE 'OpExecutionMode .* LocalSize 8 8 1' dis.txt \
                  || { echo "the <8, 8> thread group size did not reach the LocalSize execution mode"; exit 1; }
                grep -q 'OpImageWrite' dis.txt \
                  || { echo "tex2Dstore did not become an image write"; exit 1; }
                grep -q 'OpControlBarrier' dis.txt \
                  || { echo "barrier() did not become a control barrier"; exit 1; }
                grep -q 'OpVariable %_ptr_Workgroup[A-Za-z0-9_]* Workgroup' dis.txt \
                  || { echo "groupshared is not Workgroup storage -- every invocation would get a private copy and barrier() would guard nothing"; exit 1; }
                touch $out
              '';

          # The warm-up only helps if it produces the same cache key as the real compile, and a
          # mismatch has no symptom -- it just silently stops helping.
          checks.compile-key-has-one-source = pkgs.runCommand "compile-key-has-one-source" { } ''
            effect=${./src/effects/effect_reshade.cpp}
            layer=${./src/basalt.cpp}

            grep -q 'reshadeCompileDefines(imageExtent' "$effect" \
              || { echo "the effect no longer asks reshadeCompileDefines for its defines"; exit 1; }
            grep -q 'BUFFER_RCP_WIDTH' <<< "$(sed -n '/ReshadeEffect::createReshadeModule/,$p' "$effect")" \
              && { echo "createReshadeModule builds the compile key itself again -- it can now drift from the warm-up"; exit 1; }
            grep -q 'reshadeCompileDefines(extent, unormFormat' "$layer" \
              || { echo "the warm-up no longer asks reshadeCompileDefines for its defines"; exit 1; }
            touch $out
          '';

          # The application holds the first imageCount fake images for the swapchain's life, so the
          # pool may only ever be appended to. Freeing or reordering it hands the application dead
          # handles, which is a use-after-free in the game's own present path.
          # Compiling under globalLock is what stalled every other intercepted call. The warm-up
          # exists to stop that, so a compile started from anywhere else in the layer, or from the
          # reload path without letting the lock go first, puts the stall straight back.
          # A depth image, its format and its view were three vectors indexed positionally, and a
          # view was only appended when the image being bound was the one created last. An
          # application creating images from several threads made them drift, after which the layer
          # paired an image with another image's view and destroyed the wrong one.
          # Draining the queue also waits out whatever the application has already submitted, which
          # measured at 45 percent of the wait. Waiting on the layer's own submission is the same
          # guarantee for the objects being freed, without the rest.
          checks.waits-are-scoped-to-our-own-work = pkgs.runCommand "waits-are-scoped-to-our-own-work" { } ''
            reload=$(sed -n '/void reloadEffectsForSwapchain/,/^        Logger::info("reloading/p' ${./src/basalt.cpp})
            grep -q 'WaitForFences' <<< "$reload" \
              || { echo "the reload drains the queue again instead of waiting on the layer's own passes"; exit 1; }

            grep -q 'QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, effectFence)' ${./src/basalt.cpp} \
              || { echo "the effect submission carries no fence, so nothing can wait on just that work"; exit 1; }

            grep -cE 'QueueWaitIdle' ${./src/image.cpp} | grep -qx 1 \
              || { echo "image.cpp should hold exactly one QueueWaitIdle, the fallback inside submitAndWait"; exit 1; }
            grep -q 'static void submitAndWait' ${./src/image.cpp} \
              || { echo "the one-shot uploads no longer share a single scoped wait"; exit 1; }
            touch $out
          '';

          checks.depth-tracking-cannot-drift = pkgs.runCommand "depth-tracking-cannot-drift" { } ''
            grep -qE 'depthFormats|depthImageViews' ${./src/logical_device.hpp} ${./src/basalt.cpp} \
              && { echo "depth tracking is back to parallel vectors -- they drift apart when images are created from several threads"; exit 1; }
            grep -q 'std::vector<DepthImage>  depthImages;' ${./src/logical_device.hpp} \
              || { echo "depth images are no longer one record each"; exit 1; }
            grep -q 'depthImage.image == image' ${./src/basalt.cpp} \
              || { echo "depth images are matched by position again rather than by handle"; exit 1; }
            touch $out
          '';

          checks.compiles-never-hold-the-lock = pkgs.runCommand "compiles-never-hold-the-lock" { } ''
            src=${./src/basalt.cpp}

            warm=$(sed -n '/void runEffectCompileWarmUp/,/^    }/p' "$src")
            grep -q 'getOrCompileReshadeEffect' <<< "$warm" \
              || { echo "runEffectCompileWarmUp no longer compiles anything"; exit 1; }

            outside=$(grep -c 'getOrCompileReshadeEffect' "$src")
            inside=$(grep -c 'getOrCompileReshadeEffect' <<< "$warm")
            [ "$outside" = "$inside" ] \
              || { echo "the layer compiles an effect outside runEffectCompileWarmUp -- that call may be holding globalLock"; exit 1; }

            grep -q 'l.unlock();' <<< "$(grep -B4 'runEffectCompileWarmUp(requests);' "$src")" \
              || { echo "the reload warms the cache without releasing globalLock first, which is the whole point of warming"; exit 1; }
            touch $out
          '';

          checks.effect-image-pool-only-appends = pkgs.runCommand "effect-image-pool-only-appends" { } ''
            body=$(sed -n '/bool growFakeSwapchainImages/,/^    }/p' ${./src/basalt.cpp})
            [ -n "$body" ] || { echo "growFakeSwapchainImages is gone -- the pool can no longer grow"; exit 1; }

            grep -qE 'FreeMemory|DestroyImage|fakeImages\.clear|fakeImages\.resize|fakeImages\.erase|fakeImages\.assign' <<< "$body" \
              && { echo "growing the effect image pool releases or reorders it -- the application is still holding those handles"; exit 1; }
            grep -q 'fakeImages.insert(pLogicalSwapchain->fakeImages.end()' <<< "$body" \
              || { echo "growth no longer appends at the end -- earlier slots would shift under the application"; exit 1; }
            grep -q 'settingsManager.getMaxEffects()' <<< "$body" \
              || { echo "growth is unbounded -- a runaway effect list could exhaust video memory"; exit 1; }
            touch $out
          '';

          checks.typed-characters-are-bounded = pkgs.runCommand "typed-characters-are-bounded" { } ''
            src=${./src/keyboard_input_wayland.cpp}
            grep -q 'TYPED_CHARS_LIMIT' "$src" \
              || { echo "the typed character accumulator has no bound -- it is drained only while the overlay is open, so it would grow for the life of the game"; exit 1; }
            grep -q 'typedCharsAccumulator.size() + len <= TYPED_CHARS_LIMIT' "$src" \
              || { echo "the bound is declared but the append does not check it"; exit 1; }
            touch $out
          '';

          checks.reshade-input-map =
            pkgs.runCommand "reshade-input-map" { nativeBuildInputs = [ pkgs.gcc ]; }
              ''
                mkdir -p src test
                cp ${./src/reshade_input_map.hpp} src/reshade_input_map.hpp
                cp ${./test/reshade_input_map_test.cpp} test/reshade_input_map_test.cpp
                g++ -std=c++20 -O1 -Wall -Werror -o runner test/reshade_input_map_test.cpp
                ./runner
                touch $out
              '';

          checks.reshade-version-is-truthful = pkgs.runCommand "reshade-version-is-truthful" { } ''
            env=${./src/reshade_fx_env.hpp}
            grep -q 'add_macro_definition("__RESHADE__", std::to_string(VKBASALT_RESHADE_FX_VERSION))' "$env" \
              || { echo "__RESHADE__ must come from VKBASALT_RESHADE_FX_VERSION, not a literal"; exit 1; }
            grep -q 'INT_MAX' <<< "$(sed -n '/add_macro_definition("__RESHADE__"/p' "$env")" \
              && { echo "__RESHADE__ claims INT_MAX -- every shader version gate opens and modern paths reach a 4.7 compiler"; exit 1; }
            grep -q 'define VKBASALT_RESHADE_FX_VERSION 40800' ${./src/reshade_fx_version.hpp} \
              || { echo "the declared FX level changed -- confirm src/reshade was resynced to match"; exit 1; }

            # The layer and the standalone tester must share one compile
            # environment, or the tester reports results the layer would not.
            for f in ${./src/shader_cache.cpp} ${./tools/test_shaders.cpp}; do
              grep -q 'addReshadeBaseMacros' "$f" \
                || { echo "$f does not use the shared compile environment -- its results would not match the layer"; exit 1; }
              grep -q 'add_macro_definition("__RESHADE__"' "$f" \
                && { echo "$f defines __RESHADE__ itself instead of sharing reshade_fx_env.hpp"; exit 1; }
            done
            touch $out
          '';

          packages = {
            vkbasalt-overlay = mkVkbasaltOverlay pkgs;

            vkbasalt-overlay-debug = self'.packages.vkbasalt-overlay.overrideAttrs {
              mesonBuildType = "debug";
            };

            default = self'.packages.vkbasalt-overlay;
          }
          # 32-bit layer for 32-bit games (hardware.graphics.extraPackages32);
          # the manifest's library_arch keeps the loader from mixing the two.
          // pkgs.lib.optionalAttrs (pkgs.stdenv.hostPlatform.system == "x86_64-linux") {
            vkbasalt-overlay-i686 = mkVkbasaltOverlay pkgs.pkgsi686Linux;
          };
        };
    };
}
