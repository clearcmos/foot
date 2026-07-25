# Meson fallback dependencies

The wrap files pin fallback dependencies to full commit revisions so builds
do not change when an upstream default branch advances.

To update a fallback:

1. Change its `revision` in the corresponding `.wrap` file.
2. Set up a clean build with
   `--force-fallback-for=fcft,tllist,wayland-protocols`.
3. Compile and run the full test suite with both GCC and Clang.

System dependencies that satisfy the version constraints remain preferred
for normal local builds.
