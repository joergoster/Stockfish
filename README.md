# Matefish
Matefish is a mate-solving program for standard checkmate problems.
It is a UCI chess engine based on Stockfish 11, yet the search has completely been rewritten
for the purpose to solve checkmate problems as fast as possible.
It's still far from the functionality and speed offered by ChestUCI or Gustav, though.

It is mainly inspired by the publication REALIZATION OF THE CHESS MATE SOLVER APPLICATION
http://www.doiserbia.nb.rs/img/doi/0354-0243/2004/0354-02430402273V.pdf
and discussions on Talkchess and the german CSS forum.

# Usage
Matefish makes use of some prunings and extensions which are only safe if you specify the exact mate distance you are looking for. Starting an infinite analysis makes no sense.
In a GUI, start a search with a fixed depth or mate limit. However, not all GUIs may support one or even both search limits. On a command-line you can either use 'go mate x' or 'go depth x', where x is the number of moves to the mate.
Typically, this info is provided with the puzzle.

# Options
**Threads** - Sets the number of threads to be used by the Alpha-Beta Search.

**Hash** - A dummy option for the Fritz GUI (has no effect).

**PNS Hash** - Memory to be used by the Proof-Number Search. Max. 1 GB for now.

**KingMoves** - Number of moves (0 - 8) allowed for the opponent king.

**AllMoves** - Number of all moves allowed for the opponent.

**ProofNumberSearch** - Switch to Proof-Number search (only single-threaded).

**RootMoveStats** - Prints some info about the scored and ranked root moves, mainly a debug option.

**SyzygyPath** - Path to the Syzygy Endgame Tablebases, used by both search methods.

# Examples Alpha-Beta Search
```
position fen 8/8/8/8/2Np4/3N4/k1K5/8 w - -
go mate 4
info string Starting Alpha-Beta Search ...
info time 2 multipv 1 depth 1 seldepth 1 nodes 2 nps 1000 tbhits 0 score cp 0 pv d3c1
info time 2 multipv 1 depth 3 seldepth 7 nodes 68 nps 34000 tbhits 0 score cp 0 pv d3c1
info time 3 multipv 1 depth 5 seldepth 7 nodes 2871 nps 957000 tbhits 0 score cp 0 pv d3c1
info string Success! Mate in 4 found!
info time 4 multipv 1 depth 7 seldepth 7 nodes 3342 nps 835500 tbhits 0 score mate 4 pv d3b4 a2a1 c4a3 d4d3 c2b3 d3d2 b4c2
bestmove d3b4 ponder a2a1

position fen 6r1/p1pq1p1p/1p1p1Qnk/3PrR2/2n1P1PP/P1P5/4R3/6K1 w - -
go depth 11
info string Starting Alpha-Beta Search ...
info time 1 multipv 1 depth 1 seldepth 1 nodes 5 nps 5000 tbhits 0 score cp 0 pv f6g6
info string Success! Mate in 11 found!
info time 3 multipv 1 depth 3 seldepth 21 nodes 2637 nps 879000 tbhits 0 score mate 11 pv f5h5 e5h5 g4g5 h5g5 h4g5 h6h5 e2h2 g6h4 f6h6 h5g4 h6h4 g4f3 h2f2 f3e3 h4g3 e3e4 g3f3 e4e5 f2e2 c4e3 e2e3
bestmove f5h5 ponder e5h5
```

The second example also demonstrates that now 'go depth x' and 'go mate x' commands are handled the same way!
It also shows that checks get always extended so that forced checkmates will be found noticeably faster!

As a special feature, Matefish is able to construct a mating sequence with the help of the Syzygy endgame bases.
This works for very basic endings only. (Specifically KQK, KRK, KBBK, KBNK and KNNNK.)

```
position fen 4k3/8/8/8/8/8/8/2B1K1N1 w - - 0 1
go
info depth 57 seldepth 57 multipv 1 score mate 29 nodes 56 nps 1806 tbhits 651 time 31 pv e1d2 e8d7 d2c3 d7c6 c3c4 c6d7 c1f4 d7c6 g1e2 c6b7 c4b5 b7a7 e2c3 a7a8 c3a4 a8a7 a4b6 a7b7 f4h2 b7a7 b5c6 a7a6 h2b8 a6a5 b6d5 a5a4 b8e5 a4b3 d5e3 b3b4 c6b6 b4a4 e5c3 a4b3 c3e1 b3a4 e1d2 a4b3 b6a5 b3a3 d2e1 a3b2 a5a4 b2a2 e3d1 a2a1 a4b3 a1b1 e1d2 b1a1 d2c1 a1b1 c1a3 b1a1 a3b2 a1b1 d1c3
bestmove e1d2 ponder e8d7

```

Additionally, Matefish now offers the possibility to run a **ProofNumberSearch!**
For more information on this search method see https://www.chessprogramming.org/Proof-Number_Search.
Please note, the PNS Hash is currently limited to 1 GB. Nevertheless, PNS is able to solve mates
which are currently out of reach for the default Alpha-Beta search!

# Examples Proof-Number Search
```
setoption name ProofNumberSearch value true
position fen 8/5P2/8/8/8/n7/1pppp2K/br1r1kn1 w - -
go mate 10
info string Starting Proof-Number Search ...
info string Success! Mate in 10 found!
info time 52 multipv 1 depth 19 seldepth 19 nodes 199774 nps 3841807 tbhits 0 score mate 10 pv f7f8q f1e1 f8a3 e1f1 a3f8 f1e1 h2g1 d1c1 f8f2 e1d1 f2f3 d1e1 f3g3 e1d1 g3g4 d1e1 g4b4 c1d1 b4h4
bestmove f7f8q ponder f1e1

position fen 1R6/6pK/5p2/4p3/1B1p3n/8/p1p1pp2/1k3b2 w - - 0 1
go mate 38
info string Starting Proof-Number Search ...
info string Success! Mate in 38 found!
info time 63 multipv 1 depth 75 seldepth 75 nodes 294672 nps 4677333 tbhits 0 score mate 38 pv b4a3 b1a1 a3b2 a1b1 b2d4 b1c1 d4e3 c1d1 b8d8 d1e1 e3d2 e1d1 d2b4 d1c1 b4a3 c1b1 d8b8 b1a1 a3b2 a1b1 b2e5 b1c1 e5f4 c1d1 b8d8 d1e1 f4d2 e1d1 d2b4 d1c1 b4a3 c1b1 d8b8 b1a1 a3b2 a1b1 b2f6 b1c1 f6g5 c1d1 b8d8 d1e1 g5d2 e1d1 d2b4 d1c1 b4a3 c1b1 d8b8 b1a1 a3b2 a1b1 b2g7 b1c1 g7h6 c1d1 b8d8 d1e1 h6d2 e1d1 d2b4 d1c1 b4a3 c1b1 d8b8 b1a1 a3e7 c2c1q e7f6 c1b2 b8b2 e2e1q b2b3 e1c3 f6c3
bestmove b4a3 ponder b1a1
```

## Terms of use

Matefish is free, and distributed under the **GNU General Public License version 3**
(GPL v3). Essentially, this means you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your website, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute Matefish in
some way, you MUST always include the license and the full source code
(or a pointer to where the source code can be found) to generate the 
exact binary you are distributing. If you make any changes to the
source code, these changes must also be made available under the GPL v3.

For full details, read the copy of the GPL v3 found in the file named
*Copying.txt*.
