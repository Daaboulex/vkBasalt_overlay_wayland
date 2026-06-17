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
      url = "github:Daaboulex/nix-packaging-standard?ref=v2.5.0";
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
        {
          # Extend the standard's lint config for this fork: src/ vendors
          # third-party markdown (ReShade's LICENSE.md) that we do not relint,
          # and the README uses inline HTML (<details>/<img>) for screenshots.
          pre-commit.settings.hooks.rumdl = {
            excludes = [ "^src/" ];
            settings.configuration.MD033.enabled = false;
          };

          packages.vkbasalt-overlay = pkgs.stdenv.mkDerivation {
            pname = "vkbasalt-overlay";
            # This repo IS the source; the version tracks its own git revision.
            version = "0.1.0-unstable-${self.shortRev or "dirty"}";

            src = self;

            nativeBuildInputs = with pkgs; [
              meson
              ninja
              pkg-config
              glslang
              wayland-scanner
            ];

            buildInputs = with pkgs; [
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

            meta = with pkgs.lib; {
              description = "Vulkan post-processing layer with real-time overlay UI (Wayland + X11)";
              homepage = "https://github.com/Daaboulex/vkBasalt_overlay_wayland";
              license = licenses.zlib;
              platforms = pkgs.lib.platforms.linux;
              mainProgram = "vkbasalt-run";
            };
          };

          packages.vkbasalt-overlay-debug = self'.packages.vkbasalt-overlay.overrideAttrs {
            mesonBuildType = "debug";
          };

          packages.default = self'.packages.vkbasalt-overlay;
        };
    };
}
