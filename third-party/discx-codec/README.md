# Discx audio decoding

EAX Discx uses unmodified FFmpeg 6.1.6 libraries under the GNU LGPL 2.1 or later. Setup includes the corresponding source archive, build recipe, and LGPL text alongside the replaceable codec DLLs. FFmpeg runs inside the game through a private x86 adapter. [FFmpeg 6.1.6 source](https://ffmpeg.org/releases/ffmpeg-6.1.6.tar.xz)

Run the PowerShell build script in this directory with a Destination argument to reproduce the decoder and its source bundle. Use LLVM-MinGW and Git for Windows. The recipe enables local audio formats and disables network access, GPL components, and nonfree components.
