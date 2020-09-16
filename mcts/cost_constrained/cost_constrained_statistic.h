// Copyright (c) 2019 Julian Bernhard
// 
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
// ========================================================

#ifndef UCT_COST_CONSTRAINED_STATISTIC_H
#define UCT_COST_CONSTRAINED_STATISTIC_H

#include "mcts/statistics/uct_statistic.h"
#include "mcts/cost_constrained/risk_uct_statistic.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cmath>

namespace mcts {

// A upper confidence bound implementation
class CostConstrainedStatistic : public mcts::NodeStatistic<CostConstrainedStatistic>, mcts::RandomGenerator
{
public:
    MCTS_TEST

    CostConstrainedStatistic(ActionIdx num_actions, AgentIdx agent_idx, const MctsParameters & mcts_parameters) :
             NodeStatistic<CostConstrainedStatistic>(num_actions, agent_idx, mcts_parameters),
             RandomGenerator(mcts_parameters.RANDOM_SEED),
             reward_statistic_(num_actions, agent_idx,  make_reward_statistic_parameters(mcts_parameters)),
             cost_statistic_(num_actions, agent_idx, make_cost_statistic_parameters(mcts_parameters)),
             unexpanded_actions_(num_actions),
             mean_step_costs_(),
             lambda(mcts_parameters.cost_constrained_statistic.LAMBDA),
             kappa(mcts_parameters.cost_constrained_statistic.KAPPA),
             action_filter_factor(mcts_parameters.cost_constrained_statistic.ACTION_FILTER_FACTOR),
             cost_constraint(mcts_parameters.cost_constrained_statistic.COST_CONSTRAINT)
             {
                 // initialize action indexes from 0 to (number of actions -1)
                 std::iota(unexpanded_actions_.begin(), unexpanded_actions_.end(), 0);
                 for(const auto& action : unexpanded_actions_) {
                    mean_step_costs_[action] = 0.0f;
                 }
             }

    ~CostConstrainedStatistic() {};

    template <class S>
    ActionIdx choose_next_action(const S& state) {
       if(unexpanded_actions_.empty())
        {
          // Expansion policy does consider node counts
          return greedy_policy(kappa, action_filter_factor).first;
        } else {
            // Select randomly an unexpanded action
            std::uniform_int_distribution<ActionIdx> random_action_selection(0,unexpanded_actions_.size()-1);
            ActionIdx array_idx = random_action_selection(random_generator_);
            ActionIdx selected_action = unexpanded_actions_[array_idx];
            unexpanded_actions_.erase(unexpanded_actions_.begin()+array_idx);
            return selected_action;
        }
    }

    Policy get_policy() const {
      return greedy_policy(0.0f, action_filter_factor).second;
    }

    ActionIdx get_best_action() const {
      return greedy_policy(0.0f, action_filter_factor).first;
    }

    bool policy_is_ready() const {
      return unexpanded_actions_.empty();
    }

    typedef std::pair<ActionIdx, Policy> PolicySampled;
    PolicySampled greedy_policy(const double kappa_local, const double action_filter_factor_local) const {
      // Greedy Policy
      std::vector<double> ucb_values;
      calculate_ucb_values(ucb_values, kappa_local);
      
      const auto feasible_actions = filter_feasible_actions(ucb_values, action_filter_factor_local);
      auto policy = solve_LP_and_sample(feasible_actions);
      return policy;
    }

    Cost calc_updated_constraint_based_on_policy(const PolicySampled& policy, const Cost& current_constraint) const {
      double other_actions_costs = 0.0f;
      for(const auto& action_pair : policy.second) {
        if(action_pair.first == policy.first) {
          continue;
        }
        other_actions_costs += action_pair.second * cost_statistic_.ucb_statistics_.at(action_pair.first).action_value_;
      }
      return (current_constraint - policy.second.at(policy.first)*mean_step_costs_.at(policy.first) - other_actions_costs) /
              (cost_statistic_.k_discount_factor * policy.second.at(policy.first));
    }

