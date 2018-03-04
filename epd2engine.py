#!/bin/python3
"""
 A free tool to test UCI/Xboard engines over epd collections using the python-chess module
 Copyright (C) 2016-2018 Manik Charan

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

lib_folder = os.path.realpath(os.path.abspath(os.path.join(os.path.split(
    inspect.getfile(inspect.currentframe())
)[0], "libs/python-chess")))
if lib_folder not in sys.path:
    sys.path.insert(0, lib_folder)

import chess.uci
import chess.xboard

def print_help():
    print("Usage:")
    print("python3 epd2engine.py",
          "-e <engine-path>*",
          "-p <xboard/uci>*",
          "-f <epd-collection-file-path>*",
          "-t <seconds-per-position>*",
          "-E <egtb-path>", # FIXME: Currently only supports syzygy
          "-T <num-threads>",
          "-H <hash-size>")
    print("Options ending with * are necessary")


def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "f:e:t:p:E:T:H:")
    except:
        print_help()
        sys.exit(1)

    # Parse runtime params
    params_left = 4
    egtbpath = None
    threads = None
    hashsize = None
    for opt, arg in opts:
        # Engine path
        if opt == '-e':
            engine = arg
            params_left -= 1
        # Protocol
        elif opt == '-p':
            proto = arg
            params_left -= 1
        # EPD file
        elif opt == '-f':
            epd_file = arg
            params_left -= 1
        # Time per pos
        elif opt == '-t':
            time_per_pos = float(arg)
            params_left -= 1
        # EGTB
        elif opt == '-E':
            egtbpath = arg
        # Threads
        elif opt == '-T':
            threads = int(arg)
        # Hash
        elif opt == '-H':
            hashsize = int(arg)
        # Failure
        else:
            print_help()
            sys.exit(1)

    # Need 4 specific params at least:
    # engine, protocol, epd-file, time-per-pos
    if params_left > 0:
        print_help()
        sys.exit(1)

    board = chess.Board()

    # Initialize the engine
    if proto == "uci":
        engine = chess.uci.popen_engine(engine)
        engine.uci()

        # Attach an info handler
        info_handler = chess.uci.InfoHandler()
        engine.info_handlers.append(info_handler)

        # EGTB, threads and Hash options
        option_map = {}
        if egtbpath:
            option_map["SyzygyPath"] = egtbpath
        if threads:
            option_map["Threads"] = threads
        if hashsize:
            option_map["Hash"] = hashsize
        engine.setoption(option_map)
    elif proto == "xboard":
        engine = chess.xboard.popen_engine(engine)
        engine.xboard()

        # Attach a post handler
        post_handler = chess.xboard.PostHandler()
        engine.post_handlers.append(post_handler)

        # EGTB, threads and Hash options
        if egtbpath:
            engine.egtpath("syzygy", egtbpath)
        if threads:
            engine.cores(threads)
        if hashsize:
            engine.memory(hashsize)
    else:
        print("Unrecognized protocol!")
        sys.exit(1)


    def move(res):
        if proto == "uci":
            return res.bestmove
        else:
            return res

    successes = 0
    total = 0
    failed_epds = []
    epd_file = open(epd_file, 'r')
    for epd in epd_file:
        ops = board.set_epd(epd)
        total += 1

        # Find bestmoves (bms) or avoidmoves (ams)
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

        # Start thinking
        if proto == "uci":
            engine.position(board)
            res = engine.go(movetime=(time_per_pos * 1000))
        else:
            engine.force()
            engine.setboard(board)
            engine.st(time_per_pos)
            res = engine.go()

        # Check against am/bm
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

        # Record success/failure, pv and rate
        if fail:
            print("Failure")
            failed_epds.append(epd)
        else:
            print("Success")
            successes += 1
        if proto == "uci":
            pv = info_handler.info["pv"][1]
        else:
            pv = post_handler.post["pv"]
        pv = " ".join([move.uci() for move in pv])
        print("PV: {}".format(pv))
        print(
            "Success rate: {}% ({}/{})".format(
                round(((successes * 100.0) / total), 2), successes, total
            )
        )
        print()

    # Stop engine
    engine.quit()
    if engine.is_alive():
        engine.kill()

    # Write failures to failed.epd and end
    print("Success rate: " + str(successes * 100.0 / total) + "%")
    if total != successes:
        print("Failed: " + str(total - successes))
        print("Failed EPDs in failed.epd")

        fails_file = open("failed.epd", "w")
        for epd in failed_epds:
            fails_file.write(epd)

if __name__ == "__main__":
    main()
