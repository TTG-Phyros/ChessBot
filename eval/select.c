#include "chess.h"

#include <string.h>

typedef enum {
    EVAL_PROFILE_BASIC = 0,
    EVAL_PROFILE_ADVANCED = 1,
    EVAL_PROFILE_TACTICAL = 2
} EvalProfile;

static EvalProfile current_profile = EVAL_PROFILE_BASIC;

void set_evaluation_profile(const char *profile) {
    if (!profile || profile[0] == '\0') {
        current_profile = EVAL_PROFILE_BASIC;
        return;
    }

    if (strcmp(profile, "advanced") == 0) {
        current_profile = EVAL_PROFILE_ADVANCED;
        return;
    }

    if (strcmp(profile, "tactical") == 0) {
        current_profile = EVAL_PROFILE_TACTICAL;
        return;
    }

    current_profile = EVAL_PROFILE_BASIC;
}

const char *get_evaluation_profile(void) {
    if (current_profile == EVAL_PROFILE_ADVANCED) {
        return "advanced";
    }
    if (current_profile == EVAL_PROFILE_TACTICAL) {
        return "tactical";
    }
    return "basic";
}

int evaluate_board(const Board *board) {
    if (current_profile == EVAL_PROFILE_ADVANCED) {
        return evaluate_board_advanced(board);
    }
    if (current_profile == EVAL_PROFILE_TACTICAL) {
        return evaluate_board_tactical(board);
    }
    return evaluate_board_basic(board);
}
