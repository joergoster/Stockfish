/*
  Matefish, a UCI chess engine for solving mates derived from Stockfish.
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2025 The Stockfish developers (see AUTHORS file)
  Copyright (C) 2021-2025 Jörg Oster

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

#ifndef TT_H_INCLUDED
#define TT_H_INCLUDED

#include <cstring>   // For std::memcpy

#include "memory.h"
//#include "misc.h"
#include "types.h"

/// TTEntry struct is the 8 bytes transposition table entry, defined as below:
///
/// key        32 bit
/// move       16 bit
/// depth      16 bit

struct TTEntry {

  TTEntry() = default;
  TTEntry(TTEntry* tte) { std::memcpy(this, tte, sizeof(TTEntry)); }

  Move  move()  const { return Move(move16); }
  Depth depth() const { return Depth(depth16); }

private:
  friend class TranspositionTable;

  uint32_t key32;
  uint16_t move16;
  uint16_t depth16;
};


/// A TranspositionTable consists of a power of 2 number of clusters and each
/// cluster consists of ClusterSize number of TTEntry. Each non-empty entry
/// contains information of exactly one position. The size of a cluster should
/// divide the size of a cache line size, to ensure that clusters never cross
/// cache lines. This ensures best cache performance, as the cacheline is
/// prefetched as soon as possible.

class TranspositionTable {

  static constexpr int CacheLineSize = 64;
  static constexpr int ClusterSize = 4;

  struct Cluster {
    TTEntry entry[ClusterSize];
  };

  static_assert(CacheLineSize % sizeof(Cluster) == 0, "Cluster size incorrect");

public:
 ~TranspositionTable() { aligned_large_pages_free(table); }
  TTEntry* probe(const Key key, bool& found) const;
  void save(const Key key, Move m, Depth d) const;
  int hashfull() const;
  void resize(size_t mbSize);
  void clear();

  // Find the appropriate place in the Transposition Table and
  // point to the first slot. (Simply use the modulo operation.)
  TTEntry* first_entry(const Key key) const {
    return &table[key % clusterCount].entry[0];
  }

private:
  friend struct TTEntry;

  size_t clusterCount;
  Cluster* table = nullptr;
};

extern TranspositionTable TT;

#endif // #ifndef TT_H_INCLUDED
