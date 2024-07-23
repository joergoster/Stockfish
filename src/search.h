/*
  Matefish, a UCI chess engine for solving mates derived from Stockfish.
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2024 The Stockfish developers (see AUTHORS file)
  Copyright (C) 2021-2024 Jörg Oster

  Matefish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Matefish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <cstring>
#include <vector>

#include "misc.h"
#include "position.h"
#include "types.h"

class Position;

namespace Search {

/// Stack struct keeps track of the information we need to remember from nodes
/// shallower and deeper in the tree during the search. Each search thread has
/// its own array of Stack objects, indexed by the current ply.

struct Stack {

  Stack() {
    pv.reserve(16);
    ply = 0;
  }

  std::vector<Move> pv;
  int ply;
};


struct RankedMove {

  RankedMove(Move m, int r) : move(m), rank(r) {}
  bool operator<(const RankedMove& rm) const {
    return rm.rank < rank;
  }

  Move move;
  int rank;
};


/// RootMove struct is used for moves at the root of the tree. For each root move
/// we store a score and a PV (really a refutation in the case of moves which
/// fail low). Score is normally set at -VALUE_INFINITE for all non-pv moves.

struct RootMove {

  explicit RootMove(Move m) : pv(1, m) {}
  bool operator==(const Move& m) const { return pv[0] == m; }
  bool operator<(const RootMove& m) const { // Sort in descending order
    return m.score != score ? m.score < score
                            : m.tbRank < tbRank;
  }

  Value score = VALUE_DRAW;
  Value previousScore = VALUE_DRAW;
  int selDepth = 0;
  int tbRank;
  int bestMoveCount = 0;
  Value tbScore;
  std::vector<Move> pv;
};

using RootMoves = std::vector<RootMove>;


/// Node struct holds all the info needed, like the place
/// in the HashTable, proof and disproof numbers, etc.

struct Node {

  void save(uint32_t proof, uint32_t disproof, Move m, Node* sibling, Node* child) {
    pn = proof;
    dn = disproof;
    move = m;
    nextSibling = sibling;
    firstChild = child;
  }

  uint32_t get_pn() const { return pn; }
  uint32_t get_dn() const { return dn; }
  Move action() const { return move; }

  Move move;         // Move which leads to this node
  uint32_t pn;       // Proof number
  uint32_t dn;       // Disproof number
  Node* nextSibling; // Pointer to the next sibling node
  Node* firstChild;  // Pointer to the first generated child node
};


/// A small stack for the proof-number search

struct PnsStack {

  PnsStack() {
    std::memset(&st, 0, sizeof(StateInfo));
    ply = 0;
    parentNode = nullptr;
    pv.reserve(16);
  }

  StateInfo st;
  int ply;
  Node* parentNode;
  std::vector<Move> pv;
};


/// LimitsType struct stores information sent by GUI about available time to
/// search the current move, maximum depth/nodes, or if we are in analysis mode.

struct LimitsType {

  LimitsType() { // Init explicitly due to broken value-initialization of non POD in MSVC
    lastOutputTime = movetime = startTime = TimePoint(0);
    depth = mate = perft = infinite = 0;
    nodes = 0;
  }

  TimePoint elapsed_time() const { return now() - startTime; }

  std::vector<Move> searchmoves;
  TimePoint lastOutputTime, movetime, startTime;
  int depth, mate, perft, infinite;
  int64_t nodes;
};

extern LimitsType Limits;

void init(Position& pos);
void clear();

} // namespace Search

#endif // #ifndef SEARCH_H_INCLUDED
