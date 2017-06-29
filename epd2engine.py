#!/bin/python3
"""
 A free tool to test UCI/Xboard engines over epd collections using the python-chess module
 Copyright (C) 2016-2017 Manik Charan

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
import chess.xboard

def move(res):
    global proto
    if proto == "uci":
        return res.bestmove
    else:
        return res

def print_help():
    print("Usage:")
    print("python3 epd2uci.py -e <engine> -p <xboard/uci> -f <epd-collection> -t <seconds-per-position>")

try:
    opts, args = getopt.getopt(sys.argv[1:], "f:e:t:p:")
except:
    print_help()
    sys.exit(1)

params_left = 4
for opt, arg in opts:
    if opt == '-e':
        engine = arg
        params_left -= 1
    elif opt == '-p':
        proto = arg
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
if proto == "uci":
    engine = chess.uci.popen_engine(engine)
    engine.uci()
elif proto == "xboard":
    engine = chess.xboard.popen_engine(engine)
    engine.xboard()
else:
    print("Unrecognized protocol!")
    sys.exit(1)

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

    if proto == "uci":
        engine.position(board)
        res = engine.go(movetime=(time_per_pos * 1000))
    else:
        engine.setboard(board)
        engine.st(time_per_pos)
        res = engine.go()
    if bms != None:
        fail = True
        for bm in bms:
            if move(res) == bm:
                fail = False
                break
    elif ams != None:
        fail = False
        for am in ams:
            if move(res) == am:
                fail = True
                break
    if (fail):
        print("Failed")
        failed_epds.append(epd)
    else:
        print("Succeeded")
        successes += 1
    print("Engine move: " + str(move(res)))
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
