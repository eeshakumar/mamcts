package(default_visibility = ["//visibility:public"])


# the following rules are only for testing purposes
cc_binary(
  name = "mamcts.so",
  srcs = ["module.cpp",
          "common.hpp",
          "define_mamcts.hpp",
          "define_mamcts.cpp",
          "define_environments.hpp",
          "define_environments.cpp",
          "define_crossing_state.hpp"
  ],
  deps = [
    "//mcts:mamcts",
    "//environments:crossing_state",
    "@python_linux//:python-lib",
    "@pybind11//:pybind11",
    "@com_github_eigen_eigen//:eigen", 
  ],
  linkshared = 1,
  linkstatic = 1,
  copts=["-O3"],
)

