/*
 * A free chess perft verification tool, derived from WyldChess
 * Copyright (C) 2016  Manik Charan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <iostream>
#include <chrono>
#include <string>
#include <unistd.h>
#include "magicmoves.hpp"

#define INITIAL_POSITION (("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"))

#define BB(x)   ((1ULL << (x)))
#define MAX_PLY (128)

#define MOVE_TYPE_SHIFT (12)
#define PROM_TYPE_SHIFT (15)
#define CAP_TYPE_SHIFT  (18)

#define MOVE_TYPE_MASK (7 << MOVE_TYPE_SHIFT)
#define PROM_TYPE_MASK (7 << PROM_TYPE_SHIFT)
#define CAP_TYPE_MASK  (7 << CAP_TYPE_SHIFT)

typedef unsigned long long u64;

enum Colors {
	WHITE,
	BLACK
};

enum PieceTypes {
	PAWN = 2,
	KNIGHT,
	BISHOP,
	ROOK,
	QUEEN,
	KING,
	FULL
};

enum PieceWithColors {
	WP = 2, WN, WB, WR, WQ, WK,
	BP = 10, BN, BB, BR, BQ, BK
};

enum CastlingRights {
	WKC = 1,
	WQC = 2,
	BKC = 4,
	BQC = 8
};

enum Squares {
	A1, B1, C1, D1, E1, F1, G1, H1,
	A2, B2, C2, D2, E2, F2, G2, H2,
	A3, B3, C3, D3, E3, F3, G3, H3,
	A4, B4, C4, D4, E4, F4, G4, H4,
	A5, B5, C5, D5, E5, F5, G5, H5,
	A6, B6, C6, D6, E6, F6, G6, H6,
	A7, B7, C7, D7, E7, F7, G7, H7,
	A8, B8, C8, D8, E8, F8, G8, H8
};

enum MoveTypes {
	NORMAL,
	CASTLE       = 1 << MOVE_TYPE_SHIFT,
	ENPASSANT    = 2 << MOVE_TYPE_SHIFT,
	PROMOTION    = 3 << MOVE_TYPE_SHIFT,
	DOUBLE_PUSH  = 4 << MOVE_TYPE_SHIFT,
	CAPTURE      = 5 << MOVE_TYPE_SHIFT,
	PROM_CAPTURE = 6 << MOVE_TYPE_SHIFT
};

enum PromotionTypes {
	TO_KNIGHT = KNIGHT << PROM_TYPE_SHIFT,
	TO_BISHOP = BISHOP << PROM_TYPE_SHIFT,
	TO_ROOK   = ROOK   << PROM_TYPE_SHIFT,
	TO_QUEEN  = QUEEN  << PROM_TYPE_SHIFT
};

enum Files {
	FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H
};

enum Ranks {
	RANK_1,
	RANK_2,
	RANK_3,
	RANK_4,
	RANK_5,
	RANK_6,
	RANK_7,
	RANK_8
};

#define rank_of(sq) (sq >> 3)
#define file_of(sq) (sq & 7)

#define from_sq(m)   (m & 0x3f)
#define to_sq(m)     ((m >> 6) & 0x3f)
#define move_type(m) (m & MOVE_TYPE_MASK)
#define prom_type(m) ((m & PROM_TYPE_MASK) >> PROM_TYPE_SHIFT)
#define cap_type(m)  ((m & CAP_TYPE_MASK) >> CAP_TYPE_SHIFT)

#define popcnt(bb)  (__builtin_popcountll(bb))
#define bitscan(bb) (__builtin_ctzll(bb))

#define move_normal(from, to)              (from | (to << 6) | NORMAL)
#define move_cap(from, to, cap)            (from | (to << 6) | CAPTURE | (cap << CAP_TYPE_SHIFT))
#define move_double_push(from, to)         (from | (to << 6) | DOUBLE_PUSH)
#define move_castle(from, to)              (from | (to << 6) | CASTLE)
#define move_ep(from, to)                  (from | (to << 6) | ENPASSANT)
#define move_prom(from, to, prom)          (from | (to << 6) | PROMOTION | prom)
#define move_prom_cap(from, to, prom, cap) (from | (to << 6) | PROM_CAPTURE | prom | (cap << CAP_TYPE_SHIFT))

static int stm;

static u64 p_atks_bb[2][64];
static u64 n_atks_bb[64];
static u64 k_atks_bb[64];
static u64 b_pseudo_atks_bb[64];
static u64 r_pseudo_atks_bb[64];
static u64 q_pseudo_atks_bb[64];
static u64 intervening_sqs_bb[64][64];
static u64 dirn_sqs_bb[64][64];

static u64 rank_mask[8] = {
	0xffULL,
	0xffULL << 8,
	0xffULL << 16,
	0xffULL << 24,
	0xffULL << 32,
	0xffULL << 40,
	0xffULL << 48,
	0xffULL << 56
};

struct Movelist {
	int  moves[218];
	int* end;
};

struct State {
	u64 pinned_bb;
	u64 checkers_bb;
	u64 ep_sq;
	int move;
	int castling_rights;
};

struct Position {
	u64    bb[9];
	int    board[64];
	State* state;
	State  hist[MAX_PLY];
};

template<int c>
static inline void move_piece(Position* const pos, int const from, int const to, int const pt)
{
	u64 from_to       = BB(from) ^ BB(to);
	pos->bb[FULL]    ^= from_to;
	pos->bb[c]       ^= from_to;
	pos->bb[pt]      ^= from_to;
	pos->board[to]    = pos->board[from];
	pos->board[from]  = 0;
}

template<int c>
static inline void put_piece(Position* const pos, int const sq, int const pt)
{
	u64 set         = BB(sq);
	pos->bb[FULL]  |= set;
	pos->bb[c]     |= set;
	pos->bb[pt]    |= set;
	pos->board[sq]  = pt;
}

template<int c>
static inline void remove_piece(Position* const pos, int const sq, int const pt)
{
	u64 clr         = BB(sq);
	pos->bb[FULL]  ^= clr;
	pos->bb[c]     ^= clr;
	pos->bb[pt]    ^= clr;
	pos->board[sq]  = 0;
}

template<int pt>
static inline u64 get_atks(int const sq, u64 const occupancy)
{
	switch (pt) {
	case KNIGHT: return n_atks_bb[sq];
	case BISHOP: return Bmagic(sq, occupancy);
	case ROOK:   return Rmagic(sq, occupancy);
	case QUEEN:  return Qmagic(sq, occupancy);
	case KING:   return k_atks_bb[sq];
	default:     return -1;
	}
}

static inline u64 get_atks(int const sq, u64 const occupancy, int pt)
{
	switch (pt) {
	case KNIGHT: return n_atks_bb[sq];
	case BISHOP: return Bmagic(sq, occupancy);
	case ROOK:   return Rmagic(sq, occupancy);
	case QUEEN:  return Qmagic(sq, occupancy);
	case KING:   return k_atks_bb[sq];
	default:     return -1;
	}
}

template<int c>
static inline u64 pawn_shift(u64 const bb)
{
	return (c == WHITE ? bb << 8 : bb >> 8);
}

template<int c>
static inline u64 pawn_double_shift(u64 const bb)
{
	return (c == WHITE ? bb << 16 : bb >> 16);
}

static inline int file_diff(int const sq1, int const sq2)
{
	return std::abs((sq1 % 8) - (sq2 % 8));
}

void init_intervening_sqs()
{
	int i, j, high, low;
	for (i = 0; i < 64; i++) {
		for (j = 0; j < 64; j++) {
			intervening_sqs_bb[i][j] = 0ULL;
			if (i == j)
				continue;
			high = j;
			if (i > j) {
				high = i;
				low = j;
			}
			else
				low = i;
			if (file_of(high) == file_of(low)) {
				dirn_sqs_bb[i][j] = Rmagic(high, 0ULL) & Rmagic(low, 0ULL);
				for (high -= 8; high != low; high -= 8)
					intervening_sqs_bb[i][j] |= BB(high);
			}
			else if (rank_of(high) == rank_of(low)) {
				dirn_sqs_bb[i][j] = Rmagic(high, 0ULL) & Rmagic(low, 0ULL);
				for (--high; high != low; high--)
					intervening_sqs_bb[i][j] |= BB(high);
			}
			else if (rank_of(high) - rank_of(low) == file_of(high) - file_of(low)) {
				dirn_sqs_bb[i][j] = Bmagic(high, 0ULL) & Bmagic(low, 0ULL);
				for (high -= 9; high != low; high -= 9)
					intervening_sqs_bb[i][j] |= BB(high);
			}
			else if (rank_of(high) - rank_of(low) == file_of(low) - file_of(high)) {
				dirn_sqs_bb[i][j] = Bmagic(high, 0ULL) & Bmagic(low, 0ULL);
				for (high -= 7; high != low; high -= 7)
					intervening_sqs_bb[i][j] |= BB(high);
			}
		}
	}
}


void init_atks()
{
	static int king_offsets[8] = { -9, -8, -7, -1, 1, 7, 8, 9 };
	static int knight_offsets[8] = { -17, -15, -10, -6, 6, 10, 15, 17 };
	static int pawn_offsets[2][2] = { { 7, 9 }, { -9, -7 } };
	int off, ksq, nsq, psq, c, sq;

	for (sq = 0; sq != 64; ++sq) {
		k_atks_bb[sq] = 0ULL;
		n_atks_bb[sq] = 0ULL;
		p_atks_bb[WHITE][sq] = 0ULL;
		p_atks_bb[BLACK][sq] = 0ULL;

		for (off = 0; off != 8; ++off) {
			ksq = sq + king_offsets[off];
			if (   ksq <= H8
			    && ksq >= A1
			    && file_diff(sq, ksq) <= 1)
				k_atks_bb[sq] |= BB(ksq);

			nsq = sq + knight_offsets[off];
			if (   nsq <= H8
			    && nsq >= A1
			    && file_diff(sq, nsq) <= 2)
				n_atks_bb[sq] |= BB(nsq);
		}

		for (off = 0; off != 2; ++off) {
			for (c = 0; c != 2; ++c) {
				psq = sq + pawn_offsets[c][off];
				if (   psq <= H8
				    && psq >= A1
				    && file_diff(sq, psq) <= 1)
					p_atks_bb[c][sq] |= BB(psq);
			}
		}

		b_pseudo_atks_bb[sq] = Bmagic(sq, 0ULL);
		r_pseudo_atks_bb[sq] = Rmagic(sq, 0ULL);
		q_pseudo_atks_bb[sq] = Qmagic(sq, 0ULL);
	}
}

template<int c>
void undo_move(Position* const pos)
{
	--pos->state;

	int const m    = pos->state->move,
	          from = from_sq(m),
	          to   = to_sq(m),
	          mt   = move_type(m);

	switch (mt) {
	case NORMAL:
		{
			move_piece<c>(pos, to, from, pos->board[to]);
		}
		break;
	case CAPTURE:
		{
			move_piece<c>(pos, to, from, pos->board[to]);
			put_piece<!c>(pos, to, cap_type(m));
		}
		break;
	case DOUBLE_PUSH:
		{
			move_piece<c>(pos, to, from, PAWN);
		}
		break;
	case ENPASSANT:
		{
			put_piece<!c>(pos, (c == WHITE ? to - 8 : to + 8), PAWN);
			move_piece<c>(pos, to, from, PAWN);
		}
		break;
	case CASTLE:
		{
			move_piece<c>(pos, to, from, KING);

			switch(to) {
			case C1:
				move_piece<c>(pos, D1, A1, ROOK);
				break;
			case G1:
				move_piece<c>(pos, F1, H1, ROOK);
				break;
			case C8:
				move_piece<c>(pos, D8, A8, ROOK);
				break;
			case G8:
				move_piece<c>(pos, F8, H8, ROOK);
				break;
			default:
				break;
			}
		}
		break;
	case PROM_CAPTURE:
		{
			remove_piece<c>(pos, to, prom_type(m));
			put_piece<c>(pos, from, PAWN);
			put_piece<!c>(pos, to, cap_type(m));
		}
		break;
	default:
		{
			remove_piece<c>(pos, to, prom_type(m));
			put_piece<c>(pos, from, PAWN);
		}
		break;
	}
}

template<int c>
void do_move(Position* const pos, int const m)
{
	static int const castle_perms[64] = {
		13, 15, 15, 15, 12, 15, 15, 14,
		15, 15, 15, 15, 15, 15, 15, 15,
		15, 15, 15, 15, 15, 15, 15, 15,
		15, 15, 15, 15, 15, 15, 15, 15,
		15, 15, 15, 15, 15, 15, 15, 15,
		15, 15, 15, 15, 15, 15, 15, 15,
		15, 15, 15, 15, 15, 15, 15, 15,
		 7, 15, 15, 15, 3,  15, 15, 11
	};

	State* const curr = pos->state;
	State* const next = ++pos->state;

	curr->move     = m;
	next->ep_sq    = -1;

	int const from = from_sq(m),
	          to   = to_sq(m),
	          mt   = move_type(m);

	next->castling_rights =  (curr->castling_rights & castle_perms[from]) & castle_perms[to];

	switch (mt) {
	case NORMAL:
		{
			move_piece<c>(pos, from, to, pos->board[from]);
		}
		break;
	case CAPTURE:
		{
			remove_piece<!c>(pos, to, cap_type(m));
			move_piece<c>(pos, from, to, pos->board[from]);
		}
		break;
	case DOUBLE_PUSH:
		{
			move_piece<c>(pos, from, to, PAWN);
			next->ep_sq = (c == WHITE ? from + 8 : from - 8);
		}
		break;
	case ENPASSANT:
		{
			move_piece<c>(pos, from, to, PAWN);
			remove_piece<!c>(pos, (c == WHITE ? to - 8 : to + 8), PAWN);
		}
		break;
	case CASTLE:
		{
			move_piece<c>(pos, from, to, KING);

			switch (to) {
			case C1:
				move_piece<c>(pos, A1, D1, ROOK);
				break;
			case G1:
				move_piece<c>(pos, H1, F1, ROOK);
				break;
			case C8:
				move_piece<c>(pos, A8, D8, ROOK);
				break;
			case G8:
				move_piece<c>(pos, H8, F8, ROOK);
				break;
			default:
				break;
			}
		}
		break;
	case PROM_CAPTURE:
		{
			remove_piece<!c>(pos, to, cap_type(m));
			remove_piece<c>(pos, from, PAWN);
			put_piece<c>(pos, to, prom_type(m));
		}
		break;
	default:
		{
			remove_piece<c>(pos, from, PAWN);
			put_piece<c>(pos, to, prom_type(m));
		}
		break;
	}
}

static inline int get_piece_from_char(char c)
{
	switch (c) {
	case 'p': return BP;
	case 'r': return BR;
	case 'n': return BN;
	case 'b': return BB;
	case 'q': return BQ;
	case 'k': return BK;
	case 'P': return WP;
	case 'R': return WR;
	case 'N': return WN;
	case 'B': return WB;
	case 'Q': return WQ;
	case 'K': return WK;
	default : return -1;
	}
}

static inline int get_cr_from_char(char c)
{
	switch (c) {
	case 'K': return WKC;
	case 'Q': return WQC;
	case 'k': return BKC;
	case 'q': return BQC;
	default : return -1;
	}
}


static inline char get_char_from_piece(int const piece, int c)
{
	char x;
	int const pt = piece;
	switch (pt) {
	case PAWN:
		x = 'P';
		break;
	case KNIGHT:
		x = 'N';
		break;
	case BISHOP:
		x = 'B';
		break;
	case ROOK:
		x = 'R';
		break;
	case QUEEN:
		x = 'Q';
		break;
	case KING:
		x = 'K';
		break;
	default:
		return -1;
	}

	if (c == BLACK)
		x += 32;
	return x;
}


void init_pos(Position* const pos)
{
	int i;
	for (i = 0; i != 64; ++i)
		pos->board[i] = 0;
	for (i = 0; i != 9; ++i)
		pos->bb[i] = 0ULL;
	pos->state                  = pos->hist;
	pos->state->pinned_bb       = 0ULL;
	pos->state->castling_rights = 0;
	pos->state->ep_sq           = -1;
	pos->state->checkers_bb     = 0ULL;
}

int set_pos(Position* pos, std::string fen)
{
	init_pos(pos);
	int piece, pt, sq,
	    tsq   = 0,
	    index = 0;
	char c;
	while (tsq < 64) {
		sq = tsq ^ 56;
		c  = fen[index++];
		if (c == ' ') {
			break;
		} else if (c > '0' && c < '9') {
			tsq += (c - '0');
		} else if (c == '/') {
			continue;
		} else {
			piece = get_piece_from_char(c);
			pt = piece & 7;
			if ((piece >> 3) == WHITE)
				put_piece<WHITE>(pos, sq, pt);
			else
				put_piece<BLACK>(pos, sq, pt);
			++tsq;
		}
	}

	++index;
	stm = fen[index] == 'w' ? WHITE : BLACK;
	index   += 2;
	while ((c = fen[index++]) != ' ') {
		if (c == '-') {
			++index;
			break;
		} else {
			pos->state->castling_rights |= get_cr_from_char(c);
		}
	}
	int ep_sq = -1;
	if ((c = fen[index++]) != '-') {
		ep_sq = (c - 'a') + ((fen[index++] - '1') << 3);
		pos->state->ep_sq = ep_sq;
	}

	return index;
}

static int const is_prom_sq[64] = {
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1
};

template<int by_color>
static inline u64 atkers_to_sq(Position const * const pos, int const sq, u64 const occupancy)
{
	return (  ( pos->bb[KNIGHT]                   & n_atks_bb[sq])
		| ( pos->bb[PAWN]                     & p_atks_bb[!by_color][sq])
		| ((pos->bb[ROOK]   | pos->bb[QUEEN]) & Rmagic(sq, occupancy))
		| ((pos->bb[BISHOP] | pos->bb[QUEEN]) & Bmagic(sq, occupancy))
		| ( pos->bb[KING]                     & k_atks_bb[sq]))
		& pos->bb[by_color];
}

static inline void add_move(int const m, int** end)
{
	**end = m;
	++*end;
}

static void extract_moves(int const from, u64 atks_bb, int** end)
{
	int to;
	while (atks_bb) {
		to       = bitscan(atks_bb);
		atks_bb &= atks_bb - 1;
		add_move(move_normal(from, to), end);
	}
}

static void extract_caps(int board[64], int const from, u64 atks_bb, int** end)
{
	int to;
	while (atks_bb) {
		to       = bitscan(atks_bb);
		atks_bb &= atks_bb - 1;
		add_move(move_cap(from, to, board[to]), end);
	}
}

template<int c>
static void gen_check_blocks(Position* const pos, u64 blocking_poss_bb, int** end)
{
	u64 blockers_poss_bb, pawn_block_poss_bb;
	int blocking_sq, blocker;
	u64       pawns_bb       = pos->bb[PAWN] & pos->bb[c];
	u64 const inlcusion_mask = ~(pawns_bb | pos->bb[KING] | pos->state->pinned_bb),
	          full_bb        = pos->bb[FULL],
	          vacancy_mask   = ~full_bb;
	while (blocking_poss_bb) {
		blocking_sq         = bitscan(blocking_poss_bb);
		blocking_poss_bb   &= blocking_poss_bb - 1;
		pawn_block_poss_bb  = pawn_shift<!c>(BB(blocking_sq));
		if (pawn_block_poss_bb & pawns_bb) {
			blocker = bitscan(pawn_block_poss_bb);
			if (is_prom_sq[blocking_sq]) {
				add_move(move_prom(blocker, blocking_sq, TO_QUEEN), end);
				add_move(move_prom(blocker, blocking_sq, TO_KNIGHT), end);
				add_move(move_prom(blocker, blocking_sq, TO_ROOK), end);
				add_move(move_prom(blocker, blocking_sq, TO_BISHOP), end);
			} else {
				add_move(move_normal(blocker, blocking_sq), end);
			}
		} else if (((c == WHITE && rank_of(blocking_sq) == RANK_4)
			 || (c == BLACK && rank_of(blocking_sq) == RANK_5))
			   && (pawn_block_poss_bb & vacancy_mask)
			   && (pawn_block_poss_bb = pawn_shift<!c>(pawn_block_poss_bb) & pawns_bb)) {
			add_move(move_double_push(bitscan(pawn_block_poss_bb), blocking_sq), end);
		}
		blockers_poss_bb = atkers_to_sq<c>(pos, blocking_sq, full_bb) & inlcusion_mask;
		while (blockers_poss_bb) {
			add_move(move_normal(bitscan(blockers_poss_bb), blocking_sq), end);
			blockers_poss_bb &= blockers_poss_bb - 1;
		}
	}
}

template<int c>
static void gen_checker_caps(Position* pos, u64 checkers_bb, int** end)
{
	u64 atkers_bb;
	int checker, atker, checker_pt;
	u64 const pawns_bb      = pos->bb[PAWN] & pos->bb[c],
	          non_king_mask = ~pos->bb[KING],
		  full_bb       = pos->bb[FULL];
	int const ep_sq         = pos->state->ep_sq;
	if (    ep_sq != -1
	    && (pawn_shift<!c>(BB(ep_sq)) & checkers_bb)) {
		u64 ep_poss     = pawns_bb & p_atks_bb[!c][ep_sq];
		while (ep_poss) {
			atker    = bitscan(ep_poss);
			ep_poss &= ep_poss - 1;
			add_move(move_ep(atker, ep_sq), end);
		}
	}
	while (checkers_bb) {
		checker      = bitscan(checkers_bb);
		checker_pt   = pos->board[checker];
		checkers_bb &= checkers_bb - 1;
		atkers_bb    = atkers_to_sq<c>(pos, checker, full_bb) & non_king_mask;
		while (atkers_bb) {
			atker      = bitscan(atkers_bb);
			atkers_bb &= atkers_bb - 1;
			if (  (BB(atker) & pawns_bb)
			    && is_prom_sq[checker]) {
				add_move(move_prom_cap(atker, checker, TO_QUEEN, checker_pt), end);
				add_move(move_prom_cap(atker, checker, TO_KNIGHT, checker_pt), end);
				add_move(move_prom_cap(atker, checker, TO_ROOK, checker_pt), end);
				add_move(move_prom_cap(atker, checker, TO_BISHOP, checker_pt), end);
			} else {
				add_move(move_cap(atker, checker, checker_pt), end);
			}
		}
	}
}

template<int c>
static void gen_check_evasions(Position* pos, int** end)
{
	int const ksq = bitscan(pos->bb[KING] & pos->bb[c]);

	u64 checkers_bb = pos->state->checkers_bb,
	    evasions_bb = k_atks_bb[ksq] & ~pos->bb[c];

	u64 const full_bb      = pos->bb[FULL];
	u64 const sans_king_bb = full_bb ^ BB(ksq);

	int sq;
	while (evasions_bb) {
		sq = bitscan(evasions_bb);
		evasions_bb &= evasions_bb - 1;
		if (!atkers_to_sq<!c>(pos, sq, sans_king_bb)) {
			if (pos->board[sq])
				add_move(move_cap(ksq, sq, pos->board[sq]), end);
			else
				add_move(move_normal(ksq, sq), end);
		}
	}

	if (checkers_bb & (checkers_bb - 1))
		return;

	gen_checker_caps<c>(pos, checkers_bb, end);

	if (checkers_bb & k_atks_bb[ksq])
		return;

	u64 const blocking_poss_bb = intervening_sqs_bb[bitscan(checkers_bb)][ksq];
	if (blocking_poss_bb)
		gen_check_blocks<c>(pos, blocking_poss_bb, end);
}

template<int c>
static void gen_pawn_moves(Position* pos, int** end)
{
	int to, cap_pt, fr;
	u64 single_push, from, cap_candidates;
	u64 const vacancy_mask = ~pos->bb[FULL];
	u64       pawns_bb     = pos->bb[PAWN] & pos->bb[c];
	u64 prom_candidates_bb = pawns_bb & rank_mask[(c == WHITE ? RANK_7 : RANK_2)];
	pawns_bb              ^= prom_candidates_bb;
	if (pos->state->ep_sq != -1) {
		int const ep_sq   = pos->state->ep_sq;
		u64       ep_poss = pawns_bb & p_atks_bb[!c][ep_sq];
		while (ep_poss) {
			fr       = bitscan(ep_poss);
			ep_poss &= ep_poss - 1;
			add_move(move_ep(fr, ep_sq), end);
		}
	}
	while (prom_candidates_bb) {
		fr                  = bitscan(prom_candidates_bb);
		prom_candidates_bb &= prom_candidates_bb - 1;
		single_push         = pawn_shift<c>(BB(fr));
		cap_candidates      = p_atks_bb[c][fr] & pos->bb[!c];
		if (single_push & vacancy_mask) {
			to = bitscan(single_push);
			add_move(move_prom(fr, to, TO_QUEEN), end);
			add_move(move_prom(fr, to, TO_KNIGHT), end);
			add_move(move_prom(fr, to, TO_ROOK), end);
			add_move(move_prom(fr, to, TO_BISHOP), end);
		}
		while (cap_candidates) {
			to              = bitscan(cap_candidates);
			cap_candidates &= cap_candidates - 1;
			cap_pt          = pos->board[to];
			add_move(move_prom_cap(fr, to, TO_QUEEN, cap_pt), end);
			add_move(move_prom_cap(fr, to, TO_KNIGHT, cap_pt), end);
			add_move(move_prom_cap(fr, to, TO_ROOK, cap_pt), end);
			add_move(move_prom_cap(fr, to, TO_BISHOP, cap_pt), end);
		}
	}
	while (pawns_bb) {
		from        = pawns_bb & -pawns_bb;
		fr          = bitscan(pawns_bb);
		pawns_bb   &= pawns_bb - 1;
		single_push = pawn_shift<c>(from);
		cap_candidates = p_atks_bb[c][fr] & pos->bb[!c];
		if (single_push & vacancy_mask) {
			add_move(move_normal(bitscan(from), bitscan(single_push)), end);
			if (c == WHITE && (from & rank_mask[RANK_2])) {
				u64 const double_push = single_push << 8;
				if (double_push & vacancy_mask)
					add_move(move_double_push(bitscan(from), bitscan(double_push)), end);
			} else if (c == BLACK && (from & rank_mask[RANK_7])) {
				u64 const double_push = single_push >> 8;
				if (double_push & vacancy_mask)
					add_move(move_double_push(bitscan(from), bitscan(double_push)), end);
			}
		}
		while (cap_candidates) {
			to              = bitscan(cap_candidates);
			cap_pt          = pos->board[to];
			cap_candidates &= cap_candidates - 1;
			add_move(move_cap(fr, to, cap_pt), end);
		}
	}
}

template<int c>
static inline void gen_castling(Position* pos, int** end)
{
	static int const castling_poss[2][2] = {
		{ WKC, WQC },
		{ BKC, BQC }
	};
	static int const castling_intermediate_sqs[2][2][2] = {
		{ { F1, G1 }, { D1, C1 } },
		{ { F8, G8 }, { D8, C8 } }
	};
	static int const castling_king_sqs[2][2][2] = {
		{ { E1, G1 }, { E1, C1 } },
		{ { E8, G8 }, { E8, C8 } }
	};
	static u64 const castle_mask[2][2] = {
		{ (BB(F1) | BB(G1)), (BB(D1) | BB(C1) | BB(B1)) },
		{ (BB(F8) | BB(G8)), (BB(D8) | BB(C8) | BB(B8)) }
	};

	u64 const full_bb = pos->bb[FULL];

	if (    (castling_poss[c][0] & pos->state->castling_rights)
	    && !(castle_mask[c][0] & pos->bb[FULL])
	    && !(atkers_to_sq<!c>(pos, castling_intermediate_sqs[c][0][0], full_bb))
	    && !(atkers_to_sq<!c>(pos, castling_intermediate_sqs[c][0][1], full_bb)))
		add_move(move_castle(castling_king_sqs[c][0][0], castling_king_sqs[c][0][1]), end);

	if (    (castling_poss[c][1] & pos->state->castling_rights)
	    && !(castle_mask[c][1] & pos->bb[FULL])
	    && !(atkers_to_sq<!c>(pos, castling_intermediate_sqs[c][1][0], full_bb))
	    && !(atkers_to_sq<!c>(pos, castling_intermediate_sqs[c][1][1], full_bb)))
		add_move(move_castle(castling_king_sqs[c][1][0], castling_king_sqs[c][1][1]), end);
}

template<int pt, int c>
static void gen_moves(Position* pos, int** end)
{
	int from;
	u64 const full_bb      = pos->bb[FULL],
		  opp_mask     = pos->bb[!c],
	          vacancy_mask = ~full_bb;
	u64 curr_piece_bb = pos->bb[pt] & pos->bb[c];
	while (curr_piece_bb) {
		from           = bitscan(curr_piece_bb);
		curr_piece_bb &= curr_piece_bb - 1;
		extract_caps(pos->board, from, get_atks<pt>(from, full_bb) & opp_mask, end);
		extract_moves(from, get_atks<pt>(from, full_bb) & vacancy_mask, end);
	}
	gen_moves<pt+1, c>(pos, end);
}

template<>
inline void gen_moves<PAWN, WHITE>(Position* pos, int** end)
{
	gen_pawn_moves<WHITE>(pos, end);
	gen_moves<KNIGHT, WHITE>(pos, end);
}

template<>
inline void gen_moves<PAWN, BLACK>(Position* pos, int** end)
{
	gen_pawn_moves<BLACK>(pos, end);
	gen_moves<KNIGHT, BLACK>(pos, end);
}

template<>
inline void gen_moves<KING, WHITE>(Position* pos, int** end)
{
	int const from = bitscan(pos->bb[KING] & pos->bb[WHITE]);
	extract_moves(from, k_atks_bb[from] & ~pos->bb[FULL], end);
	extract_caps(pos->board, from, k_atks_bb[from] & pos->bb[BLACK], end);
	gen_castling<WHITE>(pos, end);
}

template<>
inline void gen_moves<KING, BLACK>(Position* pos, int** end)
{
	int const from = bitscan(pos->bb[KING] & pos->bb[BLACK]);
	extract_moves(from, k_atks_bb[from] & ~pos->bb[FULL], end);
	extract_caps(pos->board, from, k_atks_bb[from] & pos->bb[WHITE], end);
	gen_castling<BLACK>(pos, end);
}

template<int to_color>
static inline u64 get_pinned(Position* const pos)
{
	int const ksq = bitscan(pos->bb[KING] & pos->bb[to_color]);
	int sq;
	u64 bb;
	u64 pinned_bb  = 0ULL;
	u64 pinners_bb = ( (pos->bb[ROOK] | pos->bb[QUEEN])
			  & pos->bb[!to_color]
			  & r_pseudo_atks_bb[ksq])
		    | ( (pos->bb[BISHOP] | pos->bb[QUEEN])
		       & pos->bb[!to_color]
		       & b_pseudo_atks_bb[ksq]);
	while (pinners_bb) {
		sq          = bitscan(pinners_bb);
		pinners_bb &= pinners_bb - 1;
		bb          = intervening_sqs_bb[sq][ksq] & pos->bb[FULL];
		if(!(bb & (bb - 1)))
			pinned_bb ^= bb & pos->bb[to_color];
	}
	return pinned_bb;
}

template<int by_color>
static inline u64 get_checkers(Position const * const pos)
{
	const int sq = bitscan(pos->bb[KING] & pos->bb[!by_color]);
	return (  ( pos->bb[KNIGHT]                   & n_atks_bb[sq])
		| ( pos->bb[PAWN]                     & p_atks_bb[!by_color][sq])
		| ((pos->bb[ROOK]   | pos->bb[QUEEN]) & Rmagic(sq, pos->bb[FULL]))
		| ((pos->bb[BISHOP] | pos->bb[QUEEN]) & Bmagic(sq, pos->bb[FULL]))
		| ( pos->bb[KING]                     & k_atks_bb[sq]))
		& pos->bb[by_color];
}

// Idea from Stockfish 6
template<int c>
static inline int legal_move(Position* const pos, int move)
{
	int const from = from_sq(move);
	int const ksq  = bitscan(pos->bb[KING] & pos->bb[c]);
	if (move_type(move) == ENPASSANT) {
		u64 const to_bb  = BB(pos->state->ep_sq);
		u64 const pieces = (pos->bb[FULL] ^ BB(from) ^ pawn_shift<!c>(to_bb)) | to_bb;

		return !(Rmagic(ksq, pieces) & ((pos->bb[QUEEN] | pos->bb[ROOK]) & pos->bb[!c]))
		    && !(Bmagic(ksq, pieces) & ((pos->bb[QUEEN] | pos->bb[BISHOP]) & pos->bb[!c]));
	} else if (from == ksq) {
		return move_type(move) == CASTLE
		   || !atkers_to_sq<!c>(pos, to_sq(move), pos->bb[FULL]);
	} else {
		return !(pos->state->pinned_bb & BB(from))
		     || (BB(to_sq(move)) & dirn_sqs_bb[from][ksq]);
	}
}

static inline void move_str(int move, char str[6])
{
	int from = from_sq(move),
	    to   = to_sq(move);
	str[0]    = file_of(from) + 'a';
	str[1]    = rank_of(from) + '1';
	str[2]    = file_of(to)   + 'a';
	str[3]    = rank_of(to)   + '1';
	if (move_type(move) == PROMOTION) {
		const int prom = prom_type(move);
		switch (prom) {
		case QUEEN:
			str[4] = 'q';
			break;
		case KNIGHT:
			str[4] = 'n';
			break;
		case BISHOP:
			str[4] = 'b';
			break;
		case ROOK:
			str[4] = 'r';
			break;
		}
	}
	else {
		str[4] = '\0';
	}
	str[5] = '\0';
}


void print_board(Position* pos)
{
	int i, piece;
	for (i = 0; i != 64; ++i) {
		if (i && !(i & 7))
			printf("\n");
		piece = pos->board[i ^ 56];
		if (!piece)
			printf("- ");
		else
			printf("%c ", get_char_from_piece(piece, (BB((i ^ 56)) & pos->bb[WHITE] ? WHITE : BLACK)));
	}
	printf("\n");
}

u64 leaves     = 0ULL;
u64 captures   = 0ULL;
u64 enpassants = 0ULL;
u64 castles    = 0ULL;
u64 promotions = 0ULL;

template<int root, int c>
void perft(Position* pos, Movelist* list, int depth, bool count_extras, bool divide = false)
{
	list->end = list->moves;
	pos->state->pinned_bb = get_pinned<c>(pos);
	pos->state->checkers_bb = get_checkers<!c>(pos);
	if (pos->state->checkers_bb)
		gen_check_evasions<c>(pos, &list->end);
	else
		gen_moves<PAWN, c>(pos, &list->end);

	int* move;
	if (depth == 1) {
		for (move = list->moves; move < list->end; ++move) {
			if (!legal_move<c>(pos, *move)) continue;
			++leaves;
			if (count_extras) {
				captures += !!cap_type(*move);
				switch (move_type(*move)) {
				case ENPASSANT:
					++enpassants;
					++captures;
					break;
				case CASTLE:
					++castles;
					break;
				case PROMOTION:
					++promotions;
					break;
				default:
					break;
				}
			}
		}
	} else {
		char mstr[6];
		u64 divide_count;
		for (move = list->moves; move < list->end; ++move) {
			if (!legal_move<c>(pos, *move)) continue;
			if (root && divide)
				divide_count = leaves;
			do_move<c>(pos, *move);
			perft<0, !c>(pos, list + 1, depth - 1, count_extras);
			undo_move<c>(pos);
			if (root && divide) {
				divide_count = leaves - divide_count;
				move_str(*move, mstr);
				printf("%s: %'llu\n", mstr, divide_count);
			}
		}
	}
}

int main(int argc, char** argv)
{
	initmagicmoves();
	init_atks();
	init_intervening_sqs();

	bool count_extras = false;
	bool divide = false;
	bool fen_set = false;
	bool depth_set = false;
	std::string fen;
	int max_depth;

	int c;
	while ((c = getopt(argc, argv, "sed:f:")) != -1) {
		switch (c) {
		case 's':
			divide = true;
			break;
		case 'e':
			count_extras = true;
			break;
		case 'd':
			depth_set = true;
			max_depth = std::atoi(optarg);
			if (max_depth > MAX_PLY) {
				std::cout << "Max depth allowed: " << MAX_PLY << "\n";
				return 1;
			}
			break;
		case 'f':
			fen_set = true;
			fen = optarg;
			if (fen == "startpos")
				fen = INITIAL_POSITION;
			break;
		case '?':
			std::cout << "Unknown option: " << optopt << "\n";
			return 1;
		default:
			abort();
		}
	}

	if (!(fen_set && depth_set)) {
		std::cout << "Usage: ./perft(_popcnt) <options> -d <depth> -f \"<fen>\"" << "\n"
			  << "Options:\n"
			  << "-e => Count extras: captures, enpassants, castles and promotions\n"
			  << "-s => Split(Divide) at root\n"
			  << "-d <depth> => Max depth\n"
			  << "-f \"<fen>\" => Perft the fen\n"
			  << "To perft the initial position you may also use ./perft(_popcnt) -f startpos <flags>" << std::endl;
		return 1;
	}

	Position pos;
	set_pos(&pos, fen);

	print_board(&pos);

	Movelist* list = new Movelist[max_depth];
	long t1, t2;
	int depth;
	for (depth = 1; depth <= max_depth; ++depth) {
		leaves     = 0ULL;
		captures   = 0ULL;
		enpassants = 0ULL;
		castles    = 0ULL;
		promotions = 0ULL;
		t1 = std::chrono::duration_cast<std::chrono::milliseconds> (
			std::chrono::system_clock::now().time_since_epoch()
		).count();
		stm == WHITE
			? perft<1, WHITE>(&pos, list, depth, count_extras, divide)
			: perft<1, BLACK>(&pos, list, depth, count_extras, divide);
		t2 = std::chrono::duration_cast<std::chrono::milliseconds> (
			std::chrono::system_clock::now().time_since_epoch()
		).count();
		printf("Perft(%2d): %'ld ms\n", depth, (t2 - t1));
		printf("Leaves:     %'llu\n", leaves);
		if (count_extras) {
			printf("Captures:   %'llu\n", captures);
			printf("Enpassants: %'llu\n", enpassants);
			printf("Castles:    %'llu\n", castles);
			printf("Promotions: %'llu\n", promotions);
		}
	}
}
