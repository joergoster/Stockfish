/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2022 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

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
                            : m.previousScore < previousScore;
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


/// A small stack for the PNS search

struct PnsStack {

  StateInfo st;
  int ply;
};


/// Node struct holds all the info needed, like the place
/// in the HashTable, proof and disproof numbers, etc.

struct Node {

  Node(std::vector<Node>::iterator pidx, Move m, bool exp, uint32_t pnr, uint32_t dnr) :
    parentIndex(pidx), move(m), isExpanded(exp), pn(pnr), dn(dnr) {
  }

  std::vector<Node>::iterator parent_id() const { return parentIndex; }

  uint32_t PN() const { return pn; }
  uint32_t DN() const { return dn; }

  Move action()      const { return move; }
  bool is_expanded() const { return isExpanded; }
  void mark_as_expanded() { isExpanded = true; }

  std::vector<Node>::iterator parentIndex;           // Index of the parent node TODO: move to the stack!
  Move move;                                         // Move which leads to this node
  bool isExpanded;                                   // True if all child nodes have been generated
  uint32_t pn;                                       // Proof number
  uint32_t dn;                                       // Disproof number
  std::vector<std::vector<Node>::iterator> children; // Holds the indices of all child nodes
};

using PnsHash = std::vector<Node>;


/// LimitsType struct stores information sent by GUI about available time to
/// search the current move, maximum depth/time, or if we are in analysis mode.

struct LimitsType {

  LimitsType() { // Init explicitly due to broken value-initialization of non POD in MSVC
    time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = npmsec = movetime = TimePoint(0);
    movestogo = depth = mate = perft = infinite = 0;
    nodes = 0;
  }

  bool use_time_management() const {
    return !(mate | movetime | depth | nodes | perft | infinite);
  }

  std::vector<Move> searchmoves;
  TimePoint time[COLOR_NB], inc[COLOR_NB], npmsec, movetime, startTime;
  int movestogo, depth, mate, perft, infinite;
  int64_t nodes;
};

extern LimitsType Limits;

void init(Position& pos);
void clear();

} // namespace Search

#endif // #ifndef SEARCH_H_INCLUDED
