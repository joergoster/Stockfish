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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

#include "bitboard.h"
#include "misc.h"
#include "movegen.h"
#include "search.h"
#include "thread.h"
#include "timeman.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace Search {

  LimitsType Limits;
}

namespace Tablebases {

  int Cardinality;
  bool RootInTB;
  bool UseRule50;
  Depth ProbeDepth;
}

namespace TB = Tablebases;

using std::string;
using namespace Search;

namespace {

  constexpr uint32_t PROOF_MAX_INT = 10000000;

  // Basic piece values used for move-ordering
  constexpr int MVV[PIECE_TYPE_NB] = { 0, 100, 300, 305, 500, 900, 0, 0 };

  // Helper used to detect a basic mate config
  bool is_basic_mate(const Position& pos) {
  
    Color us = pos.side_to_move();

    Value npm =  pos.count<KNIGHT>(us) * KnightValueMg
               + pos.count<BISHOP>(us) * BishopValueMg
               + pos.count<ROOK  >(us) * RookValueMg
               + pos.count<QUEEN >(us) * QueenValueMg;
           
    return   !more_than_one(pos.pieces(~us))
          && !pos.count<PAWN>(us)
          && (   npm == RookValueMg
              || npm == QueenValueMg
              || npm == BishopValueMg * 2
              || npm == KnightValueMg * 3
              || npm == KnightValueMg + BishopValueMg);
  }

  // Function prototypes
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth);
  void pn_search(Position& pos);
  Value syzygy_search(Position& pos, int ply);

  // Global variables
  int allMoves, kingMoves;
  std::atomic<int> Movecount[MAX_PLY];

  // perft() is our utility to verify move generation. All the leaf nodes up
  // to the given depth are generated and counted, and the sum is returned.
  template<bool Root>
  uint64_t perft(Position& pos, Depth depth) {

    StateInfo st;
    uint64_t cnt, nodes = 0;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<LEGAL>(pos))
    {
        if (Root && depth <= 1)
            cnt = 1, nodes++;
        else
        {
            pos.do_move(m, st);
            cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m);
        }

        if (Root)
            sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
    }

    return nodes;
  }

} // namespace


/// Search::init() is called just before a new search is started,
/// and reads in some UCI options and prepares the root moves for
/// each thread.

