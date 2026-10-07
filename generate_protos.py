import os
import hashlib
import pathlib
import subprocess
import glob

Import("env")

project_dir = env.subst("$PROJECT_DIR")
build_dir = env.subst("$BUILD_DIR")
pioenv = env.subst("$PIOENV")
python_exe = env.subst("$PYTHONEXE")

# Verify required Python dependencies are available for Nanopb generator
missing_deps = []
try:
    import google.protobuf
except ImportError:
    missing_deps.append("protobuf>=3.19.1")

try:
    import grpc_tools.protoc
except ImportError:
    missing_deps.append("grpcio-tools>=1.43.0")

if missing_deps:
    print("\n" + "=" * 78)
    print("[generate_protos] ERROR: Missing Python dependencies required for Protobuf compilation:")
    for dep in missing_deps:
        print(f"  - {dep}")
    print("\nPlease install them in your Python environment by running:")
    print(f"  pip install {' '.join(missing_deps)}")
    print("or use the pre-configured VS Code / DevContainer environment.")
    print("=" * 78 + "\n")
    env.Exit(1)

proto_base_dir = os.path.join(project_dir, "proto", "meshtastic")
proto_src_dir = os.path.join(proto_base_dir, "meshtastic")
nanopb_generator = os.path.join(project_dir, ".pio", "libdeps", pioenv, "Nanopb", "generator", "nanopb_generator.py")
nanopb_lib_dir = os.path.join(project_dir, ".pio", "libdeps", pioenv, "Nanopb")

out_base_dir = os.path.join(build_dir, "nanopb")
out_meshtastic_dir = os.path.join(out_base_dir, "meshtastic")
md5_dir = os.path.join(build_dir, "nanopb_md5")

os.makedirs(out_meshtastic_dir, exist_ok=True)
os.makedirs(md5_dir, exist_ok=True)

# List of proto files in meshtastic directory
proto_files = sorted(glob.glob(os.path.join(proto_src_dir, "*.proto")))

if not os.path.exists(nanopb_generator):
    print(f"[generate_protos] Nanopb generator not found at {nanopb_generator}. Skipping for now (library will be fetched).")
elif not proto_files:
    print(f"[generate_protos] WARNING: No proto files found in {proto_src_dir}. Did you run git submodule update --init?")
else:
    rebuild_any = False
    for proto_path in proto_files:
        basename = os.path.basename(proto_path)
        stem = os.path.splitext(basename)[0]
        options_path = os.path.join(proto_src_dir, stem + ".options")

        # Compute hash of proto + options
        hasher = hashlib.md5()
        hasher.update(pathlib.Path(proto_path).read_bytes())
        if os.path.exists(options_path):
            hasher.update(pathlib.Path(options_path).read_bytes())
        current_md5 = hasher.hexdigest()

        md5_file = os.path.join(md5_dir, stem + ".md5")
        last_md5 = pathlib.Path(md5_file).read_text().strip() if os.path.exists(md5_file) else ""

        if current_md5 != last_md5:
            rebuild_any = True
            print(f"[generate_protos] Generating Nanopb C for {basename}...")
            cmd = [
                python_exe,
                nanopb_generator,
                f"-I{proto_base_dir}",
                f"-I{proto_src_dir}",
                f"-D{out_meshtastic_dir}",
                "-Q#include \"meshtastic/%s\"",
                "-S.cpp",
                "-T", # No timestamp to avoid spurious recompilations
                proto_path
            ]
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode != 0:
                print(f"[generate_protos] ERROR compiling {basename}:")
                print(res.stderr)
                env.Exit(1)
            pathlib.Path(md5_file).write_text(current_md5)

    if not rebuild_any:
        print("[generate_protos] All Meshtastic Protobufs up to date.")

# Include directories for headers:
# 1. out_base_dir so `#include "meshtastic/mesh.pb.h"` works
# 2. nanopb_lib_dir so `#include <pb.h>` works
env.Append(CPPPATH=[out_base_dir, nanopb_lib_dir])

# Register generated .c files for compilation
global_env = DefaultEnvironment()
already_called_env_name = "_MESHTASTIC_PROTO_BUILD_CALLED_" + pioenv.replace("-", "_")
if not global_env.get(already_called_env_name, False):
    env.BuildSources(os.path.join(build_dir, "nanopb_objs"), out_meshtastic_dir)
global_env[already_called_env_name] = True
