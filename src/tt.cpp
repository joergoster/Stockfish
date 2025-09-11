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

#include <cstring>   // For std::memset
#include <iostream>
#include <thread>

#include "bitboard.h"
#include "memory.h"
#include "misc.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

TranspositionTable TT; // Our global transposition table


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  Threads.main()->wait_for_search_finished();

  aligned_large_pages_free(table);

  clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);
  table = static_cast<Cluster*>(aligned_large_pages_alloc(clusterCount * sizeof(Cluster)));

  if (!table)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  clear();
}


/// TranspositionTable::clear() initializes the entire transposition table
/// to zero, in a multi-threaded way.

void TranspositionTable::clear() {

  std::vector<std::thread> threads;

  for (size_t idx = 0; idx < size_t(Options["Threads"]); ++idx)
  {
      threads.emplace_back([this, idx]() {

          // Thread binding gives faster search on systems with a first-touch policy
          if (Options["Threads"] > 8)
              WinProcGroup::bindThisThread(idx);

          // Each thread will zero its part of the hash table
          const size_t stride = clusterCount / Options["Threads"],
                       start  = stride * idx,
                       len    = idx != size_t(Options["Threads"]) - 1 ?
                                stride : clusterCount - start;

          std::memset(&table[start], 0, len * sizeof(Cluster));
      });
  }

  for (std::thread& th: threads)
      th.join();
}


/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found
/// and false and a pointer to the first slot otherwise.

TTEntry* TranspositionTable::probe(const Key key, bool& found) const {

  TTEntry* const tte = first_entry(key);
  const uint32_t key32 = key >> 32;

  for (int i = 0; i < ClusterSize; ++i)
  {
      if (tte[i].key32 == key32)
          return found = true, &tte[i];
  }

  return found = false, tte;
}


/// TranspositionTable::save() looks up the current position in the transposition
/// table and saves the new entry. Or it replaces an already existing entry
/// with one of greater depth.

void TranspositionTable::save(const Key key, Move m, Depth d) const {

  TTEntry* const tte = first_entry(key);
  const uint32_t key32 = key >> 32;

  for (int i = 0; i < ClusterSize; ++i)
  {
      if (!tte[i].key32) // Empty slot
      {
          tte[i].key32   = key32;
          tte[i].move16  = uint16_t(m);
          tte[i].depth16 = uint16_t(d);

          return;
      }

      if (tte[i].key32 == key32) // Already existing entry
      {
          tte[i].move16  = uint16_t(m);

          if (d > tte[i].depth16)
              tte[i].depth16 = uint16_t(d);

          return;
      }
  }

  // Find an entry to be replaced
  TTEntry* replace = tte;

  for (int i = 1; i < ClusterSize; ++i)
  {
      // Simply find the entry with the lowest depth
      if (  replace->depth16
          >   tte[i].depth16)
          replace = &tte[i];
  }

  replace->key32   = key32;
  replace->move16  = uint16_t(m);
  replace->depth16 = uint16_t(d);
}


/// TranspositionTable::hashfull() returns an approximation of the hashtable
/// occupation during a search. The hash is x permill full, as per UCI protocol.

int TranspositionTable::hashfull() const {

  int cnt = 0;

  for (int i = 0; i < 1000; ++i)
  {
      for (int j = 0; j < ClusterSize; ++j)
          cnt += table[i].entry[j].key32 != Key(0);
  }

  return cnt / ClusterSize;
}


/// TranspositionTable::size() returns the current size of the
/// hashtable in megabytes.

int TranspositionTable::size() const {

  return clusterCount * sizeof(Cluster) / (1024 * 1024);
}
