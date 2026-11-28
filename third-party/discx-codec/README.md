# Discx audio decoding

DISCX uses unmodified FFmpeg 6.1.6 libraries under the GNU LGPL 2.1 or later. Setup includes the corresponding source archive, build recipe, and LGPL text alongside the replaceable decoder DLL and its adapter source. The adapter links the four FFmpeg libraries into one x86 DLL. Windows loads it when the player opens the music library. [FFmpeg 6.1.6 source](https://ffmpeg.org/releases/ffmpeg-6.1.6.tar.xz)

Run the PowerShell build script in this directory with a Destination argument to reproduce the decoder and its source bundle. Use LLVM-MinGW and Git for Windows. The recipe enables local audio formats and disables network access, GPL components, and nonfree components.
