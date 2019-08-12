// Copyright (c) 2019 Julian Bernhard
// 
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
// ========================================================

#ifndef MCTS_RANDOM_GENERATOR_H
#define MCTS_RANDOM_GENERATOR_H

#include <random>


namespace mcts {

    class RandomGenerator {
    public:
        static std::mt19937 random_generator_;
    public:
        RandomGenerator() {}

        ~RandomGenerator() {}

    };
} // namespace mcts



#endif