void Search::init(Position& pos) {

  // Read UCI options
  kingMoves = Options["KingMoves"];
  allMoves  = Options["AllMoves"];

  // Initialize Movecount array
  for (int i = 0; i < MAX_PLY; i++)
      Movecount[i] = 0;

  // Analyze the root position in order to find some
  // automatic settings for the search if possible.
  const Color us = pos.side_to_move();
  const auto ourKing = pos.square<KING>(us);
  const auto theirKing = pos.square<KING>(~us);
  const Bitboard kingRing = pos.attacks_from<KING>(theirKing);
  int oppMoves, oppKingMoves;

  // Prepare the root moves
  RootMoves searchMoves;
  StateInfo rootSt;

  for (const auto& m : MoveList<LEGAL>(pos))
      if (   Limits.searchmoves.empty()
          || std::count(Limits.searchmoves.begin(), Limits.searchmoves.end(), m))
          searchMoves.emplace_back(m);

  // Now we rank the root moves for the mate search.
  // First, try ranking by TBs.
  TB::rank_root_moves(pos, searchMoves);

  if (TB::RootInTB)
      pos.this_thread()->tbHits = searchMoves.size();

  else
  {
      for (auto& rm : searchMoves)
      {
          rm.tbRank = 0;
          Square to = to_sq(rm.pv[0]);

          // Check bonuses
          if (pos.gives_check(rm.pv[0]))
          {
              rm.tbRank += 8000;

              // Bonus for a knight check
              if (type_of(pos.moved_piece(rm.pv[0])) == KNIGHT)
                  rm.tbRank += 400;

              // Bonus for queen/rook contact checks
              else if (  (type_of(pos.moved_piece(rm.pv[0])) == QUEEN
                       || type_of(pos.moved_piece(rm.pv[0])) == ROOK)
                       && distance(theirKing, to) == 1)
                   rm.tbRank += 500;
          }

          // Bonus for captures by MVV
          if (pos.capture(rm.pv[0]))
              rm.tbRank += MVV[type_of(pos.piece_on(to))];

          // Bonus for a move freeing a potential promotion square
          Bitboard ourPawns = pos.pieces(us, PAWN);
    
          if (   (us == WHITE && shift<NORTH>(ourPawns) & Rank8BB & from_sq(rm.pv[0]))
              || (us == BLACK && shift<SOUTH>(ourPawns) & Rank1BB & from_sq(rm.pv[0])))
              rm.tbRank += 500;          

          // Bonus for a knight eventually able to give check on the next move
          if (type_of(pos.moved_piece(rm.pv[0])) == KNIGHT)
          {
              if (pos.attacks_from<KNIGHT>(to) & pos.check_squares(KNIGHT))
                  rm.tbRank += 600;

              rm.tbRank += 256 * popcount(PseudoAttacks[KNIGHT][to] & kingRing);
          }

          // Bonus for a queen eventually able to give check on the next move
          else if (type_of(pos.moved_piece(rm.pv[0])) == QUEEN)
          {
              if (pos.attacks_from<QUEEN>(to) & pos.check_squares(QUEEN))
                  rm.tbRank += 500;

              rm.tbRank += 128 * popcount(PseudoAttacks[QUEEN][to] & kingRing);
          }

          // Bonus for a rook eventually able to give check on the next move
          else if (type_of(pos.moved_piece(rm.pv[0])) == ROOK)
          {
              if (pos.attacks_from<ROOK>(to) & pos.check_squares(ROOK))
                  rm.tbRank += 400;

              rm.tbRank += 96 * popcount(PseudoAttacks[ROOK][to] & kingRing);
          }

          // Bonus for a bishop eventually able to give check on the next move
          else if (type_of(pos.moved_piece(rm.pv[0])) == BISHOP)
          {
              if (pos.attacks_from<BISHOP>(to) & pos.check_squares(BISHOP))
                  rm.tbRank += 300;

              rm.tbRank += 64 * popcount(PseudoAttacks[BISHOP][to] & kingRing);
          }

          // Bonus for pawns TODO: promotions
          if (type_of(pos.moved_piece(rm.pv[0])) == PAWN)
          {
              rm.tbRank +=   64 * edge_distance(file_of(to))
                          + 128 * relative_rank(us, to);
          }

          // Try to prevent some checks
          if (PseudoAttacks[BISHOP][ourKing] & to)
              rm.tbRank += 128 - 32 * distance(ourKing, to);

          if (PseudoAttacks[ROOK][ourKing] & to)
              rm.tbRank += 128 - 32 * distance(ourKing, to);

          // R-Mobility (kind of ?)
          pos.do_move(rm.pv[0], rootSt);
          oppMoves = int(MoveList<LEGAL>(pos).size());
          oppKingMoves = int(MoveList<LEGAL, KING>(pos).size());
          pos.undo_move(rm.pv[0]);

          // Give an extra boost for mating moves!
          rm.tbRank += oppMoves == 0 ? 4096 : -8 * oppMoves;
          rm.tbRank -= 40 * oppKingMoves;
      }
  }

  // Now, sort the moves by their rank
  std::stable_sort(searchMoves.begin(), searchMoves.end(),
      [](const RootMove &a, const RootMove &b) { return a.tbRank > b.tbRank; });

  // If requested, print out the root moves and their ranking
  if (Options["RootMoveStats"])
      for (const auto& rm : searchMoves)
          std::cout << "Root move: " << UCI::move(rm.pv[0], pos.is_chess960()) << "   Rank: " << rm.tbRank << std::endl;
  
  // Finally, distribute the ranked root moves among all available threads
  auto it = searchMoves.begin();

  while (it < searchMoves.end())
  {
      for (Thread* th : Threads)
      {
          th->rootMoves.emplace_back(*it);
          it++;

          if (it == searchMoves.end())
              break;
      }
  }

  assert(Threads.nodes_searched() == 0);
}


/// Search::clear() resets search state to its initial value

void Search::clear() {

  Threads.main()->wait_for_search_finished();

  Threads.clear();
  Tablebases::init(Options["SyzygyPath"]); // Free mapped files
}


/// MainThread::search() is started when the program receives the UCI 'go'
/// command. It searches from the root position and outputs the "bestmove".

void MainThread::search() {

  // Special case 1: 'go perft x' 
  if (Limits.perft)
  {
      nodes = perft<true>(rootPos, Limits.perft);
      sync_cout << "\nNodes searched: " << nodes << "\n" << sync_endl;

      return;
  }

  // Special case 2: no move(s) to search
  if (rootMoves.empty())
  {
      // Must be mate or stalemate
      sync_cout << "info depth 0 score "
                << UCI::value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW)
                << std::endl;
      std::cout << "bestmove " << UCI::move(MOVE_NULL, rootPos.is_chess960()) << sync_endl;

      return;
  }

  Time.init(Limits, rootPos.side_to_move(), rootPos.game_ply());

  // Start the Proof-Number search, if requested
  if (Options["ProofNumberSearch"])
      pn_search(rootPos);

  else // Otherwise, start the default AB search
  {
      for (Thread* th : Threads)
          if (th != this)
              th->start_searching();

      Thread::search(); // Let's start searching!
  }

  while (!Threads.stop && Limits.infinite)
  {} // Busy wait for a stop

  // Stop the threads if not already stopped
  Threads.stop = true;

  // Wait until all threads have finished
  for (Thread* th : Threads)
      if (th != this)
          th->wait_for_search_finished();

  Thread* bestThread = this;

  for (Thread* th : Threads)
      if (  !th->rootMoves.empty()
          && th->rootMoves[0].score > bestThread->rootMoves[0].score)
          bestThread = th;

  // Give some info about the final result of the search
  if (bestThread->rootMoves[0].score < VALUE_MATE_IN_MAX_PLY)
      sync_cout << "info string Failure! No mate found!" << sync_endl;
  else
      sync_cout << "info string Success! Mate in "
                << (VALUE_MATE - bestThread->rootMoves[0].score + 1) / 2 << " found!" << sync_endl;

  // Print the best PV line
  sync_cout << UCI::pv(bestThread->rootPos, bestThread->rootDepth) << sync_endl;

  // Send best move and ponder move (if available)
  sync_cout << "bestmove " << UCI::move(bestThread->rootMoves[0].pv[0], bestThread->rootPos.is_chess960());

  if (bestThread->rootMoves[0].pv.size() > 1)
      std::cout << " ponder " << UCI::move(bestThread->rootMoves[0].pv[1], bestThread->rootPos.is_chess960());

  std::cout << sync_endl;
}