    void calculate_ucb_values(std::vector<double>& values, const double& kappa_local) const {
      const auto& reward_stats = reward_statistic_.ucb_statistics_;
      const auto& cost_stats = cost_statistic_.ucb_statistics_;
      MCTS_EXPECT_TRUE(reward_stats.size() ==  cost_stats.size());

      values.resize(reward_statistic_.ucb_statistics_.size(), 0.0f);
      for (size_t idx = 0; idx < reward_stats.size(); ++idx)
      {
          double cost_value_normalized = cost_statistic_.get_normalized_ucb_value(idx);
          double reward_value_normalized = reward_statistic_.get_normalized_ucb_value(idx);

          const auto exploration_term = kappa_local * 
              sqrt( log(reward_statistic_.total_node_visits_) / ( reward_statistic_.ucb_statistics_.at(idx).action_count_));
          values[idx] = reward_value_normalized - lambda * cost_value_normalized 
                 + (std::isnan(exploration_term) ? std::numeric_limits<double>::max() : exploration_term);
      }
    }

    std::vector<ActionIdx> filter_feasible_actions(const std::vector<double>& values,
                                           const double& action_filter_factor_local) const {
      std::vector<ActionIdx> filtered_actions;
      const ActionIdx maximizing_action = std::distance(values.begin(), std::max_element(values.begin(), values.end()));
      const Reward max_val = values[maximizing_action];
      const double node_counts_maximizing = (reward_statistic_.ucb_statistics_.at(maximizing_action).action_count_ == 0) ? std::numeric_limits<double>::max() :
                                 sqrt( log( reward_statistic_.ucb_statistics_.at(maximizing_action).action_count_) /
                                                  ( reward_statistic_.ucb_statistics_.at(maximizing_action).action_count_) );
      for (size_t action_idx = 0; action_idx < values.size(); ++action_idx) {
          const double value_difference = std::abs(values[action_idx] - max_val);
          const double node_count_relations = ( reward_statistic_.ucb_statistics_.at(action_idx).action_count_ == 0) ? std::numeric_limits<double>::max() :
                                 sqrt( log( reward_statistic_.ucb_statistics_.at(action_idx).action_count_) /
                                                  ( reward_statistic_.ucb_statistics_.at(action_idx).action_count_) ) + 
                                              node_counts_maximizing;
          if(value_difference <= action_filter_factor_local * node_count_relations) {
            filtered_actions.push_back(action_idx);
          }
      }
      return filtered_actions;
    }

    PolicySampled solve_LP_and_sample(const std::vector<ActionIdx>& feasible_actions) const {
      // Solved for K=1
      const auto& cost_stats = cost_statistic_.ucb_statistics_;

      ActionIdx maximizing_action = feasible_actions.at(0);
      ActionIdx minimizing_action = feasible_actions.at(0);
      for (const auto& feasible_action : feasible_actions ) {
        if(cost_stats.at(feasible_action).action_value_ > cost_stats.at(maximizing_action).action_value_) {
          maximizing_action = feasible_action;
          continue;
        }
        if(cost_stats.at(feasible_action).action_value_ < cost_stats.at(minimizing_action).action_value_) {
          minimizing_action = feasible_action;
          continue;
        }
      }

      Policy stochastic_policy;
      for ( const auto action : cost_stats) {
         stochastic_policy[action.first] = 0.0f;
      }
      if(minimizing_action == maximizing_action) {
        stochastic_policy[minimizing_action] = 1.0f;
        return std::make_pair(minimizing_action, stochastic_policy);
      }

      // Three cases
      const double max_val = cost_stats.at(maximizing_action).action_value_;
      const double min_val = cost_stats.at(minimizing_action).action_value_;
      if( min_val >= cost_constraint) {
          // amin gets probability one, amax gets probability zero
          stochastic_policy[minimizing_action] = 1.0f;
          return std::make_pair(minimizing_action, stochastic_policy);
      } else if( max_val <= cost_constraint) {
         // amax gets probability one, amin gets probability zero
         stochastic_policy[maximizing_action] = 1.0f;
         return std::make_pair(maximizing_action, stochastic_policy);
      } else {
         const double probability_maximizer = (cost_constraint - min_val) / (max_val - min_val);
         std::uniform_real_distribution<double> unif(0, 1);
         stochastic_policy[maximizing_action] = probability_maximizer;
         stochastic_policy[minimizing_action] = 1 - probability_maximizer;
         double sample = unif(random_generator_);
         if(sample <= probability_maximizer) {
           return std::make_pair(maximizing_action, stochastic_policy);
         } else {
           return std::make_pair(minimizing_action, stochastic_policy);
         }
      }
    }

