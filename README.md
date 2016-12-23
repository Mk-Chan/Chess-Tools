# Chess-Tools
A collection of tools to help with chess programming.

## Current List
* perft

	A very fast perft tool derived from WyldChess(using Pradyumna Kannan's magic bitboards) that can be used to count leaf nodes, captures, enpassants, castles and promotions.
	It can also be used to calculate split(divide) counts from the root position.

* epd2uci

	A script that runs a chess engine(UCI only at the moment) for the given number of seconds over
	a file containing a list of EPDs and prints results. Requires the python-chess module.