/// Thread::search() is the main iterative deepening loop. It calls search()
/// repeatedly with increasing depth until the allocated thinking time has been
/// consumed, the user stops the search, or the maximum search depth is reached.

void Thread::search() {

  // Do we have a basic endgame mate like KQK, KRK,
  // KBBK, KBNK or KNNNK? Then we don't need to search
  // but we can get a mate line using the syzygy dtz tables.
  if (   TB::RootInTB
      && is_basic_mate(rootPos))
  {
      if (   this == Threads.main()
          && rootMoves[0].tbRank > 900)
      {
          StateInfo rootSt;
          rootMoves[0].pv.resize(1);

          rootPos.do_move(rootMoves[0].pv[0], rootSt);
          rootMoves[0].score = -syzygy_search(rootPos, 1);
          rootPos.undo_move(rootMoves[0].pv[0]);

          rootDepth = rootMoves[0].selDepth;
          sync_cout << UCI::pv(rootPos, rootDepth) << sync_endl;
      }

      return;
  }
  
  Stack stack[MAX_PLY+1], *ss = stack;
  StateInfo rootSt;
  Value alpha, beta, bestValue, value;
  
  for (int i = 0; i <= MAX_PLY; ++i)
      (ss+i)->ply = i;

  ss->pv.clear();

  targetDepth = Limits.mate ? 2 * Limits.mate - 1 : MAX_PLY;
  fullDepth = std::max(targetDepth - (Limits.mate > 5 ? 4 : 2), 1);
  size_t multiPV = rootMoves.size();

  // Setting alpha, beta and bestValue such that we achieve
  // many beta cutoffs on odd plies.
  alpha = VALUE_MATE - 2 * Limits.mate;
  beta  = VALUE_INFINITE;
  bestValue = VALUE_MATE_IN_MAX_PLY - 1;

  while (true)
  {
      for (pvIdx = 0; pvIdx < multiPV; ++pvIdx)
      {
          // Only search winning moves
          if (   TB::RootInTB
              && rootMoves[pvIdx].tbRank <= 0)
              continue;

          if (  !TB::RootInTB
              && rootDepth == 1
              && rootMoves[pvIdx].tbRank < 5000)
              continue;

          ++Movecount[rootDepth];

          if (Time.elapsed() > 300 && this == Threads.main())
              sync_cout << "info currmove "  << UCI::move(rootMoves[pvIdx].pv[0], rootPos.is_chess960())
                        << " currmovenumber " << Movecount[rootDepth].load() << sync_endl;

          if (   targetDepth > 7
              && rootDepth > 3
              && rootDepth < targetDepth)
          {
              if (   rootDepth < targetDepth - 4
                  && rootMoves[pvIdx].tbRank < 8000)
                  continue;

              else if (   rootDepth < targetDepth - 2
                       && rootMoves[pvIdx].tbRank < 4000)
                  continue;

              else if (   rootDepth < targetDepth
                       && rootMoves[pvIdx].tbRank < 0)
                  continue;
          }

          selDepth = 1;

          assert(is_ok(rootMoves[pvIdx].pv[0]));
          
          // Make, search and undo the root move
          rootPos.do_move(rootMoves[pvIdx].pv[0], rootSt);
          nodes++;

          value = -::search(rootPos, ss+1, -beta, -alpha, rootDepth-1);

          rootPos.undo_move(rootMoves[pvIdx].pv[0]);

          if (value > bestValue)
          {
              bestValue = value;

              // Assign the search value, selective search depth
              // and the pv to this root move.
              rootMoves[pvIdx].score = value;
              rootMoves[pvIdx].selDepth = selDepth;
              rootMoves[pvIdx].pv.resize(1);

              // Append child pv
              for (auto& m : (ss+1)->pv)
                  rootMoves[pvIdx].pv.push_back(m);

              // Sort the PV lines searched so far
              std::stable_sort(rootMoves.begin(), rootMoves.begin() + pvIdx + 1);
          }

          // Have we found a "mate in x" within the specified limit?
          if (bestValue >= alpha)
              Threads.stop = true;

          if (Threads.stop.load())
              break;
      }

      if (Threads.stop.load())
          break;

      rootMoves[0].selDepth = selDepth;

      // Let the main thread report about the just finished depth
      if (this == Threads.main() && rootDepth < targetDepth)
          sync_cout << UCI::pv(rootPos, rootDepth) << sync_endl;

      // Target depth reached?
      if (rootDepth == targetDepth)
          break;

      rootDepth += 2;
  }
}


