Build ffmpeg
1. Download Mingw at http://mingw.osdn.io/
2. Install mingw+msys
3. Download source ffmpeg https://ffmpeg.org/download.html#releases
4. Open msys and cd to source folder example: cd D:/FFMPEG/ffmpeg-4.4.tar/ffmpeg-4.4
5. Run: configure --help to view configure
6. Create folder ffmpeg and run: configure --prefix=ffmpeg/ --disable-network --disable-debug --disable-yasm
7. Run: make
8. Run: make install