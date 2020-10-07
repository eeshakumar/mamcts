// Copyright (c) 2019 Julian Bernhard
// 
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
// ========================================================

#ifndef UCT_STATISTIC_H
#define UCT_STATISTIC_H

#include "mcts/mcts.h"
#include <unordered_map>
#include <iostream>
#include <iomanip>

namespace mcts {

// A upper confidence bound implementation
class UctStatistic : public mcts::NodeStatistic<UctStatistic>, mcts::RandomGenerator
{
public:
    MCTS_TEST
    FRIEND_COST_CONSTRAINED_STATISTIC

    typedef struct UcbPair
    {
        UcbPair() : action_count_(0), action_value_(0.0f) {};
        unsigned action_count_;
        double action_value_;
    } UcbPair;
    typedef std::map<ActionIdx, UcbPair> UcbStatistics;

    UctStatistic(ActionIdx num_actions, AgentIdx agent_idx, const MctsParameters & mcts_parameters) :
             NodeStatistic<UctStatistic>(num_actions, agent_idx, mcts_parameters),
             RandomGenerator(mcts_parameters.RANDOM_SEED),
             value_(0.0f),
             latest_return_(0.0),
             ucb_statistics_(),
             total_node_visits_(0),
             unexpanded_actions_(num_actions),
             upper_bound(mcts_parameters.uct_statistic.UPPER_BOUND),
             lower_bound(mcts_parameters.uct_statistic.LOWER_BOUND),
             k_discount_factor(mcts_parameters.DISCOUNT_FACTOR), 
             exploration_constant(mcts_parameters.uct_statistic.EXPLORATION_CONSTANT),
             progressive_widening_k(mcts_parameters.uct_statistic.PROGRESSIVE_WIDENING_K),
             progressive_widening_alpha(mcts_parameters.uct_statistic.PROGRESSIVE_WIDENING_ALPHA) {
                 // initialize action indexes from 0 to (number of actions -1)
                 std::iota(unexpanded_actions_.begin(), unexpanded_actions_.end(), 0);
             }

    ~UctStatistic() {};

    template <class S>
    ActionIdx choose_next_action(const S& state) {
        if(require_progressive_widening_total()) {
            // Select randomly an unexpanded action
            std::uniform_int_distribution<ActionIdx> random_action_selection(0,unexpanded_actions_.size()-1);
            ActionIdx array_idx = random_action_selection(random_generator_);
            ActionIdx selected_action = unexpanded_actions_[array_idx];
            unexpanded_actions_.erase(unexpanded_actions_.begin()+array_idx);
            ucb_statistics_[selected_action] = UcbPair();
            return selected_action;
        } else {
            // Select an action based on the UCB formula
            std::unordered_map<ActionIdx, double> values;
            ActionIdx selected_action = calculate_ucb_and_max_action(ucb_statistics_, values);
            return selected_action;
       }
    }

    ActionIdx get_best_action() const {
        double temp = ucb_statistics_.begin()->second.action_value_;
        ActionIdx best = ucb_statistics_.begin()->first;
        for (auto it = ucb_statistics_.begin(); it != ucb_statistics_.end(); ++it)
        {
            if(it->second.action_value_>temp){
                temp = it->second.action_value_;
                best = it->first;
            }
        }
        return best;
    }

    Policy get_policy() const {
        Policy policy;
        for (auto it = ucb_statistics_.begin(); it != ucb_statistics_.end(); ++it)
        {
            policy[it->first] = it->second.action_value_;
        }
        return policy;
    }

    void update_from_heuristic(const NodeStatistic<UctStatistic>& heuristic_statistic) {
        const UctStatistic& heuristic_statistic_impl = heuristic_statistic.impl();
        update_from_heuristic_from_backpropagated(heuristic_statistic_impl.value_);
    }


    void update_from_heuristic_from_backpropagated(const Reward& backpropagated) {
        value_ = backpropagated;
        latest_return_ = value_;
        total_node_visits_ += 1;
    }

    void update_statistic(const NodeStatistic<UctStatistic>& changed_child_statistic) {
        const UctStatistic& changed_uct_statistic = changed_child_statistic.impl();
        this->update_statistics_from_backpropagated(changed_uct_statistic.latest_return_);
    }
    