namespace {

  // search<>() is the main search function

  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    StateInfo st;
    Value bestValue, value;
    bool inCheck = !!pos.checkers();
    int moveCount;
    bool extension;
    Color us = pos.side_to_move();
    Thread* thisThread = pos.this_thread();

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);

    // Start with a fresh pv
    // Start with a fresh pv
    ss->pv.clear();
    thisThread->selDepth = std::max(thisThread->selDepth, ss->ply);

    // Check for aborted search or maximum ply reached
    if (   Threads.stop.load()
        || ss->ply == MAX_PLY)
        return VALUE_ZERO;

    // At the leafs, we simply either return a mate score
    // or zero. No evaluation needed!
    if (depth == 0)
    {
        if (inCheck && !MoveList<LEGAL>(pos).size())
            return mated_in(ss->ply);
        else
            return VALUE_DRAW;
    }

    if (ss->ply & 1)
    {
        if (   kingMoves < 8
            && int(MoveList<LEGAL, KING>(pos).size()) > kingMoves)
            return VALUE_DRAW;

        if (   allMoves < 250
            && int(MoveList<LEGAL>(pos).size()) > allMoves)
            return VALUE_DRAW;
    }

    // Check for draw by repetition
    if (pos.is_draw(ss->ply))
        return VALUE_DRAW;

    // TODO: Tablebase probe
    // For the root color we can immediately return on
    // TB draws or losses

    bestValue = -VALUE_INFINITE;
    moveCount = 0;
    int rankThisMove = 0;
    int oppMoves;

    std::vector<RankedMove> legalMoves;
    legalMoves.reserve(64);

    [[maybe_unused]] Bitboard b1 = pos.checkers();
    [[maybe_unused]] Bitboard ourPawns = pos.pieces(us, PAWN);
    [[maybe_unused]] Bitboard kingRing = pos.attacks_from<KING>(pos.square<KING>(~us));
    [[maybe_unused]] Square ourKing = pos.square<KING>(us);

    // Score the moves! VERY IMPORTANT!!!
    for (const auto& m : MoveList<LEGAL>(pos))
    {
        // Checking moves get a high enough rank for both sides
        if (pos.gives_check(m))
            rankThisMove += 8000;

        if (pos.capture(m))
            rankThisMove += MVV[type_of(pos.piece_on(to_sq(m)))];

        if (ss->ply & 1) // Side to get mated
        {
            if (inCheck)
            {
                // Rank moves first which capture the checking piece
                if (pos.capture(m))
                    rankThisMove += 1000;

                // Bonus for intercepting a check
                else if (   type_of(pos.moved_piece(m)) != KING
                         && aligned(lsb(b1), ourKing, to_sq(m)))
                    rankThisMove += 400;
            }
        }
        else
        {
            if (rankThisMove >= 6000) // Checking move
            {
                // Bonus for a knight check
                if (type_of(pos.moved_piece(m)) == KNIGHT)
                    rankThisMove += 400;

                // Bonus for queen/rook contact checks
                else if (  (type_of(pos.moved_piece(m)) == QUEEN
                         || type_of(pos.moved_piece(m)) == ROOK)
                         && distance(pos.square<KING>(~us), to_sq(m)) == 1)
                   rankThisMove += 500;

                pos.do_move(m, st);
                oppMoves = int(MoveList<LEGAL>(pos).size());
                pos.undo_move(m);

                // Give an extra boost for mating moves!
                rankThisMove += oppMoves == 0 ? 4096 : -8 * oppMoves;
            }

            if (pos.advanced_pawn_push(m))
                rankThisMove += 1000;
                
            // Bonus for a move freeing a potential promotion square
            if (   (us == WHITE && shift<NORTH>(ourPawns) & Rank8BB & from_sq(m))
                || (us == BLACK && shift<SOUTH>(ourPawns) & Rank1BB & from_sq(m)))
                rankThisMove += 500;          

            // Bonus for a piece eventually able to give check on the next move
            // or to attack squares next to the opponent's king.
            if (type_of(pos.moved_piece(m)) == KNIGHT)
            {
                if (pos.attacks_from<KNIGHT>(to_sq(m)) & pos.check_squares(KNIGHT))
                    rankThisMove += 600;

                rankThisMove += 256 * popcount(PseudoAttacks[KNIGHT][to_sq(m)] & kingRing);
            }
            else if (type_of(pos.moved_piece(m)) == QUEEN)
            {
                if (pos.attacks_from<QUEEN>(to_sq(m)) & pos.check_squares(QUEEN))
                    rankThisMove += 500;

                rankThisMove += 128 * popcount(PseudoAttacks[QUEEN][to_sq(m)] & kingRing);
            }
            else if (type_of(pos.moved_piece(m)) == ROOK)
            {
                if (pos.attacks_from<ROOK>(to_sq(m)) & pos.check_squares(ROOK))
                    rankThisMove += 400;

                rankThisMove += 96 * popcount(PseudoAttacks[ROOK][to_sq(m)] & kingRing);
            }
            else if (type_of(pos.moved_piece(m)) == BISHOP)
            {
                if (pos.attacks_from<BISHOP>(to_sq(m)) & pos.check_squares(BISHOP))
                    rankThisMove += 300;

                rankThisMove += 64 * popcount(PseudoAttacks[BISHOP][to_sq(m)] & kingRing);
            }

            // Try to prevent some checks
            if (PseudoAttacks[BISHOP][ourKing] & to_sq(m))
                rankThisMove += 128 - 32 * distance(ourKing, to_sq(m));

            if (PseudoAttacks[ROOK][ourKing] & to_sq(m))
                rankThisMove += 128 - 32 * distance(ourKing, to_sq(m));
        }

        // Add this ranked move
        legalMoves.emplace_back(RankedMove(m, rankThisMove));

        rankThisMove = 0;
    }

    std::sort(legalMoves.begin(), legalMoves.end());

    // Search all legal moves
    for (auto& lm : legalMoves)
    {
        extension = false;

        // Extensions
        // Not more than one extension and not during the last iteration.
        if (   depth == 1
            && ss->ply < thisThread->targetDepth - 1
            && thisThread->rootDepth < thisThread->targetDepth)
        {
            // Check extension
            // Fires always during all iterations except the last one,
            // and up to the specified mate limit.
            if (lm.rank >= 6000)
                extension = true;

            // Other moves will only be extended during the one
            // or the two iterations just before the last one.
            else if (thisThread->rootDepth >= thisThread->fullDepth)
            {
                // Extend captures and promotions
                if (pos.capture_or_promotion(lm.move))
                    extension = true;

                // and piece moves which can reach a possible checking square with the next move.
                else if (   (type_of(pos.moved_piece(lm.move)) == KNIGHT && pos.attacks_from<KNIGHT>(to_sq(lm.move)) & pos.check_squares(KNIGHT))
                         || (type_of(pos.moved_piece(lm.move)) == BISHOP && pos.attacks_from<BISHOP>(to_sq(lm.move)) & pos.check_squares(BISHOP))
                         || (type_of(pos.moved_piece(lm.move)) == ROOK   && pos.attacks_from<ROOK  >(to_sq(lm.move)) & pos.check_squares(ROOK))
                         || (type_of(pos.moved_piece(lm.move)) == QUEEN  && pos.attacks_from<QUEEN >(to_sq(lm.move)) & pos.check_squares(QUEEN)))
                    extension = true;
            }
        }

        // At frontier nodes we can skip all non-checking
        // and non-extended moves.
        if (    depth == 1
            && !extension
            &&  lm.rank < 6000)
        {
            assert(!pos.gives_check(lm.move));

            continue;
        }

        moveCount++;

        assert(is_ok(lm.move));
        
        pos.do_move(lm.move, st);
        thisThread->nodes++;
        
        value = -search(pos, ss+1, -beta, -alpha, depth - 1 + 2 * extension);
        
        pos.undo_move(lm.move);

        // Do we have a new best value?
        if (value > bestValue)
        {
            // Beta-cutoff?
            if (value >= beta)
                return value;

            bestValue = value;

            if (value > alpha)
            {
                // Update alpha
                alpha = value;

                // Reset PV and insert current best move
                ss->pv.clear();
                ss->pv.push_back(lm.move);

                // Append child pv
                for (auto& m : (ss+1)->pv)
                    ss->pv.push_back(m);
            }
        }

        // If we have found a mate within the specified limit,
        // we can immediately break from the moves loop.
        // Note: this can only happen for the root color!
        if (bestValue > VALUE_MATE - 2 * Limits.mate)
            break;
    }

    // No moves? Must be Mate or Stalemate!
    if (!moveCount)
        bestValue = inCheck ? mated_in(ss->ply) // Checkmate!
                            : VALUE_DRAW;

    assert(-VALUE_INFINITE <= bestValue && bestValue < VALUE_INFINITE);

    return bestValue;
  }

  // pn_search() is the Proof-Number search.
  //
  // See https://www.chessprogramming.org/Proof-Number_Search
  // and http://mcts.ai/pubs/mcts-survey-master.pdf

  void pn_search(Position& pos) {

    // Prepare our PNS Hash Table where we store all nodes
    PnsHash pns;
    pns.reserve(32000000); // About 1024 MB hash size

    // A small stack
    PnsStack stack[128], *ss = stack;

    for (int i = 0; i < 128; i++)
        (ss+i)->ply = i;

    Move PVTable[512][MAX_PLY]; // For storing the PVs
    for (int j = 0; j < 512; j++)
        for (int k = 0; k < MAX_PLY; k++)
            PVTable[j][k] = MOVE_NONE;

    bool giveOutput, updatePV;
    int targetDepth = std::min(2 * Limits.mate - 1, MAX_PLY-1);
    int pvLine = 0;
    uint64_t iteration;
    uint32_t minPN, minDN, sumChildrenPN, sumChildrenDN;

    Thread* thisThread = pos.this_thread();
    TimePoint elapsed, lastOutputTime;

    // Prepare the iterators
    const auto rootIndex = pns.begin(); // Index of the root node
    auto currentIndex = rootIndex;
    auto bestIndex = currentIndex;
    auto nextIndex = rootIndex + 1; // The next node will have this index

    for (RootMove& rm : thisThread->rootMoves)
        rm.score = VALUE_ZERO, rm.selDepth = targetDepth;
    thisThread->rootDepth = targetDepth;

    // Create the root node
    pns.push_back(Node(rootIndex, MOVE_NONE, false, 1, 1));
    thisThread->nodes++;

    lastOutputTime = now();
    giveOutput = updatePV = false;
    iteration = 0;


    // Now we can start the main MCTS loop, which consists of 4 steps:
    // Selection, Expansion, Simulation, and Backpropagation.
    while (!Threads.stop.load())
    {
        //////////////////////////////////////
        //                                  //
        //   Step 1: SELECTION              //
        //                                  //
        //////////////////////////////////////

        // Determine the most promising node for further expansion.
        // At OR nodes we are selecting the child node with the smallest
        // Proof Number (PN), while at AND nodes we are selecting the
        // one with the smallest Disproof Number (DN)!
        while ((*currentIndex).is_expanded() && ss->ply < targetDepth)
        {
            assert(!(*currentIndex).children.empty());

            if (ss->ply & 1) // AND node
            {
                minDN = PROOF_MAX_INT + 1;

                for (auto child : (*currentIndex).children)
                {
                    if ((*child).DN() < minDN)
                    {
                        minDN = (*child).DN();
                        bestIndex = child;
                    }
                }
            }
            else // OR node
            {
                minPN = PROOF_MAX_INT + 1;

                for (auto child : (*currentIndex).children)
                {
                    if ((*child).PN() < minPN)
                    {
                        minPN = (*child).PN();
                        bestIndex = child;
                    }
                }
            }

            // Reset the StateInfo object
            std::memset(&ss->st, 0, sizeof(StateInfo));

            // Make the move
            pos.do_move((*bestIndex).action(), ss->st);

            // Increment the stack level
            ss++;

            currentIndex = bestIndex;
        }


        //////////////////////////////////////
        //                                  //
        //   Step 2: EXPANSION              //
        //                                  //
        //////////////////////////////////////

        // We determined the Most-Proving Node (MPN). Simply
        // generate all child nodes and mark this node as
        // fully expanded.

        // The expanded node is 1 ply away
        const bool andNode = (ss->ply + 1) & 1;

        std::memset(&ss->st, 0, sizeof(StateInfo));

        for (auto& move : MoveList<LEGAL>(pos))
        {
            // Skip moves at the root which are not part
            // of the root moves of this thread.
            if (    currentIndex == rootIndex
                && !std::count(thisThread->rootMoves.begin(),
                               thisThread->rootMoves.end(), move))
                continue;

            if (    ss->ply == targetDepth - 1
                && !pos.gives_check(move))
            {
                assert(andNode);
                continue;
            }

            pos.do_move(move, ss->st);
            ss++;

            int n = int(MoveList<LEGAL>(pos).size());
            
            // Create the new node: new nodes are default-initialized as
            // non-terminal internal nodes with pn = 1 and dn = 1.
            pns.push_back(Node(currentIndex, move, false, andNode ? n : 1, andNode ? 1 : n));
            thisThread->nodes++;

            // Add index of this node as child node to the parent node
            (*currentIndex).children.push_back(nextIndex);

            // Check for mate, draw by repetition, 50-move rule or
            // maximum ply reached.
            if (n == 0)
            {
                if (pos.checkers()) // WIN for the root side
                {
                    (*nextIndex).pn = andNode ? 0 : PROOF_MAX_INT;
                    (*nextIndex).dn = andNode ? PROOF_MAX_INT : 0;

                    // If we have reached the specified mate distance, add
                    // the move leading to this node to the current PV line.
                    if (   ss->ply == targetDepth
                        && pvLine < 512)
                    {
                        assert(andNode);

//                        sync_cout << "Starting PV" << sync_endl;
                        PVTable[pvLine][ss->ply-1] = move;
                        updatePV = true;
                    }
                }
                else // Treat stalemates as a LOSS for the root side
                {
                    (*nextIndex).pn = PROOF_MAX_INT;
                    (*nextIndex).dn = 0;
                }
            }
            else if (pos.is_draw(ss->ply) || ss->ply == targetDepth)
            {
                (*nextIndex).pn = PROOF_MAX_INT;
                (*nextIndex).dn = 0;
            }
//            else if (pos.is_draw(ss->ply)) // Treat repetitions as a LOSS for the root side
//            {
//                (*nextIndex).pn = PROOF_MAX_INT;
//                (*nextIndex).dn = 0;
//            }

            nextIndex++;

            pos.undo_move(move);
            ss--;

//            if (   ( andNode && (*(nextIndex-1)).PN() == 0)
//                || (!andNode && (*(nextIndex-1)).DN() == 0))
//                break;
        }

        (*currentIndex).mark_as_expanded();


        //////////////////////////////////////
        //                                  //
        //   Step 3: SIMULATION             //
        //                                  //
        //////////////////////////////////////


        //////////////////////////////////////
        //                                  //
        //   Step 4: BACKPROPAGATION        //
        //                                  //
        //////////////////////////////////////

        // Now we have to unwind all made moves
        // to get back to the root position and we're
        // updating every single node on this way.
        while (true)
        {
            if (ss->ply & 1) // AND node
            {
                sumChildrenPN = 0;
                minDN = PROOF_MAX_INT + 1;

                for (auto idx : (*currentIndex).children)
                {
                    sumChildrenPN = std::min(sumChildrenPN + (*idx).PN(), PROOF_MAX_INT);

                    if ((*idx).DN() < minDN)
                        minDN = (*idx).DN();
                }

                (*currentIndex).pn = sumChildrenPN;
                (*currentIndex).dn = minDN;
            }
            else // OR node
            {
                minPN = PROOF_MAX_INT + 1;
                sumChildrenDN = 0;

                for (auto idx : (*currentIndex).children)
                {
                    if ((*idx).PN() < minPN)
                        minPN = (*idx).PN();

                    sumChildrenDN = std::min(sumChildrenDN + (*idx).DN(), PROOF_MAX_INT);
                }

                (*currentIndex).pn = minPN;
                (*currentIndex).dn = sumChildrenDN;
            }

            if (currentIndex == rootIndex)
                break;

            // Update PV if necessary
            if (updatePV)
                PVTable[pvLine][ss->ply-1] = (*currentIndex).action();

            // Go back to the parent node
            pos.undo_move((*currentIndex).action());
            ss--;

            currentIndex = (*currentIndex).parent_id();
        }

        // We are back at the root!
        assert(currentIndex == rootIndex);
        assert(ss->ply == 0);

        // Iteration finished
        iteration++;
        bestIndex = rootIndex;

        if (updatePV)
        {
            pvLine++;
            updatePV = false;
        }

        // Now check for some stop conditions
        if (   iteration >= 10000000
            || (*rootIndex).PN() == 0
            || (*rootIndex).DN() == 0)
            Threads.stop = true;

        else if (   Limits.nodes
                 && iteration >= uint64_t(Limits.nodes))
            Threads.stop = true;
            
        else if (   Limits.movetime
                 && Time.elapsed() >= Limits.movetime)
            Threads.stop = true;

        // Time for another GUI update?
        if (!Threads.stop.load())
        {
            elapsed = now();

            giveOutput =  Time.elapsed() <  2100 ? elapsed - lastOutputTime >= 200
                        : Time.elapsed() < 10100 ? elapsed - lastOutputTime >= 1000
                        : Time.elapsed() < 60100 ? elapsed - lastOutputTime >= 2500
                                                 : elapsed - lastOutputTime >= 5000;
            if (giveOutput)
                lastOutputTime = now();
        }

        // Update the root moves stats and send info
        if (   Threads.stop.load()
            || giveOutput)
        {
            // Assign the score and the PV to one root move only.
            // In the best case it's the proving move.
            std::vector<Node>::iterator pvNode;

            for (auto rootChild : (*rootIndex).children)
            {
                sync_cout << "Root move " << UCI::move((*rootChild).action(), pos.is_chess960()) << "   PN: " << (*rootChild).PN()
                                                                                                 << "   DN: " << (*rootChild).DN() << sync_endl;
                if ((*rootChild).PN() == 0)
                    pvNode = rootChild;
            }

            if ((*rootIndex).PN() == 0 && PVTable[0][0] != MOVE_NONE)
            {
                assert((*pvNode).PN() == 0);

                RootMove& rm = *std::find(thisThread->rootMoves.begin(),
                                          thisThread->rootMoves.end(), (*pvNode).action());
                rm.pv.resize(1);

                int m, n;
                for (m = 0; m < 512; m++)
                    if (PVTable[m][0] == rm.pv[0]) break;

                for (n = 1; n < MAX_PLY; ++n)
                {
                    if (PVTable[m][n] == MOVE_NONE)
                        break;

                    rm.pv.push_back(PVTable[m][n]);
                }

                rm.score = VALUE_MATE - int(rm.pv.size());
            }

            // Sort the root moves and update the GUI
            std::stable_sort(thisThread->rootMoves.begin(), thisThread->rootMoves.end());

            if (!Threads.stop.load())
                sync_cout << UCI::pv(pos, targetDepth) << sync_endl;
        }

    }

    pns.clear();
  }

  // syzygy_search() tries to build a mating sequence if the
  // root position is a winning TB position. It repeatedly
  // calls itself until a mate is found.

  Value syzygy_search(Position& pos, int ply) {

    StateInfo st;
    Move bestMove;
    Value bestValue;
    RootMoves legalMoves;

    bestMove = MOVE_NONE;
    bestValue = -VALUE_INFINITE;

    // No legal moves? Must be mate!
    if (!MoveList<LEGAL>(pos).size())
    {
        pos.this_thread()->rootMoves[0].pv.resize(ply);
        pos.this_thread()->rootMoves[0].selDepth = ply;

        return mated_in(ply);
    }

    // Insert legal moves
    for (const auto& m : MoveList<LEGAL>(pos))
        legalMoves.emplace_back(m);

    // Rank moves strictly by dtz and pick the best
    Tablebases::rank_root_moves(pos, legalMoves);
    pos.this_thread()->tbHits += legalMoves.size();

    bestMove = legalMoves[0].pv[0];

    pos.do_move(bestMove, st);
    pos.this_thread()->nodes++;
    bestValue = -syzygy_search(pos, ply+1);
    pos.undo_move(bestMove);

    pos.this_thread()->rootMoves[0].pv[ply] = bestMove;

    return bestValue;
  }

} // namespace


