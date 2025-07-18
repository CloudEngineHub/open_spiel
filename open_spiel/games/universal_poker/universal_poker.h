// Copyright 2019 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef OPEN_SPIEL_GAMES_UNIVERSAL_POKER_H_
#define OPEN_SPIEL_GAMES_UNIVERSAL_POKER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "open_spiel/games/universal_poker/acpc/project_acpc_server/game.h"
#include "open_spiel/abseil-cpp/absl/algorithm/container.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/abseil-cpp/absl/types/optional.h"
#include "open_spiel/abseil-cpp/absl/types/span.h"
#include "open_spiel/games/universal_poker/acpc_cpp/acpc_game.h"
#include "open_spiel/games/universal_poker/logic/card_set.h"
#include "open_spiel/policy.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"

// This is a wrapper around the Annual Computer Poker Competition bot (ACPC)
// environment. See http://www.computerpokercompetition.org/. The code is
// initially available at https://github.com/ethansbrown/acpc
// It is an optional dependency (see install.md for documentation and
// open_spiel/scripts/global_variables.sh to enable this).
//
// It has not been extensively reviewed/tested by the DeepMind OpenSpiel team.
namespace open_spiel {
namespace universal_poker {

class UniversalPokerGame;

constexpr uint8_t kMaxUniversalPokerPlayers = 10;

// This is the mapping from int to action. E.g. the legal action "0" is fold,
// the legal action "1" is check/call, etc.
enum ActionType { kFold = 0, kCall = 1, kBet = 2, kAllIn = 3, kHalfPot = 4 };

// There are 5 actions: Fold, Call, Half-Pot bet, Pot Bet, and all-in.
inline constexpr int kNumActionsFCHPA =
    static_cast<int>(ActionType::kHalfPot) + 1;

enum BettingAbstraction { kFCPA = 0, kFC = 1, kFULLGAME = 2, kFCHPA = 3 };

enum StateActionType {
  ACTION_DEAL = 1,
  ACTION_FOLD = 2,
  ACTION_CHECK_CALL = 4,
  ACTION_BET = 8,
  ACTION_ALL_IN = 16
};

constexpr StateActionType ALL_ACTIONS[5] = {
    ACTION_DEAL, ACTION_FOLD, ACTION_CHECK_CALL, ACTION_BET, ACTION_ALL_IN};

class UniversalPokerState : public State {
 public:
  explicit UniversalPokerState(std::shared_ptr<const Game> game);

  bool IsTerminal() const override;
  bool IsChanceNode() const override;
  Player CurrentPlayer() const override;
  std::string ActionToString(Player player, Action move) const override;
  std::string ToString() const override;
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;
  // Warning: all 'call' actions will have encoded sizing of 0. This could be
  // potentially misleading in certain all-in situations if the caller has a
  // stack that is smaller than the size of the bet! (See ObservationTensor if
  // you need any player's exact contribution to the pot).
  void InformationStateTensor(Player player,
                              absl::Span<float> values) const override;
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;

  // The probability of taking each possible action in a particular info state.
  std::vector<std::pair<Action, double>> ChanceOutcomes() const override;
  std::vector<Action> LegalActions() const override;

  // Used to make UpdateIncrementalStateDistribution much faster.
  std::unique_ptr<HistoryDistribution> GetHistoriesConsistentWithInfostate(
      int player_id) const override;
  std::vector<Action> ActionsConsistentWithInformationFrom(
      Action action) const override {
    return {action};
  }
  std::unique_ptr<State> ResampleFromInfostate(
      int player_id, std::function<double()> rng) const;

  const acpc_cpp::ACPCState &acpc_state() const { return acpc_state_; }
  const BettingAbstraction &betting() const { return betting_abstraction_; }

  int PotSize(double multiple = 1.) const;

  // Returns the raise-to size of the current player going all-in.
  int AllInSize() const;
  void ApplyChoiceAction(StateActionType action_type, int size);

 protected:
  void DoApplyAction(Action action_id) override;

 private:
  void _CalculateActionsAndNodeType();

  double GetTotalReward(Player player) const;

  const uint32_t &GetPossibleActionsMask() const { return possibleActions_; }
  int GetPossibleActionCount() const;

  // Note: might want to update the action sequence in the future to track
  // everything per-round.
  const std::string &GetActionSequence() const { return actionSequence_; }
  // Unabstracted sizings for each entry in the Action Sequence.
  const std::vector<int> &GetActionSequenceSizings() const {
    return actionSequenceSizings_;
  }

  // Helper functions for dealing cards and getting the cards that have been
  // dealt.
  void AddBoardCard(uint8_t card);
  void AddHoleCard(uint8_t card);
  logic::CardSet HoleCards(Player player) const;
  logic::CardSet BoardCards() const;
  uint8_t ChanceOutcomeToCard(Action outcome) const;

  const acpc_cpp::ACPCGame *acpc_game_;
  mutable acpc_cpp::ACPCState acpc_state_;
  logic::CardSet deck_;  // The remaining cards to deal.
  int hole_cards_dealt_ = 0;
  int board_cards_dealt_ = 0;

  // The current player:
  // kChancePlayerId for chance nodes
  // kTerminalPlayerId when we everyone except one player has fold, or that
  // we have reached the showdown.
  // The current player >= 0 otherwise.
  Player cur_player_;
  uint32_t possibleActions_;
  std::string actionSequence_;
  std::vector<int> actionSequenceSizings_;

