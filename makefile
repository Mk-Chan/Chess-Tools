all:
	g++ -std=c++11 -O3 *.cc -o perft
	g++ -std=c++11 -O3 -mpopcnt *.cc -o perft_popcnt