/// MainThread::check_time() is used to print debug info and, more importantly,
/// to detect when we are out of available time and thus stop the search.

void MainThread::check_time() {

  if (--callsCnt > 0)
      return;

  // When using nodes, ensure checking rate is not lower than 0.1% of nodes
  callsCnt = Limits.nodes ? std::min(1024, int(Limits.nodes / 1024)) : 1024;

  static TimePoint lastInfoTime = now();

  TimePoint elapsed = Time.elapsed();
  TimePoint tick = Limits.startTime + elapsed;

  if (tick - lastInfoTime >= 1000)
  {
      lastInfoTime = tick;
      dbg_print();
  }

  if (   (Limits.use_time_management() && elapsed > Time.maximum() - 10)
      || (Limits.movetime && elapsed >= Limits.movetime)
      || (Limits.nodes && Threads.nodes_searched() >= (uint64_t)Limits.nodes))
      Threads.stop = true;
}


/// UCI::pv() formats PV information according to the UCI protocol. UCI requires
/// that all (if any) unsearched PV lines are sent using a previous search score.

string UCI::pv(const Position& pos, Depth depth) {

  std::stringstream ss;
  TimePoint elapsed = Time.elapsed() + 1;
  const RootMoves& rootMoves = pos.this_thread()->rootMoves;
  uint64_t nodesSearched = Threads.nodes_searched();
  uint64_t tbHits = Threads.tb_hits();

      if (ss.rdbuf()->in_avail()) // Not at first line
          ss << "\n";

      ss << "info"
         << " time "     << elapsed
         << " multipv "  << 1
         << " depth "    << depth
         << " seldepth " << rootMoves[0].selDepth
         << " nodes "    << nodesSearched
         << " nps "      << nodesSearched * 1000 / elapsed
         << " tbhits "   << tbHits
         << " score "    << UCI::value(rootMoves[0].score)
         << " pv";

      for (Move m : rootMoves[0].pv)
          ss << " " << UCI::move(m, pos.is_chess960());

  return ss.str();
}


void Tablebases::rank_root_moves(Position& pos, Search::RootMoves& rootMoves) {

    RootInTB = false;
    UseRule50 = Options["Syzygy50MoveRule"];
    ProbeDepth = Options["SyzygyProbeDepth"];
    Cardinality = Options["SyzygyProbeLimit"];

    // Tables with fewer pieces than SyzygyProbeLimit are searched with
    // ProbeDepth == DEPTH_ZERO
    if (Cardinality > MaxCardinality)
        Cardinality = MaxCardinality;

    if (rootMoves.size() == 0)
        return;

    if (    Cardinality >= pos.count<ALL_PIECES>()
        && !pos.can_castle(ANY_CASTLING))
    {
        // Rank moves using DTZ tables
        RootInTB = root_probe(pos, rootMoves);

        // DTZ tables are missing; try to rank moves using WDL tables
        if (!RootInTB)
            RootInTB = root_probe_wdl(pos, rootMoves);
    }

    // Clean up if both, root_probe() and root_probe_wdl() have failed!
    if (!RootInTB)
        for (auto& rm : rootMoves)
            rm.tbRank = 0;
}
