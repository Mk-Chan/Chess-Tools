all:
	g++ -std=c++11 -O3 *.cc -o perft
	g++ -std=c++11 -O3 -mpopcnt *.cc -o perft_popcnt
	x86_64-w64-mingw32-g++ -static -std=c++11 -O3 *.cc -o perft_win64.exe
	x86_64-w64-mingw32-g++ -static -std=c++11 -O3 -mpopcnt *.cc -o perft_popcnt_win64.exe
