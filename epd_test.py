#!/bin/python3
import sys
import time
import getopt
import pexpect

def get_piece_prefix(fen, sq_file, sq_rank):
    rank_skip = 8 - sq_rank
    relevant_rank = fen.split('/')[rank_skip]
    curr_file = 0
    index = 0
    while curr_file < sq_file:
        if relevant_rank[index].isnumeric():
            curr_file += int(relevant_rank[index])
        else:
            curr_file += 1
        index += 1

    if index >= len(relevant_rank):
        return None

    piece_prefix = relevant_rank[index]
    if not piece_prefix.isnumeric() and curr_file == sq_file:
        return piece_prefix.upper()
    else:
        return None

class Suite:
    id_list  = []
    fen_list = []
    moves    = []
    avoids   = []

castles = { "e1g1":"0-0", "e8g8":"0-0", "e1c1":"0-0-0", "e8c8":"0-0-0" }

try:
    opts, args = getopt.getopt(sys.argv[1:], "f:e:t:")
except getopt.GetoptError:
    print("Usage:")
    print("python3 epd_test.py -e <engine> -f <epd-collection> -t <time-per-position>")
    sys.exit(1)

suite = Suite()

for opt, arg in opts:
    if opt == '-e':
        engine = arg
    elif opt == '-f':
        epd_file = arg
    elif opt == '-t':
        time_per_pos = arg

f = open(epd_file, 'r')
p = pexpect.spawn(engine)
seconds_to_search = float(time_per_pos)

p.sendline("uci")

failed_epds = []

success_rate = 0.0
count = 0
tests = 0
for epd_line in f:
    best_move = None
    avoid_move = None

    components = epd_line.split()

    fen_end = 4
    if components[fen_end].isnumeric():
        fen_end = 6
    fen = ' '.join(components[:fen_end])
    suite.fen_list.append(fen)
    index = fen_end
    while index < len(components):
        opcode = components[index]
        if opcode == "id":
            index += 1

            curr_id = components[index][1:]
            while curr_id[-1] != ';':
                index += 1
                curr_id += ' ' + components[index]
            curr_id = curr_id[:-2]

            suite.id_list.append(curr_id)
        elif opcode == "bm":
            index += 1

            tmp = []
            tmp.append(components[index])
            while components[index][-1] != ';':
                index += 1
                tmp.append(components[index])
            tmp[-1] = tmp[-1][:-1]
            suite.moves.append(tmp)

            suite.avoids.append(False)
        elif opcode == "am":
            index += 1

            tmp = []
            tmp.append(components[index])
            while components[index][-1] != ';':
                index += 1
                tmp.append(components[index])
            tmp[-1] = tmp[-1][:-1]
            suite.moves.append(tmp)

            suite.avoids.append(True)
        index += 1

    tests += 1
    print("Testing " + curr_id)
    p.sendline("ucinewgame")
    p.sendline("position fen " + fen)
    p.sendline("go movetime " + str(seconds_to_search * 1000))
    time.sleep(seconds_to_search * 1.01)
    p.sendline("stop")

    # Look for a 'bestmove' line
    while 1:
        line = p.readline().decode()
        if line.startswith("bestmove"):
            # Found a bestmove line, extract the move and test
            move_str = line[9:line.find(' ', 10)].strip()

            print(suite.id_list[count])
            target_moves = suite.moves[count]
            avoid = suite.avoids[count]
            if avoid:
                print("Avoid moves: " + str(target_moves))
            else:
                print("Best moves: " + str(target_moves))
            print("Original engine move: " + move_str)

            ################################
            # Form a sufficient SAN string #
            ################################

            # Get the piece prefix
            from_file = ord(move_str[0]) - ord('a')
            from_rank = int(move_str[1])
            fen = suite.fen_list[count]
            piece_prefix = get_piece_prefix(fen, from_file, from_rank)

            # Target square is the enpassant square and moving piece is a pawn
            if move_str[2:] == fen[3] and piece_prefix == 'P':
                move_str_san = move_str[0] + "x" + fen[3]

            # Castling move
            elif piece_prefix == 'K' and (move_str in castles):
                move_str_san = castles[move_str]

            # Otherwise, it's a normal move, a capture with or without a promotion
            else:
                # Initialize a list which will contain each part of the SAN(Standard Algebraic Notation) string
                move_str_list = [ piece_prefix ]

                # Determine if move is a capture move
                to_file = ord(move_str[2]) - ord('a')
                to_rank = int(move_str[3])
                is_capture = (get_piece_prefix(fen, to_file, to_rank) != None)

                # Move is a capture
                if is_capture:
                    move_str_list.append("x")
                    # Moves a pawn and captures => Replace piece prefix with file name
                    if piece_prefix == "P":
                        move_str_list[0] = move_str[0]
                # Moves a pawn but doesn't capture => Remove piece prefix
                elif piece_prefix == "P":
                    move_str_list[0] = ""

                # Destination square
                move_str_list.append(move_str[2:4])

                # Promotion move
                if len(move_str) == 5:
                    move_str_list.append("=" + move_str[4].upper())

                # Accumulate each part into the SAN string
                move_str_san = ''.join(move_str_list)

            ###############################
            # Reduce the given SAN string #
            ###############################

            for index, target_move in enumerate(target_moves):
                target_move = target_move.strip()

                # Strip given move of check/checkmate symbol
                if target_move[-1] in [ '+', '#' ]:
                    target_move = target_move[:-1]
                # In case of old checkmate notation (example: Qxf7++)
                if target_move[-1] == '+':
                    target_move = target_move[:-1]
                target_moves[index] = target_move

            #######################
            # Compare the strings #
            #######################

            match = False
            move_str_san = move_str_san.strip()
            for target_move in target_moves:
                print("Checking " + target_move)
                target_move = target_move.strip()

                # All pawn moves, all non-explicit file/rank piece moves and all castles
                # Compare directly
                if target_move == move_str_san:
                    match = True
                    break
                # Exclude the above,
                # Check for existence and correctness of Explicit file/rank moves
                elif        (len(target_move) > 3 and target_move[1] != 'x')                           \
                        and (target_move[2] == 'x' or len(target_move) == 4)                           \
                        and (target_move[1] in move_str[:2]):
                    # Strip file/rank specifier
                    target_move = target_move[0] + target_move[2:]
                    # Compare directly
                    if target_move == move_str_san:
                        match = True
                        break

            # Success statistics
            success = avoid ^ match
            if success:
                print("Success!")
                success_rate += 1.0
            else:
                print("Failure!")
                failed_epds.append(epd_line)

            print("Engine move: " + move_str_san)
            print("Current rate: " + str(success_rate * 100 / tests) + "%")
            count += 1
            print()
            break
p.sendline("quit")
success_rate *= 100 / tests
print("Success rate: " + str(success_rate) + "%")
print("\nFailures:")
for fail in failed_epds:
    print(fail.strip())
