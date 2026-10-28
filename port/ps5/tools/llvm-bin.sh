#!/usr/bin/env bash
# SpeedBreaker PS5: a bin folder with Homebrew's llvm@18 tools and Homebrew's lld
# (llvm@18 has no ld.lld), and an llvm-config that names it as --bindir, for
# the payload SDK's prospero-* wrappers. Prints the llvm-config to export:
#
#   export LLVM_CONFIG=$(port/ps5/tools/llvm-bin.sh build/ps5-deps/llvm18)
set -euo pipefail
dir="$(mkdir -p "$1/bin" && cd "$1" && pwd)"
llvm="${LLVM18:-/opt/homebrew/opt/llvm@18}"
lld="${LLD:-/opt/homebrew/opt/lld}"
for f in "$llvm"/bin/*; do ln -sf "$f" "$dir/bin/"; done
for f in ld.lld lld; do ln -sf "$lld/bin/$f" "$dir/bin/$f"; done
rm -f "$dir/bin/llvm-config"
cat > "$dir/bin/llvm-config" <<SH
#!/bin/sh
if [ "\$1" = "--bindir" ]; then echo "$dir/bin"; exit 0; fi
exec "$llvm/bin/llvm-config" "\$@"
SH
chmod +x "$dir/bin/llvm-config"
echo "$dir/bin/llvm-config"