  BettingAbstraction betting_abstraction_;

  // Used for custom implementation of subgames. Subgames are hand-constructed
  // endgames that have been used a lot in the Poker AI literature. To build
  // a subgame the handReaches_ vector must be populated with the reach
  // probabilities of each player for each of the possible hands. Please see
  // universal_poker_test.cc for an example of how to construct custom subgames.
  std::vector<double> handReaches_;
  bool IsSubgame() const { return !handReaches_.empty(); }
  std::vector <std::pair <Action, double>> DistributeHandCardsInSubgame() const;
  bool IsDistributingSingleCard() const;
  std::vector <int> GetEncodingBase() const;
};

class UniversalPokerGame : public Game {
 public:
  explicit UniversalPokerGame(const GameParameters &params);

  int NumDistinctActions() const override;
  std::unique_ptr<State> NewInitialState() const override;
  int NumPlayers() const override;
  double MinUtility() const override;
  double MaxUtility() const override;
  int MaxChanceOutcomes() const override;
  absl::optional<double> UtilitySum() const override { return 0; }
  std::vector<int> InformationStateTensorShape() const override;
  std::vector<int> ObservationTensorShape() const override;
  int MaxGameLength() const override;
  // TODO: verify whether this bound is tight and/or tighten it.
  int MaxChanceNodesInHistory() const override { return MaxGameLength(); }
  BettingAbstraction betting_abstraction() const {
    return betting_abstraction_;
  }

  int big_blind() const { return big_blind_; }
  double MaxCommitment() const;
  const acpc_cpp::ACPCGame *GetACPCGame() const { return &acpc_game_; }
  std::string parseParameters(const GameParameters &map);

 private:
  std::string gameDesc_;
  const acpc_cpp::ACPCGame acpc_game_;
  const int potSize_;
  const std::string boardCards_;
  const std::string handReaches_;
  absl::optional<int> max_game_length_;
  BettingAbstraction betting_abstraction_ = BettingAbstraction::kFULLGAME;
  int big_blind_;
  int max_stack_size_;
};

// Only supported for UniversalPoker. Randomly plays an action from a fixed list
// of actions. If none of the actions are legal, checks/calls.
class UniformRestrictedActions : public Policy {
 public:
  // Actions will be restricted to this list when legal. If no such action is
  // legal, checks/calls.
  explicit UniformRestrictedActions(absl::Span<const ActionType> actions)
      : actions_(actions.begin(), actions.end()),
        max_action_(*absl::c_max_element(actions)) {}

  ActionsAndProbs GetStatePolicy(const State &state) const {
    ActionsAndProbs policy;
    policy.reserve(actions_.size());
    const std::vector<Action> legal_actions = state.LegalActions();
    for (Action action : legal_actions) {
      if (actions_.contains(static_cast<ActionType>(action))) {
        policy.emplace_back(action, 1.);
      }
      if (policy.size() >= actions_.size() || action > max_action_) break;
    }

    // It is always legal to check/call.
    if (policy.empty()) {
      SPIEL_DCHECK_TRUE(absl::c_find(legal_actions, ActionType::kCall) !=
                        legal_actions.end());
      policy.push_back({static_cast<Action>(ActionType::kCall), 1.});
    }

    // If we have a non-empty policy, normalize it!
    if (policy.size() > 1) NormalizePolicy(&policy);
    return policy;
  }

 private:
  const absl::flat_hash_set<ActionType> actions_;
  const ActionType max_action_;
};

// Converts an ACPC action into one that's compatible with UniversalPokerGame.
open_spiel::Action ACPCActionToOpenSpielAction(
    const project_acpc_server::Action &action,
    const UniversalPokerState &state);

// Get hole card index within the array of reach probabilities, as specified
// in https://github.com/Sandholm-Lab/LibratusEndgames :
//
// The probability, according to the Libratus blueprint strategy, of each player
// reaching this endgame with each hand. There are a total of 2,652
// probabilities in this list. The first 1,326 are for the "out of position"
// player (the first player to act on the round), while the remaining 1,326 are
// for the "button" player. Each of the 1,326 probabilities corresponds to a
// poker hand, ordered as follows:
//
// 2s2h, 2s2d, 2s2c, 2s3s, 2s3h, ..., 2sAc, 2h2d, 2h2c, ..., AdAc.
int GetHoleCardsReachIndex(int card_a, int card_b,
                           int num_suits, int num_ranks);

// Make random subgame, with optionally specified round, pot size, board
// cards and hand reach probs. If all of these variables are specified,
// it is actually a non-randomized subgame: by omiting any parameter,
// a random value will be supplied automatically.
std::shared_ptr<const Game> MakeRandomSubgame(
    std::mt19937 &rng, int pot_size = -1, std::string board_cards = "",
    std::vector<double> hand_reach = {});

// Converts an ACPC gamedef into the corresponding OpenSpiel universal_poker
// game-state string and uses that string to load + return the game.
std::shared_ptr<const Game> LoadUniversalPokerGameFromACPCGamedef(
    const std::string &acpc_gamedef);

// Number of unique hands in no-limit poker.
constexpr int kSubgameUniqueHands = 1326;  // = (52*51) / 2

std::ostream &operator<<(std::ostream &os, const BettingAbstraction &betting);

}  // namespace universal_poker
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_UNIVERSAL_POKER_H_
