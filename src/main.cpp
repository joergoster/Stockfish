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

#include <iostream>

#include "bitboard.h"
#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "uci.h"
#include "syzygy/tbprobe.h"


int main(int argc, char* argv[]) {

  UCI::init(Options);
  Bitboards::init();
  Position::init();
  Threads.set(Options["Threads"]);
  Search::clear(); // After threads are up

  // Announce to the GUI after setting up everything
  // and just before we start the UCI loop.
  std::cout << engine_info() << std::endl;

  UCI::loop(argc, argv);

  Threads.set(0);
  return 0;
}
