// Copyright (c) 2019 Julian Bernhard
// 
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
// ========================================================

#ifndef SIMPLESTATE_H
#define SIMPLESTATE_H

#include <iostream>
#include <random>
#include "mcts/hypothesis/hypothesis_state.h"

using namespace mcts;


typedef enum Actions {
        WAIT = 0,
        FORWARD = 1,
        BACKWARD = -1,
        NUM = 3
    } Actions;

class AgentPolicyCrossingState {
  public:
    AgentPolicyCrossingState(const std::pair<unsigned int, unsigned int>& desired_gap_range) : 
                            desired_gap_range_(desired_gap_range) {}
    ActionIdx act(const unsigned int& ego_distance) const {
        // sample desired gap parameter
        std::mt19937 gen(1000); //Standard mersenne_twister_engine seeded with rd()
        std::uniform_int_distribution<unsigned int> dis(desired_gap_range_.first, desired_gap_range_.second);
        const unsigned int desired_gap_dst = dis(gen);

        return calculate_action(ego_distance, desired_gap_dst);
    }

    ActionIdx calculate_action(const unsigned int& ego_distance, const unsigned int desired_gap_dst) const {
        if (ego_distance - desired_gap_dst > 0) {
            return Actions::FORWARD;
        } else if (ego_distance - desired_gap_dst == 0) {
            return Actions::WAIT;
        } else {
            return Actions::BACKWARD;
        }
    }

    Probability get_probability(const unsigned int& ego_distance, const ActionIdx& action) const {
        std::vector<unsigned int> gap_distances(desired_gap_range_.second - desired_gap_range_.first+1);
        std::iota(gap_distances.begin(), gap_distances.end(),desired_gap_range_.first);
        unsigned int action_selected = 0;
        for(const auto& desired_gap_dst : gap_distances) {
            const auto calculated = calculate_action(ego_distance, desired_gap_dst);
            if(calculated == action ) {
                action_selected++;
            }
        }
        return static_cast<float>(action_selected)/static_cast<float>(gap_distances.size());
    }

  private: 
        const std::pair<unsigned int, unsigned int> desired_gap_range_;
};

    
typedef struct AgentState {
    AgentState() : x_pos(0), last_action(Actions::WAIT) {}
    AgentState(const unsigned int& x, const Actions& last_action) : 
            x_pos(x), last_action(last_action) {}
    unsigned int x_pos;
    Actions last_action;
} AgentState;

// A simple environment with a 1D state, only if both agents select different actions, they get nearer to the terminal state
class HypothesisCrossingState : public mcts::HypothesisStateInterface<HypothesisCrossingState>
{
private:
  static const unsigned int num_other_agents = 1;

public:
    HypothesisCrossingState(const std::unordered_map<AgentIdx, HypothesisId>& current_agents_hypothesis) :
                            HypothesisStateInterface<HypothesisCrossingState>(current_agents_hypothesis),
                            hypothesis_(),
                            other_agent_states_(),
                            ego_state_(),
                            terminal_(false) {}

    HypothesisCrossingState(const std::unordered_map<AgentIdx, HypothesisId>& current_agents_hypothesis,
                            const std::array<AgentState, num_other_agents> other_agent_states,
                            const AgentState& ego_state,
                            const bool& terminal) :
                            HypothesisStateInterface<HypothesisCrossingState>(current_agents_hypothesis),
                            hypothesis_(),
                            other_agent_states_(other_agent_states),
                            ego_state_(ego_state),
                            terminal_(terminal) {};
    ~HypothesisCrossingState() {};

    std::shared_ptr<HypothesisCrossingState> clone() const
    {
        return std::make_shared<HypothesisCrossingState>(*this);
    }

    ActionIdx plan_action_current_hypothesis(const AgentIdx& agent_idx) const {
        const HypothesisId agt_hyp_id = current_agents_hypothesis_.at(agent_idx);
        return hypothesis_.at(agt_hyp_id).act(dst_to_ego(agent_idx-1));
    };

    template<typename ActionType = int>
    Probability get_probability(const HypothesisId& hypothesis, const AgentIdx& agent_idx, const ActionType& action) const { 
        return hypothesis_.at(hypothesis).get_probability(dst_to_ego(agent_idx-1), action);
    ;}

    template<typename ActionType = int>
    ActionType get_last_action(const AgentIdx& agent_idx) const {
        if (agent_idx == ego_agent_idx) {
            return ego_state_.last_action;
        } else {
            return other_agent_states_[agent_idx-1].last_action;
        }
    }

    Probability get_prior(const HypothesisId& hypothesis, const AgentIdx& agent_idx) const { return 0.5f;}

    HypothesisId get_num_hypothesis(const AgentIdx& agent_idx) const {return hypothesis_.size();}

    std::shared_ptr<HypothesisCrossingState> execute(const JointAction& joint_action, std::vector<Reward>& rewards, Cost& ego_cost) const {
        // normally we map each single action value in joint action with a map to the floating point action. Here, not required
        const AgentState next_ego_state(ego_state_.x_pos + joint_action[ego_agent_idx], static_cast<Actions>(joint_action[ego_agent_idx]));

        std::array<AgentState, num_other_agents> next_other_agent_states;
        for(size_t i = 0; i < other_agent_states_.size(); ++i) {
            const auto& old_state = other_agent_states_[i];
            next_other_agent_states[i] = AgentState(old_state.x_pos + joint_action[i+1], static_cast<Actions>(joint_action[i+1]));
        }

        const bool goal_reached = ego_state_.x_pos >= ego_goal_reached_position;
        bool collision = false;
        for (const auto& state: next_other_agent_states) {
            if(next_ego_state.x_pos == crossing_point && state.x_pos == crossing_point) {
                collision = true;
            }
        }

        const bool terminal = goal_reached || collision;
        rewards.resize(num_other_agents+1);
        rewards[0] = goal_reached * 100.0f - 1000.0f * collision;
        ego_cost = collision * 1.0f;

        return std::make_shared<HypothesisCrossingState>(current_agents_hypothesis_, next_other_agent_states, next_ego_state, terminal);
    }

    ActionIdx get_num_actions(AgentIdx agent_idx) const {
        return static_cast<size_t>(Actions::NUM); // WAIT, FORWARD, BACKWARD
    }

    bool is_terminal() const {
        return terminal_;
    }

    const std::vector<AgentIdx> get_agent_idx() const {
        std::vector<AgentIdx> agent_idx(num_other_agents+1);
        std::iota(agent_idx.begin(), agent_idx.end(),0);
        return agent_idx; // adapt to number of agents
    }

    std::string sprintf() const
    {
        std::stringstream ss; // todo
        return ss.str();
    }

    void add_hypothesis(const AgentPolicyCrossingState& hypothesis) {
        hypothesis_.push_back(hypothesis);
    }

private:

    inline unsigned int dst_to_ego(const AgentIdx& other_agent_idx) const {
        return ego_state_.x_pos - other_agent_states_[other_agent_idx].x_pos;
    }

private:
  
    const unsigned int state_x_length = 41; /* 21 is crossing point (41-1)/2+1 */
    const unsigned int ego_goal_reached_position = 35;
    const unsigned int crossing_point = (state_x_length-1)/2+1;

    std::vector<AgentPolicyCrossingState> hypothesis_;

    std::array<AgentState, num_other_agents> other_agent_states_;
    AgentState ego_state_;
    bool terminal_;
};



#endif 
