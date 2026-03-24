{
  description = "vkBasalt overlay — Vulkan post-processing layer with in-game UI (Wayland + X11)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    git-hooks = {
      url = "github:cachix/git-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      git-hooks,
    }:
    let
      supportedSystems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = self.packages.${system}.vkbasalt-overlay;

          vkbasalt-overlay = pkgs.stdenv.mkDerivation {
            pname = "vkbasalt-overlay";
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
              platforms = [ "x86_64-linux" ];
              mainProgram = "vkbasalt-run";
            };
          };

          vkbasalt-overlay-debug = self.packages.${system}.vkbasalt-overlay.overrideAttrs {
            mesonBuildType = "debug";
          };
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt);

      checks = forAllSystems (system: {
        pre-commit = git-hooks.lib.${system}.run {
          src = self;
          hooks = {
            nixfmt.enable = true;
          };
        };
      });

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inherit (self.checks.${system}.pre-commit) shellHook;
            buildInputs = self.checks.${system}.pre-commit.enabledPackages ++ [
              pkgs.nil
            ];
          };
        }
      );

      overlays.default = final: _prev: {
        inherit (self.packages.${final.stdenv.hostPlatform.system}) vkbasalt-overlay;
      };
    };
}
