{
  description = "SSL vision_processor build environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          # Only what python/*.py needs at the system level. wrapper_backend/ is
          # uv-managed (see pyproject.toml) and brings its own venv.
          python = pkgs.python3.withPackages (ps: [
            ps.protobuf
            ps.pyyaml
            ps.opencv4          # python/cam_viewer.py
          ]);

          # pocl is a CPU OpenCL runtime. It makes the build environment
          # self-contained and testable without a GPU driver; a real GPU ICD on
          # the host is picked up instead when OCL_ICD_VENDORS points at it.
          openclRuntime = pkgs.pocl;
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
              protobuf          # provides protoc for both C++ and Python codegen
            ];

            buildInputs = with pkgs; [
              # Deliberately the nixpkgs default (3.4.x), matching what every
              # distro ships: Debian, Ubuntu through 26.04 LTS, and Fedora are
              # all still on 3.4. Pinning eigen_5 here would let code compile
              # that does not build for anyone packaging against a distro Eigen.
              eigen

              opencv            # core imgproc imgcodecs videoio
              yaml-cpp
              # Must match the ffmpeg nixpkgs built opencv against, currently
              # 8.1.2. The default `ffmpeg` is 9.x, which links fine but loads a
              # second set of libav* sonames alongside the ones opencv's videoio
              # pulls in -- two copies of ffmpeg's global state in one process.
              ffmpeg_8          # libavformat libavcodec libavutil
              protobuf

              opencl-headers    # CL/cl.h
              opencl-clhpp      # CL/opencl.hpp (CL_HPP_TARGET_OPENCL_VERSION=300)
              ocl-icd           # libOpenCL.so, the ICD loader
              openclRuntime

              python
              uv                # wrapper_backend/
            ];

            # The ICD loader finds runtimes through this. Without it the loader
            # reports zero platforms and vision_processor exits at startup.
            OCL_ICD_VENDORS = "${openclRuntime}/etc/OpenCL/vendors";

            # uv must not download its own interpreter inside the shell.
            UV_PYTHON = python.interpreter;
            UV_PYTHON_DOWNLOADS = "never";

            shellHook = ''
              echo "vision_processor dev shell"
              echo "  eigen      ${pkgs.eigen.version}"
              echo "  opencv     ${pkgs.opencv.version}"
              echo "  ffmpeg     ${pkgs.ffmpeg_8.version}"
              echo "  protobuf   ${pkgs.protobuf.version}"
              echo
              echo "  cmake -B build . && make -C build -j vision_processor"
              echo
              echo "Camera SDKs (Spinnaker, mvIMPACT) are proprietary and not"
              echo "packaged here; the OpenCV backend is available."
            '';
          };
        }
      );
    };
}
