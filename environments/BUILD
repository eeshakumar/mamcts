cc_library(
    name = "crossing_state",
    hdrs = [
        "crossing_state.h",
        "crossing_state_common.h",
        "crossing_state_parameters.h",
        "crossing_state_agent_policy.h",
        "viewer.h",
        "crossing_state_episode_runner.h"
    ],
    deps = [
        "//mcts:mamcts",
    ],
    visibility = ["//visibility:public"],
)

py_library(
    name = "pyviewer",
    srcs = ["pyviewer.py"],
    data = ['//python/bindings:mamcts.so'],
    imports = ['../../python/bindings'],
    visibility = ["//visibility:public"],
)