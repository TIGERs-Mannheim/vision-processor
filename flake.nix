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
              # Eigen 5 is required: CameraModel::getEuler() calls
              # Matrix::canonicalEulerAngles(), which does not exist in 3.4.x.
              # nixpkgs' default `eigen` is still 3.4.1, hence the explicit pin.
              eigen_5

              opencv            # core imgproc imgcodecs videoio
              yaml-cpp
              ffmpeg            # libavformat libavcodec libavutil
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
              echo "  eigen      ${pkgs.eigen_5.version}"
              echo "  opencv     ${pkgs.opencv.version}"
              echo "  protobuf   ${pkgs.protobuf.version}"
              echo
              echo "  cmake -B build . && make -C build -j vision_processor"
              echo
              echo "Camera SDKs (Spinnaker, mvIMPACT) are proprietary and not"
              echo "packaged here; the OpenCV backend is available regardless."
            '';
          };
        }
      );
    };
}