    static double calculate_next_lambda(const double& current_lambda,
                                        const double& gradient_update_step,
                                        const double& cost_constraint,
                                        const double& tau_gradient_clip,
                                        const CostConstrainedStatistic& root_statistic,
                                        const double& discount_factor) {
        const ActionIdx policy_sampled_action = root_statistic.greedy_policy(0.0f, 0.0f).first;
        const double normalized_ucb_sample_action = root_statistic.get_normalized_cost_action_value(policy_sampled_action);
        const double gradient = (normalized_ucb_sample_action - cost_constraint);
        VLOG_EVERY_N(5, 10) << "Norm. UCBSampled: " << normalized_ucb_sample_action << ", grad = "
                             << gradient << ", step = " << gradient_update_step ;
        const double new_lambda = current_lambda + gradient_update_step * gradient;
        const double clip_upper_limit = (root_statistic.reward_statistic_.upper_bound - root_statistic.reward_statistic_.lower_bound) /
                                         (tau_gradient_clip * ( 1 - discount_factor));
        const double clipped_new_lambda = std::min(std::max(new_lambda, double(0.0f)), clip_upper_limit);
        return clipped_new_lambda;
    }

    void update_from_heuristic(const NodeStatistic<CostConstrainedStatistic>& heuristic_statistic)
    {
      const CostConstrainedStatistic& statistic_impl = heuristic_statistic.impl();

      const auto heuristic_reward_value = statistic_impl.reward_statistic_.value_;
      reward_statistic_.update_from_heuristic_from_backpropagated(heuristic_reward_value);

      const auto heuristic_cost_value = statistic_impl.cost_statistic_.value_;
      cost_statistic_.update_from_heuristic_from_backpropagated(heuristic_cost_value);
    }

    void update_statistic(const NodeStatistic<CostConstrainedStatistic>& changed_child_statistic) {
      const CostConstrainedStatistic& statistic_impl = changed_child_statistic.impl();

      const auto reward_latest_return = statistic_impl.reward_statistic_.latest_return_;
      reward_statistic_.collected_reward_ = collected_reward_;
      reward_statistic_.update_statistics_from_backpropagated(reward_latest_return);

      const auto cost_latest_return = statistic_impl.cost_statistic_.latest_return_;
      cost_statistic_.collected_reward_ = collected_cost_;
      cost_statistic_.collected_action_transition_counts_ = collected_action_transition_counts_;
      cost_statistic_.update_statistics_from_backpropagated(cost_latest_return);

      mean_step_costs_[collected_cost_.first] += (collected_cost_.second - mean_step_costs_[collected_cost_.first]) /
                                                   (cost_statistic_.ucb_statistics_[collected_cost_.first].action_count_);
    }

    void set_heuristic_estimate(const Reward& accum_rewards, const Cost& accum_ego_cost)
    {
       reward_statistic_.set_heuristic_estimate_from_backpropagated(accum_rewards);
       cost_statistic_.set_heuristic_estimate_from_backpropagated(accum_ego_cost);
    }

    std::string print_node_information() const {
        return "";
    }

    static std::string print_policy(const Policy& policy) {
      std::stringstream ss;
      ss << "Policy: ";
      for (const auto& action_pair : policy) {
        ss << "P(a=" << action_pair.first << ") = " << action_pair.second << ", ";
      }
      return ss.str();
    }

    Cost expected_policy_cost(const Policy& policy) const {
      Cost expected_cost = 0.0;
      const auto& cost_stats = cost_statistic_.ucb_statistics_;
      for(const auto& cost_stat : cost_stats) {
        expected_cost += policy.at(cost_stat.first) * cost_stat.second.action_value_;
      } 
      return expected_cost;
    }

