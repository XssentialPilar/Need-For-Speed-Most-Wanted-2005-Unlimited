#!/usr/bin/env bash
set -euo pipefail
# Invoke from the extracted official ffmpeg-6.1.6 source directory.
# GNU Make, Git Bash/MSYS2, and the LLVM-MinGW i686 tools must be on PATH.
./configure \
    --target-os=mingw32 --arch=x86 --cpu=i686 --enable-cross-compile \
    --cc=i686-w64-mingw32-clang --ar=llvm-ar --nm=llvm-nm --ranlib=llvm-ranlib \
    --windres=i686-w64-mingw32-windres --disable-x86asm \
    --disable-autodetect --disable-everything --disable-programs --disable-doc \
    --disable-network --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-postproc --disable-debug --enable-small --enable-shared --disable-static \
    --enable-swresample --enable-protocol=file \
    --enable-demuxer=flac,mp3,wav,ogg,mov,aac,aiff,ape,au,asf,matroska,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le \
    --enable-decoder=flac,mp3,mp3float,opus,vorbis,aac,alac,ape,wmav1,wmav2,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_u8,pcm_s8,pcm_f32le,pcm_f32be,pcm_f64le,pcm_f64be,pcm_alaw,pcm_mulaw,adpcm_ima_wav,adpcm_ms \
    --enable-parser=flac,mpegaudio,opus,aac,vorbis \
    --extra-cflags="-D_WIN32_WINNT=0x0601 -DWINVER=0x0601"
make -j8
