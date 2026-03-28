#include "chess.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int file_rank_to_sq(int file, int rank) {
    return rank * 8 + file;
}

static int sq_to_file(int sq) {
    return sq % 8;
}

static int sq_to_rank(int sq) {
    return sq / 8;
}

static int piece_from_fen(char c) {
    switch (c) {
        case 'P': return WPAWN;
        case 'N': return WKNIGHT;
        case 'B': return WBISHOP;
        case 'R': return WROOK;
        case 'Q': return WQUEEN;
        case 'K': return WKING;
        case 'p': return BPAWN;
        case 'n': return BKNIGHT;
        case 'b': return BBISHOP;
        case 'r': return BROOK;
        case 'q': return BQUEEN;
        case 'k': return BKING;
        default: return EMPTY;
    }
}

static char piece_to_char(int piece) {
    switch (piece) {
        case WPAWN: return 'P';
        case WKNIGHT: return 'N';
        case WBISHOP: return 'B';
        case WROOK: return 'R';
        case WQUEEN: return 'Q';
        case WKING: return 'K';
        case BPAWN: return 'p';
        case BKNIGHT: return 'n';
        case BBISHOP: return 'b';
        case BROOK: return 'r';
        case BQUEEN: return 'q';
        case BKING: return 'k';
        default: return '.';
    }
}

void board_set_startpos(Board *board) {
    (void)board_from_fen(board, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    board->king_sq[0] = 4;   /* White king on e1 */
    board->king_sq[1] = 60;  /* Black king on e8 */
}

int board_from_fen(Board *board, const char *fen) {
    int rank = 7;
    int file = 0;
    const char *p = fen;

    memset(board, 0, sizeof(*board));
    board->side_to_move = WHITE;
    board->castling_rights = 0;
    board->en_passant_sq = NO_SQUARE;
    board->king_sq[0] = NO_SQUARE;
    board->king_sq[1] = NO_SQUARE;

    while (*p != '\0' && *p != ' ') {
        if (*p == '/') {
            rank--;
            file = 0;
        } else if (isdigit((unsigned char)*p)) {
            file += *p - '0';
        } else {
            if (rank < 0 || file > 7) {
                return 0;
            }
            int sq = file_rank_to_sq(file, rank);
            int piece = piece_from_fen(*p);
            board->squares[sq] = piece;
            /* Track king positions */
            if (piece == WKING) {
                board->king_sq[0] = sq;
            } else if (piece == BKING) {
                board->king_sq[1] = sq;
            }
            file++;
        }
        p++;
    }

    if (*p == ' ') {
        p++;
        if (*p == 'b') {
            board->side_to_move = BLACK;
        } else {
            board->side_to_move = WHITE;
        }

        while (*p != '\0' && *p != ' ') {
            p++;
        }
    }

    if (*p == ' ') {
        p++;
        if (*p == '-') {
            p++;
        } else {
            while (*p != '\0' && *p != ' ') {
                switch (*p) {
                    case 'K': board->castling_rights |= CASTLE_WHITE_K; break;
                    case 'Q': board->castling_rights |= CASTLE_WHITE_Q; break;
                    case 'k': board->castling_rights |= CASTLE_BLACK_K; break;
                    case 'q': board->castling_rights |= CASTLE_BLACK_Q; break;
                    default: break;
                }
                p++;
            }
        }
    }

    if (*p == ' ') {
        p++;
        if (*p != '-' && isalpha((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
            int file_idx = p[0] - 'a';
            int rank_idx = p[1] - '1';
            if (file_idx >= 0 && file_idx < 8 && rank_idx >= 0 && rank_idx < 8) {
                board->en_passant_sq = file_rank_to_sq(file_idx, rank_idx);
            }
        }
    }

    return 1;
}

void board_print(const Board *board) {
    int rank;
    int file;

    printf("\n");
    for (rank = 7; rank >= 0; rank--) {
        printf("%d ", rank + 1);
        for (file = 0; file < 8; file++) {
            int sq = file_rank_to_sq(file, rank);
            printf("%c ", piece_to_char(board->squares[sq]));
        }
        printf("\n");
    }
    printf("  a b c d e f g h\n");
    printf("Side to move: %s\n", board->side_to_move == WHITE ? "white" : "black");
    printf("Castling: %c%c%c%c\n",
           (board->castling_rights & CASTLE_WHITE_K) ? 'K' : '-',
           (board->castling_rights & CASTLE_WHITE_Q) ? 'Q' : '-',
           (board->castling_rights & CASTLE_BLACK_K) ? 'k' : '-',
           (board->castling_rights & CASTLE_BLACK_Q) ? 'q' : '-');
    if (board->en_passant_sq == NO_SQUARE) {
        printf("En passant: -\n");
    } else {
        printf("En passant: %c%c\n",
               (char)('a' + sq_to_file(board->en_passant_sq)),
               (char)('1' + sq_to_rank(board->en_passant_sq)));
    }
}

void move_to_uci(const Move *move, char out[6]) {
    int from_file = move->from % 8;
    int from_rank = move->from / 8;
    int to_file = move->to % 8;
    int to_rank = move->to / 8;

    out[0] = (char)('a' + from_file);
    out[1] = (char)('1' + from_rank);
    out[2] = (char)('a' + to_file);
    out[3] = (char)('1' + to_rank);

    if (move->promotion != EMPTY) {
        int promo = move->promotion;
        if (promo < 0) {
            promo = -promo;
        }
        switch (promo) {
            case WQUEEN: out[4] = 'q'; break;
            case WROOK: out[4] = 'r'; break;
            case WBISHOP: out[4] = 'b'; break;
            case WKNIGHT: out[4] = 'n'; break;
            default: out[4] = 'q'; break;
        }
        out[5] = '\0';
    } else {
        out[4] = '\0';
    }
}

int parse_uci_move(const Board *board, const char *uci, Move *out_move) {
    Move moves[MAX_MOVES];
    int count = generate_legal_moves(board, moves);
    int i;
    char candidate[6];

    for (i = 0; i < count; i++) {
        move_to_uci(&moves[i], candidate);
        if (strcmp(candidate, uci) == 0) {
            *out_move = moves[i];
            return 1;
        }
    }

    return 0;
}