    std::string print_edge_information(const ActionIdx& action) const {
        const auto& reward_stats = reward_statistic_.ucb_statistics_;
        const auto& cost_stats = cost_statistic_.ucb_statistics_;
        std::vector<Reward> ucb_values;
        calculate_ucb_values(ucb_values, 0.0f);
        std::stringstream ss;
        ss  << "Reward stats: " << UctStatistic::ucb_stats_to_string(reward_stats) << "\n"
            << "Cost stats: " << UctStatistic::ucb_stats_to_string(cost_stats) << "\n"
            << "Lambda:" << lambda << "\n"
            << "Ucb values: " << ucb_values << "\n"
            << "Mean step cost: C(a=" << action << ") = " << mean_step_costs_.at(action) << "\n";
        return ss.str();
    }

    Reward get_normalized_cost_action_value(const ActionIdx& action) const {
      return cost_statistic_.get_normalized_ucb_value(action);
    }

    MctsParameters make_cost_statistic_parameters(const MctsParameters& mcts_parameters) const {
      MctsParameters cost_statistic_parameters(mcts_parameters);
      cost_statistic_parameters.uct_statistic.LOWER_BOUND = mcts_parameters.cost_constrained_statistic.COST_LOWER_BOUND;
      cost_statistic_parameters.uct_statistic.UPPER_BOUND = mcts_parameters.cost_constrained_statistic.COST_UPPER_BOUND;

      cost_statistic_parameters.DISCOUNT_FACTOR = 1.0; //< For risk estimation, we do not apply a discount
      return cost_statistic_parameters;
    }

    MctsParameters make_reward_statistic_parameters(const MctsParameters& mcts_parameters) const {
      MctsParameters reward_statistic_parameters(mcts_parameters);
      reward_statistic_parameters.uct_statistic.LOWER_BOUND = mcts_parameters.cost_constrained_statistic.REWARD_LOWER_BOUND;
      reward_statistic_parameters.uct_statistic.UPPER_BOUND = mcts_parameters.cost_constrained_statistic.REWARD_UPPER_BOUND;
      return reward_statistic_parameters;
    }

    const UctStatistic::UcbStatistics& get_cost_ucb_statistics() const {
      return cost_statistic_.ucb_statistics_;
    }

    const UctStatistic::UcbStatistics& get_reward_ucb_statistics() const {
      return reward_statistic_.ucb_statistics_;
    }

    std::string sprintf() const {
      std::stringstream ss;
      ss << "Cost statistic: " << cost_statistic_.sprintf();
      return ss.str();
    }


private:

    UctStatistic reward_statistic_;
    UctStatistic cost_statistic_;
    std::vector<ActionIdx> unexpanded_actions_;
    std::unordered_map<ActionIdx, Cost> mean_step_costs_;

    const double& lambda;
    const double kappa;
    const double action_filter_factor;
    const double cost_constraint;
};

template <>
void NodeStatistic<CostConstrainedStatistic>::update_statistic_parameters(MctsParameters& parameters,
                                            const CostConstrainedStatistic& root_statistic,
                                            const unsigned int& current_iteration) {
  if(!root_statistic.policy_is_ready()) {
    return;
  }
  const double current_lambda = parameters.cost_constrained_statistic.LAMBDA;
  const double gradient_update_step = parameters.cost_constrained_statistic.GRADIENT_UPDATE_STEP/(0.1*current_iteration + 1);
  const double cost_constraint = parameters.cost_constrained_statistic.COST_CONSTRAINT;
  const double tau_gradient_clip = parameters.cost_constrained_statistic.TAU_GRADIENT_CLIP;
  const double new_lambda =  CostConstrainedStatistic::calculate_next_lambda(current_lambda,
                                                                            gradient_update_step,
                                                                            cost_constraint,
                                                                            tau_gradient_clip,
                                                                            root_statistic,
                                                                            parameters.DISCOUNT_FACTOR);
  parameters.cost_constrained_statistic.LAMBDA = new_lambda;
  VLOG_EVERY_N(5, 100) << "Updated lambda from " << current_lambda << 
    " to " << new_lambda << " in iteration " << current_iteration;
}

} // namespace mcts

#endif