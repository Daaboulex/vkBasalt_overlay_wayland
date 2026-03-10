{
  description = "vkBasalt overlay — Vulkan post-processing layer with in-game UI (Wayland + X11)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
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
            '';

            meta = with pkgs.lib; {
              description = "Vulkan post-processing layer with real-time overlay UI (Wayland + X11)";
              homepage = "https://github.com/Daaboulex/vkBasalt_overlay_wayland";
              license = licenses.zlib;
              platforms = [ "x86_64-linux" ];
              mainProgram = null;
            };
          };

          vkbasalt-overlay-debug = self.packages.${system}.vkbasalt-overlay.overrideAttrs {
            mesonBuildType = "debug";
          };
        }
      );

      overlays.default = final: _prev: {
        inherit (self.packages.${final.stdenv.hostPlatform.system}) vkbasalt-overlay;
      };
    };
}
