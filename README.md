# Overview

This is the C code portion of the RetroClip web port. It handles encoding raw RGBA image data to h264 in an mpeg 4 container.

# Setup

```
mkdir x264
git clone https://github.com/mirror/x264
git clone https://github.com/l-smash/l-smash
git clone https://github.com/realartists/raw2mp4
```

# Building for macOS

```
mkdir build
export BUILD_DIR=`pwd`/build
export PKG_CONFIG_PATH=$BUILD_DIR/lib/pkgconfig
cd lsmash
./configure --prefix=$BUILD_DIR
make -j16
make install
cd ../x264
./configure --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-asm --disable-thread --disable-opencl --disable-interlaced --bit-depth=8 --enable-static --prefix=$BUILD_DIR
make -j16
make install
```

## Running for macOS

```
cd raw2mp4
```

Then, can use either `make raw2mp4` or the included Xcode project.

# Building for emscripten

## Setup emscripten
```
curl -O https://s3.amazonaws.com/mozilla-games/emscripten/releases/emsdk-portable.tar.gz
tar xzvf emsdk-portable.tar.gz 
cd emsdk-portable
./emsdk update
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh 
```

## Build with emscripten

### Apply this patch to l-smash:
```diff
diff --git a/configure b/configure
index de5faed..b49dabc 100755
--- a/configure
+++ b/configure
@@ -115,7 +115,7 @@ TOOLS=""
 
 CFLAGS="-Wshadow -Wall -std=c99 -pedantic -I. -I$SRCDIR"
 LDFLAGS="-L."
-SO_LDFLAGS='-shared -Wl,-soname,$@ -Wl,--version-script,liblsmash.ver'
+SO_LDFLAGS='-shared -Wl,-soname,$@ -Wl'
 LIBS="-lm"
 
 for opt; do
@@ -143,6 +143,12 @@ for opt; do
             CC="$optarg"
             LD="$optarg"
             ;;
+        --ar=*)
+            AR="$optarg"
+            ;;
+        --ranlib=*)
+            RANLIB="$optarg"
+            ;;
         --target-os=*)
             TARGET_OS="$optarg"
             ;;
@@ -211,7 +217,7 @@ case "$TARGET_OS" in
         SHARED_EXT=".dll"
         DEFNAME="${SHARED_NAME}.def"
         IMPLIB="liblsmash.dll.a"
-        SO_LDFLAGS="-shared -Wl,--output-def,$DEFNAME -Wl,--out-implib,$IMPLIB -Wl,--version-script,liblsmash.ver"
+        SO_LDFLAGS="-shared -Wl,--output-def,$DEFNAME -Wl,--out-implib,$IMPLIB -Wl"
         CFLAGS="$CFLAGS -D__USE_MINGW_ANSI_STDIO=1"
         LIBARCH=i386
         if lib.exe --list > /dev/null 2>&1 ; then
@@ -238,11 +244,11 @@ case "$TARGET_OS" in
         SHARED_NAME="cyglsmash"
         SHARED_EXT=".dll"
         IMPLIB="liblsmash.dll.a"
-        SO_LDFLAGS="-shared -Wl,--out-implib,$IMPLIB -Wl,--version-script,liblsmash.ver"
+        SO_LDFLAGS="-shared -Wl,--out-implib,$IMPLIB -Wl"
         ;;
     *darwin*)
         SHARED_EXT=".dylib"
-        SO_LDFLAGS="-dynamiclib -Wl,-undefined,suppress -Wl,-read_only_relocs,suppress -Wl,-flat_namespace -Wl,--version-script,liblsmash.ver"
+        SO_LDFLAGS="-dynamiclib -Wl,-undefined,suppress -Wl,-read_only_relocs,suppress -Wl,-flat_namespace -Wl"
         ;;
     *solaris*)
         #patches welcome
```

### Build libs

```
mkdir build_js
export BUILD_DIR=`pwd`/build_js
export PKG_CONFIG_PATH=$BUILD_DIR/lib/pkgconfig
cd l-smash
emconfigure ./configure --prefix=$BUILD_DIR --enable-shared --cc=emcc --ar=emar --ranlib=emranlib
emmake make
emmake make install
cd ../x264
emconfigure ./configure --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-asm --disable-thread --disable-opencl --disable-interlaced --bit-depth=8 --enable-static --prefix=$BUILD_DIR
emmake make -j16
emmake make install
```

### Build and run raw2mp4.node.js

```
cd raw2mp4
emmake make raw2mp4.node.js
```

Test run like so:

Setup test data:

```
make generate
mkdir testdata
./generate
```

Run raw2mp4.node.js:

```
cd testdata
node ../raw2mp4.node.js output.mp4 640 480 *.raw
open output.mp4
```

### Build raw2mp4.wasm

```
emmake make raw2mp4.js
```
