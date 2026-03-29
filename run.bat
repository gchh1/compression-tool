mkdir build
cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
cd ..
bin\test_lz77.exe