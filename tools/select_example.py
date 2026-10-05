# PlatformIO pre-script: compiles the example named by `custom_example`
# (a folder under examples/) instead of the project src_dir.
# PlatformIO converts .ino files only at the top level of PROJECT_SRC_DIR,
# so each example env points PROJECT_SRC_DIR straight at its folder.
import os

Import("env")

example = env.GetProjectOption("custom_example", "").strip()
if example:
    example_dir = os.path.join(env.subst("$PROJECT_DIR"), "examples", example)
    if not os.path.isdir(example_dir):
        raise SystemExit("custom_example: folder not found: %s" % example_dir)
    env.Replace(PROJECT_SRC_DIR=example_dir)
