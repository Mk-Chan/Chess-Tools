"""
 A free tool to test UCI engines over epd collections using the python-chess module
 Copyright (C) 2016  Manik Charan

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
"""
import os
import sys
import getopt
import inspect

lib_folder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(inspect.getfile(inspect.currentframe()))[0],"libs/python-chess")))
if lib_folder not in sys.path:
    sys.path.insert(0, lib_folder)

import chess.uci

def print_help():
    print("Usage:")
    print("python3 epd2uci.py -e <engine> -f <epd-collection> -t <time-per-position>")

try:
    opts, args = getopt.getopt(sys.argv[1:], "f:e:t:")
except:
    print_help()
    sys.exit(1)

params_left = 3
for opt, arg in opts:
    if opt == '-e':
        engine = arg
        params_left -= 1
    elif opt == '-f':
        epd_file = arg
        params_left -= 1
    elif opt == '-t':
        time_per_pos = float(arg)
        params_left -= 1
    else:
        print_help()
        sys.exit(1)

if params_left > 0:
    print_help()
    sys.exit(1)

board = chess.Board()

epd_file = open(epd_file, 'r')
engine = chess.uci.popen_engine(engine)
engine.uci()

successes = 0
total = 0
failed_epds = []
for epd in epd_file:
    ops = board.set_epd(epd)
    total += 1

    print(ops.get("id"))
    bms = ops.get("bm")
    ams = ops.get("am")

    if bms != None:
        m_list = bms
    elif ams != None:
        m_list = ams

    ms = ""
    for m in m_list:
        ms += str(m) + " "

    if bms != None:
        print("Best move(s): " + ms[:-1])
    elif ams != None:
        print("Avoid move(s): " + ms[:-1])

    engine.position(board)
    res = engine.go(movetime=(time_per_pos * 1000))
    if bms != None:
        fail = True
        for bm in bms:
            if res.bestmove == bm:
                fail = False
                break
    elif ams != None:
        fail = False
        for am in ams:
            if res.bestmove == am:
                fail = True
                break
    if (fail):
        print("Failed")
        print("Engine move: " + str(res.bestmove))
        failed_epds.append(epd)
    else:
        print("Succeeded")
        print("Engine move: " + str(res.bestmove))
        successes += 1

    print("Current success rate: " + str(successes * 100.0 / total) + "%")
    print()
engine.quit()

print("Success rate: " + str(successes * 100.0 / total) + "%")
if total != successes:
    print("Failed: " + str(total - successes))
    print("Failed EPDs in failed.epd")

    fails_file = open("failed.epd", "w")
    for epd in failed_epds:
        fails_file.write(epd)
