#!/bin/bash
# Cross-compiles the private libavcodec/libavutil that only Firefox loads on the
# phone. FFmpeg n9.0.2 matches the phone's system FFmpeg (libavcodec.so.63).
set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

TAG=n9.0.2
[ -f ffmpeg-$TAG.tar.gz ] || curl -fsSL -o ffmpeg-$TAG.tar.gz \
	https://github.com/FFmpeg/FFmpeg/archive/refs/tags/$TAG.tar.gz

# Always start from clean source so the patch applies exactly once
rm -rf $TAG build
mkdir $TAG build
tar xzf ffmpeg-$TAG.tar.gz -C $TAG --strip-components=1
python3 -I ffpatch.py $TAG

cd build
../$TAG/configure --prefix=/opt/firefox-ffmpeg \
	--enable-cross-compile --arch=aarch64 --target-os=linux \
	--cross-prefix=aarch64-linux-gnu- --pkg-config=false \
	--disable-static --enable-shared --enable-pic \
	--disable-programs --disable-doc --disable-debug \
	--disable-autodetect --disable-everything \
	--disable-avformat --disable-avdevice --disable-avfilter \
	--disable-swscale --disable-swresample \
	--enable-v4l2-m2m \
	--enable-decoder=h264_v4l2m2m,hevc_v4l2m2m,vp8_v4l2m2m,vp9_v4l2m2m \
	--enable-decoder=h264,hevc,vp8,vp9,aac,aac_latm,mp3,mp3float,opus,vorbis,flac \
	--enable-parser=h264,hevc,vp8,vp9,aac,aac_latm,mpegaudio,opus,vorbis,flac \
	>/dev/null

grep -E 'define CONFIG_V4L2_M2M ' config.h
make -j"$(nproc)" >/dev/null

echo "--- built:"
ls -la libavcodec/libavcodec.so.63 libavutil/libavutil.so.61
echo "--- newest glibc needed (phone has 2.43):"
aarch64-linux-gnu-objdump -T libavcodec/libavcodec.so.63 libavutil/libavutil.so.61 |
	grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1
echo "BUILD OK"