    void update_statistics_from_backpropagated(const Reward& backpropagated) {
        //Action Value update step
        UcbPair& ucb_pair = ucb_statistics_[collected_reward_.first]; // we remembered for which action we got the reward, must be the same as during backprop, if we linked parents and childs correctly
        //action value: Q'(s,a) = Q(s,a) + (latest_return - Q(s,a))/N =  1/(N+1 ( latest_return + N*Q(s,a))
        latest_return_ = collected_reward_.second + k_discount_factor * backpropagated;
        ucb_pair.action_count_ += 1;
        ucb_pair.action_value_ = ucb_pair.action_value_ + (latest_return_ - ucb_pair.action_value_) / ucb_pair.action_count_;
        VLOG_EVERY_N(6, 10) << "Agent "<< agent_idx_ <<", Action reward, action " << collected_cost_.first << ", Q(s,a) = " << ucb_pair.action_value_;
        total_node_visits_ += 1;
        value_ = value_ + (latest_return_ - value_) / total_node_visits_;
    }

    void set_heuristic_estimate(const Reward& accum_rewards, const EgoCosts& accum_ego_cost)
    {
      this->set_heuristic_estimate_from_backpropagated(accum_rewards);
    }

    void set_heuristic_estimate_from_backpropagated(const Reward& backpropagated) {
       value_ = backpropagated;
    }

    std::string print_node_information() const
    {
        std::stringstream ss;
        ss << std::setprecision(2) << "V=" << value_ << ", N=" << total_node_visits_;
        return ss.str();
    }

    std::string print_edge_information(const ActionIdx& action ) const
    {
        std::stringstream ss;
        auto action_it = ucb_statistics_.find(action);
        if(action_it != ucb_statistics_.end()) {
            ss << std::setprecision(2) <<  "a=" << int(action) << ", N=" << action_it->second.action_count_ << ", V=" << action_it->second.action_value_;
        }
        return ss.str();
    }

    Reward get_normalized_ucb_value(const ActionIdx& action) const {
      double action_value_normalized =  (ucb_statistics_.at(action).action_value_-lower_bound)/(upper_bound-lower_bound); 
      MCTS_EXPECT_TRUE(action_value_normalized>=0);
      MCTS_EXPECT_TRUE(action_value_normalized<=1);
      return action_value_normalized;
    }

    Reward get_reward_lower_bound() const {
      return lower_bound;
    }

    Reward get_reward_upper_bound() const {
      return upper_bound;
    }

    ActionIdx calculate_ucb_and_max_action(const UcbStatistics& ucb_statistics, std::unordered_map<ActionIdx, double>& values) const {
        values.reserve(ucb_statistics.size());
        ActionIdx maximizing_action = 0;
        double max_value = std::numeric_limits<double>::min();

        for (const auto ucb_pair : ucb_statistics) {   
            double action_value_normalized = (ucb_pair.second.action_value_-lower_bound)/(upper_bound-lower_bound); 
            MCTS_EXPECT_TRUE(action_value_normalized>=0);
            MCTS_EXPECT_TRUE(action_value_normalized<=1);
            values[ucb_pair.first] = action_value_normalized + 2 * exploration_constant * sqrt( (2* log(total_node_visits_)) / ( ucb_pair.second.action_count_)  );
            if (values[ucb_pair.first] > max_value) {
                max_value = values[ucb_pair.first];
                maximizing_action = ucb_pair.first;
            }
        }
        return maximizing_action;
    }

    std::string sprintf() const {
        return UctStatistic::ucb_stats_to_string(ucb_statistics_);
    }

    static std::string ucb_stats_to_string(const UcbStatistics& ucb_stats) {
      std::stringstream ss;
      for(const auto& ucb_stat : ucb_stats) {
          ss << "a=" <<  ucb_stat.first << ", q=" << ucb_stat.second.action_value_ << ", n=" << ucb_stat.second.action_count_ << "|";
      }
      return ss.str();
    }

     inline bool require_progressive_widening_total() const {
        const auto widening_term = progressive_widening_k * std::pow(total_node_visits_,
                progressive_widening_alpha);
                // At least one action should be expanded for each hypothesis,
                // otherwise use progressive widening based on total visit and action count
        return num_expanded_actions() <= widening_term && num_expanded_actions() < num_actions_;
    }

    // How many children exist based on specific hypothesis
    inline unsigned int num_expanded_actions() const {
        return ucb_statistics_.size();
    }


protected:
    double value_;
    double latest_return_;   // tracks the return during backpropagation
    
    UcbStatistics ucb_statistics_; // first: action selection count, action-value
    unsigned int total_node_visits_;
    std::vector<ActionIdx> unexpanded_actions_; // contains all action indexes which have not been expanded yet

    // PARAMS
    double upper_bound;
    double lower_bound;
    double k_discount_factor;
    double exploration_constant;

    double progressive_widening_k;
    double progressive_widening_alpha;

};

} // namespace mcts


#endif