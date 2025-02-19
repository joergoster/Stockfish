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

#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <map>
#include <string>

#include "types.h"

using std::string;

class Position;

namespace UCI {

class Option;

/// Custom comparator because UCI options should be case insensitive
struct CaseInsensitiveLess {
  bool operator() (const string&, const string&) const;
};

/// Our options container is actually a std::map
typedef std::map<string, Option, CaseInsensitiveLess> OptionsMap;

/// Option class implements an option as defined by UCI protocol
class Option {

  typedef void (*OnChange)(const Option&);

public:
  Option(OnChange = nullptr);
  Option(bool v, OnChange = nullptr);
  Option(const char* v, OnChange = nullptr);
  Option(int v, int minv, int maxv, OnChange = nullptr);
  Option(const char* v, const char* cur, OnChange = nullptr);

  Option& operator=(const string&);
  void operator<<(const Option&);
  operator int() const;
  operator string() const;
  bool operator==(const char*) const;

private:
  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

  string defaultValue, currentValue, type;
  int min, max;
  size_t idx;
  OnChange on_change;
};

void init(OptionsMap&);
void loop(int argc, char* argv[]);
string value(Value v);
string square(Square s);
string move(Move m, bool chess960);
string pv(const Position& pos, Depth depth, size_t idx = 0);
Move to_move(const Position& pos, string& str);

void on_clear_hash(const Option&);
void on_hash_size(const Option&);
void on_logger(const Option&);
void on_threads(const Option&);
void on_tb_path(const Option&);

} // namespace UCI

extern UCI::OptionsMap Options;

#endif // #ifndef UCI_H_INCLUDED
