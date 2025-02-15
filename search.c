#include "search.h"
#include "nnue/nnue.h"
#include "timeman.h"
#include "transposition.h"
#include "Fathom/tbprobe.h"
#include "syzygy.h"
#include "uci.h"
#include "moveOrderData.h"
#include <stdio.h>
#include <pthread.h>

#define DEF_ASPWINDOW (1700)
#define NO_MOVE (-15)

#define DEF_ALPHA (-5000000)
#define DEF_BETA (5000000)

int aspwindow = DEF_ASPWINDOW;
int tbsearch = 0;

int selDepth = 0;

long nodes = 0;
long qnodes = 0;
long tbHits = 0;

const int mvv_lva[12][12] = {
        105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605,
        104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604,
        103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603,
        102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602,
        101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601,
        100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600,

        105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605,
        104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604,
        103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603,
        102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602,
        101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601,
        100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600
};

int historyCount;
int killer_moves[max_ply][2];
float history_moves[12][64][64];

int follow_pv, found_pv;

typedef struct MoveEval MoveEval;

struct MoveEval{
    int move;
    int eval;
};

static inline void swap(int* a, int* b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

void insertion_sort(int *scores, moveList *movearr){
    int i = 1;
    while (i < movearr->count){
        int j = i;
        while (j > 0 && scores[j-1] < scores[j]) {
            swap(&scores[j], &scores[j - 1]);
            swap(&movearr->moves[j], &movearr->moves[j - 1]);
            j--;
        }
        i++;
    }
}

U64 pastPawnMasks[2][64] = {
        {0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
         0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
         0x3ULL, 0x7ULL, 0xeULL, 0x1cULL,
         0x38ULL, 0x70ULL, 0xe0ULL, 0xc0ULL,
         0x303ULL, 0x707ULL, 0xe0eULL, 0x1c1cULL,
         0x3838ULL, 0x7070ULL, 0xe0e0ULL, 0xc0c0ULL,
         0x30303ULL, 0x70707ULL, 0xe0e0eULL, 0x1c1c1cULL,
         0x383838ULL, 0x707070ULL, 0xe0e0e0ULL, 0xc0c0c0ULL,
         0x3030303ULL, 0x7070707ULL, 0xe0e0e0eULL, 0x1c1c1c1cULL,
         0x38383838ULL, 0x70707070ULL, 0xe0e0e0e0ULL, 0xc0c0c0c0ULL,
         0x303030303ULL, 0x707070707ULL, 0xe0e0e0e0eULL, 0x1c1c1c1c1cULL,
         0x3838383838ULL, 0x7070707070ULL, 0xe0e0e0e0e0ULL, 0xc0c0c0c0c0ULL,
         0x30303030303ULL, 0x70707070707ULL, 0xe0e0e0e0e0eULL, 0x1c1c1c1c1c1cULL,
         0x383838383838ULL, 0x707070707070ULL, 0xe0e0e0e0e0e0ULL, 0xc0c0c0c0c0c0ULL,
         0x3030303030303ULL, 0x7070707070707ULL, 0xe0e0e0e0e0e0eULL, 0x1c1c1c1c1c1c1cULL,
         0x38383838383838ULL, 0x70707070707070ULL, 0xe0e0e0e0e0e0e0ULL, 0xc0c0c0c0c0c0c0ULL,},

        {0x303030303030300ULL, 0x707070707070700ULL, 0xe0e0e0e0e0e0e00ULL, 0x1c1c1c1c1c1c1c00ULL,
         0x3838383838383800ULL, 0x7070707070707000ULL, 0xe0e0e0e0e0e0e000ULL, 0xc0c0c0c0c0c0c000ULL,
         0x303030303030000ULL, 0x707070707070000ULL, 0xe0e0e0e0e0e0000ULL, 0x1c1c1c1c1c1c0000ULL,
         0x3838383838380000ULL, 0x7070707070700000ULL, 0xe0e0e0e0e0e00000ULL, 0xc0c0c0c0c0c00000ULL,
         0x303030303000000ULL, 0x707070707000000ULL, 0xe0e0e0e0e000000ULL, 0x1c1c1c1c1c000000ULL,
         0x3838383838000000ULL, 0x7070707070000000ULL, 0xe0e0e0e0e0000000ULL, 0xc0c0c0c0c0000000ULL,
         0x303030300000000ULL, 0x707070700000000ULL, 0xe0e0e0e00000000ULL, 0x1c1c1c1c00000000ULL,
         0x3838383800000000ULL, 0x7070707000000000ULL, 0xe0e0e0e000000000ULL, 0xc0c0c0c000000000ULL,
         0x303030000000000ULL, 0x707070000000000ULL, 0xe0e0e0000000000ULL, 0x1c1c1c0000000000ULL,
         0x3838380000000000ULL, 0x7070700000000000ULL, 0xe0e0e00000000000ULL, 0xc0c0c00000000000ULL,
         0x303000000000000ULL, 0x707000000000000ULL, 0xe0e000000000000ULL, 0x1c1c000000000000ULL,
         0x3838000000000000ULL, 0x7070000000000000ULL, 0xe0e0000000000000ULL, 0xc0c0000000000000ULL,
         0x300000000000000ULL, 0x700000000000000ULL, 0xe00000000000000ULL, 0x1c00000000000000ULL,
         0x3800000000000000ULL, 0x7000000000000000ULL, 0xe000000000000000ULL, 0xc000000000000000ULL,
         0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
         0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,}
};

#if defined(AVX) || defined(AVX2)
//taken from https://coderedirect.com/questions/143686/how-to-sum-m256-horizontally
float sum8(__m256 x) {
    const __m128 hiQuad = _mm256_extractf128_ps(x, 1);
    const __m128 loQuad = _mm256_castps256_ps128(x);
    const __m128 sumQuad = _mm_add_ps(loQuad, hiQuad);
    const __m128 loDual = sumQuad;
    const __m128 hiDual = _mm_movehl_ps(sumQuad, sumQuad);
    const __m128 sumDual = _mm_add_ps(loDual, hiDual);
    const __m128 lo = sumDual;
    const __m128 hi = _mm_shuffle_ps(sumDual, sumDual, 0x1);
    const __m128 sum = _mm_add_ss(lo, hi);
    return _mm_cvtss_f32(sum);
}

//info score cp 24 depth 9 seldepth 21 nodes 1090335 qnodes 568126 tbhits 0 time 1496 pv e2e4 e7e6 d2d4 d7d5 b1d2 d5e4 d2e4 g8f6 e4f6


//taken from https://stackoverflow.com/questions/48811369/how-to-use-bits-in-a-byte-to-set-dwords-in-ymm-register-without-avx2-inverse-o
__m256i inverse_movemask_ps(int bitmap) {
    const __m256 exponent = _mm256_set1_ps(1.0f);
    const __m256 bit_select = _mm256_castsi256_ps(
            _mm256_set_epi32(
                    0x3f800000 + (1<<7), 0x3f800000 + (1<<6),
                    0x3f800000 + (1<<5), 0x3f800000 + (1<<4),
                    0x3f800000 + (1<<3), 0x3f800000 + (1<<2),
                    0x3f800000 + (1<<1), 0x3f800000 + (1<<0)
            ));

    __m256  bcast = _mm256_castsi256_ps(_mm256_set1_epi32(bitmap));
    __m256  ored  = _mm256_or_ps(bcast, exponent);
    __m256  isolated = _mm256_and_ps(ored, bit_select);
    return (__m256i)_mm256_cmp_ps(isolated, bit_select, _CMP_EQ_OQ);
}
#endif

float fastGetScoreFromMoveTable(U64 bitboard, const float *bbPart){

#if defined(AVX) || defined(AVX2)
    float score = 0;

    for (int i = 0; i < 64; i += 8) {
        __m256i _mask_x = inverse_movemask_ps((int)(bitboard >> i));

        __m256 _x = _mm256_maskload_ps(bbPart + i, _mask_x);

        score += sum8(_x);
    }

    return score;

#else

    float score = 0;

    int count = count_bits(bitboard);

    for (int i = 0; i < count; ++i) {
        int bit = bsf(bitboard);

        score += bbPart[bit];

        pop_bit(bitboard, bit);
    }

    return score;

#endif
}

float getScoreFromMoveTable(U64 bitboard, const float *bbPart){

#if defined(AVX) || defined(AVX2)
    U64 ybb = ~bitboard;

    float score = 0;

    for (int i = 0; i < 64; i += 8) {
        __m256i _mask_x = inverse_movemask_ps((int)(bitboard >> i));
        __m256i _mask_y = inverse_movemask_ps((int)(ybb >> i));

        __m256 _x = _mm256_maskload_ps(bbPart + i, _mask_x);
        __m256 _y = _mm256_maskload_ps(bbPart + i, _mask_y);

        score += sum8(_x);
        score -= sum8(_y);
    }

    return score;

#else

    float score = 0;

    for (int i = 0; i < 64; ++i) {
        if (get_bit(bitboard, i)){
            score += bbPart[i];
        } else {
            score -= bbPart[i];
        }
    }

    return score;

#endif

}


int score_move(int move, const int *hashmove, Board *board){

    if (found_pv && !board->helperThread){
        if (move == board->pv_line.moves[board->ply]){
            found_pv = 0;
            return 200000;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (hashmove[i] == -15 || hashmove[i] == 0)
            break;
        if (hashmove[i] == move) {
            return 100000 - i;
        }
    }

    if (getcapture(move)){
        int start_piece, end_piece;
        int target_piece = P;

        int target_square = gettarget(move);

        if (board->side == white) {start_piece = p; end_piece = k;}
        else{start_piece = P; end_piece = K;}

        for (int bb_piece = start_piece; bb_piece < end_piece; bb_piece++){
            if (get_bit(board->bitboards[bb_piece], target_square)){
                target_piece = bb_piece;
                break;
            }
        }

        return mvv_lva[getpiece(move)][target_piece] + 10000;
    } else {
        if (move == killer_moves[board->ply][0]){
            return(8000);
        }
        if (move == killer_moves[board->ply][1]){
            return(7000);
        }

	if (board->searchDepth == 0){ return 0; }

        float score = 0;

        int piece = getpiece(move);
        int target = gettarget(move);

        float *dataPart = &moveOrderData[piece][target][0][0];
        char *wspart = &moveOrderWorthSearching[piece][target][0];

        for (int bb = 0; bb < 14; bb++){
            if (bb == P || bb == p || bb == 12 || bb == 13) {

                float *bbPart = &dataPart[bb * 64];
                U64 bitboard;

                //there are two other bitboards that represent what squares each side attacks
                if (bb < 12)
                    bitboard = board->bitboards[bb];
                else
                    bitboard = board->unprotectedPieces[bb - 12];

                if (board->ply < 5) {
                    score += getScoreFromMoveTable(bitboard, bbPart);
                } else {
                    score += fastGetScoreFromMoveTable(bitboard, bbPart);
                }
            }
        }

        score /= 550;

        if (historyCount > 0) {
            float historyscore = (history_moves[getpiece(move)][getsource(move)][gettarget(move)] / (float) historyCount) * 2000;
            score += historyscore;
        }

        return (int)score;
    }
}


static inline void sort_moves(moveList *move_list, int *hashmove, Board *board){

    int scores[move_list->count];

    for (int i = 0; i < move_list->count; i++) {
        scores[i] = score_move(move_list->moves[i], hashmove, board);
    }

    insertion_sort(scores, move_list);

//    if (board->ply == 0) {
//        print_fen(board);
//        printf("\n");
//        for (int i = 0; i < move_list->count; i++) {
//            print_move(move_list->moves[i]);
//            printf(" : %d\n", scores[i]);
//        }
//        printf("\n\n");
//    }
}

static inline int quiesce(int alpha, int beta, Board *board) {
    if (board->ply > selDepth){
        selDepth = board->ply;
    }

    board->searchDepth = 0;

    if (nodes % 2048 == 0)
        communicate();

    if (stop){
        return 0;
    }

    nodes++;
    qnodes++;

    int stand_pat = nnue_evaluate(board);

    if (stand_pat >= beta){
        return beta;
    }

    if (alpha <= stand_pat){
        alpha = stand_pat;
    }

    moveList legalMoves;
    legalMoves.count = 0;

    U64 old_occupancies = board->occupancies[white] | board->occupancies[black];
    board->occupancies[both] = old_occupancies;

    copy_board();

    int tmp[4] = {0,0,0,0};

    generate_moves(&legalMoves, board);
    sort_moves(&legalMoves, tmp, board);

    for (int moveId = 0; moveId < legalMoves.count; moveId++){
        int move = legalMoves.moves[moveId];

        if (make_move(move, only_captures, 0, board)){
            int score = -quiesce(-beta, -alpha, board);

            take_back();

            if (score >= beta){
                return beta;
            }

            if (score > alpha)
                alpha = score;
        }
    }

    return alpha;

}

int ZwSearch(int beta, int depth, Board *board){
    nodes++;

    if (nodes % 2048 == 0)
        communicate();

    if (stop){
        return 0;
    }

    if (depth <= 0)
        return quiesce(beta-1, beta, board);

    int tmp[4] = {0,0,0,0};

    moveList legalMoves;
    legalMoves.count = 0;

    generate_moves(&legalMoves, board);
    sort_moves(&legalMoves, tmp, board);

    copy_board();

    for (int moveId = 0; moveId < legalMoves.count; moveId++){
        int move = legalMoves.moves[moveId];

        int score;

        if (make_move(move, all_moves, 1, board)){

            score = -ZwSearch(-beta + 1, depth-1, board);

            take_back();

            if (score >= beta){
                return beta;
            }
        }
    }

    return beta-1;
}

void find_pv(moveList *moves, Board *board){
    if (board->helperThread)
        return;

    follow_pv = 0;
    for (int moveId = 0; moveId < moves->count; moveId++){
        if (moves->moves[moveId] == board->pv_line.moves[board->ply]){
            found_pv = 1;
            follow_pv = 1;
        }
    }
}

static inline int negamax(int depth, int alpha, int beta, Line *pline, Board *board) {
    //general maintenence
    nodes++;

    if (nodes % 2048 == 0)
        communicate();

    if (stop) {
        return 0;
    }

    //do check extensions before probing hash table
    int in_check = is_square_attacked(bsf((board->side == white) ? board->bitboards[K] : board->bitboards[k]), (board->side ^ 1), board);

    if (in_check)
        depth++;

    if (board->occupancies[both] == (WK | BK | WP | BP)) {
        if (getpiece(board->prevmove) == P) {
            if (!(pastPawnMasks[white][gettarget(board->prevmove)] & board->bitboards[p]))
                depth++;
        } else if (getpiece(board->prevmove) == P) {
            if (!(pastPawnMasks[black][gettarget(board->prevmove)] & board->bitboards[P]))
                depth++;
        }
    }

    board->searchDepth = depth;

    if (board->ply == 0)
        board->zobrist_history_search_index = board->zobrist_history_length;

    if (is_threefold_repetition(board)) {
        return 0;
    }

    //HASH TABLE PROBE
    int hash_move[4] = {NO_MOVE, NO_MOVE, NO_MOVE, NO_MOVE};
    int hash_lookup = ProbeHash(depth, alpha, beta, hash_move, pline, board);

    if ((hash_lookup) != valUNKNOWN && board->ply != 0) {
        return hash_lookup;
    }

    int staticeval = nnue_evaluate(board);

    //TB PROBE
    if (board->ply != 0) {
        U32 tbres = get_wdl(board);

        if (!tbsearch) {
            if (tbres != TB_RESULT_FAILED) {

                tbHits++;
                if (tbres == 4)
                    return 4000000;
                if (tbres == 2)
                    return 0;
                if (tbres == 0)
                    return -4000000;
            }
        } else {
            int move = get_root_move(board);
            if (move != 0) {

                if (tbres == 4)
                    return 4000000;
                if (tbres == 2)
                    return 0;
                if (tbres == 0)
                    return -4000000;
            }
        }
    }

    if (!board->helperThread)
        found_pv = 0;

    int pvnode = beta - alpha > 1;

    Line line;
    line.length = 0;

    int hashf = hashfALPHA;

    if (depth <= 0) {
        pline->length = 0;
        return quiesce(alpha, beta, board);
    }

    int eval;
    if (depth >= 3 && !in_check && board->ply
        && (WQ | WR) && (BQ | BR)){
        copy_board();

        make_null_move(board);
        board->enpessant = no_sq;

        eval = -ZwSearch(1 - beta, depth - 3, board);

        take_back();
        if (eval >= beta) {
            RecordHash(depth - 2, beta, NO_MOVE, hashfBETA, NULL, board);
            return beta;
        }
    }


    if (!pvnode && !in_check && depth < 3) {
        if ((staticeval - (100 * 64 * depth)) > beta){
            return beta;
        }
    }

    if (!pvnode && !in_check && depth <= 3) {

        int value = staticeval + (125 * 64);
        if (value < beta) {
            int new_value;
            if (depth == 1) {
                new_value = quiesce(alpha, beta, board);
                pline->length = 0;
                return max(value, new_value);
            }
            value += (175 * 64);
            if (value < beta && depth <= 3) {
                new_value = quiesce(alpha, beta, board);
                if (new_value < beta) {
                    pline->length = 0;
                    return max(new_value, value);
                }
            }
        }
    }

    moveList legalMoves;
    memset(&legalMoves, 0, sizeof legalMoves);

    generate_moves(&legalMoves, board);

    if (follow_pv) {
        find_pv(&legalMoves, board);
    }

    sort_moves(&legalMoves, hash_move, board);

    copy_board();

    int legalMoveCount = 0;
    int move;
    MoveEval best = {.move = NO_MOVE, .eval = -100000000};

    for (int moveId = 0; moveId < legalMoves.count; moveId++) {
        move = legalMoves.moves[moveId];

        if (make_move(move, all_moves, 1, board)) {

            legalMoveCount++;


            if (legalMoveCount == 0) {
                eval = -negamax(depth - 1, -beta, -alpha, &line, board);
            } else {
                //LMR
                //NO_LMR test for debugging purposes.

                if ((depth >= 3) && (legalMoveCount > 5) && (in_check == 0) && (getcapture(move) == 0) && (getpromoted(move) == 0)) {
#ifndef NO_LMR
                    eval = -negamax(depth - 2, -alpha - 1, -alpha, &line, board);
#else
                    eval = alpha + 1;
#endif
                } else {
                    eval = alpha + 1;
                }

                //PV search
                if (eval > alpha) {
//                    if ((depth >= 3) && (legalMoveCount > 5) && (in_check == 0) && (get_move_capture(move) == 0) && (get_move_promoted(move) == 0))
//                    {
//                        if (legalMoveCount > 10 && depth > 4) {
//                            print_fen(board);
//                            printf("\n%d\n", legalMoveCount);
//                            print_move(move);
//                            printf("\n%d\n\n", depth);
//                        }
//                    }

                    eval = -negamax(depth - 1, -alpha - 1, -alpha, &line, board);
                    if ((eval > alpha) && (eval < beta)) {
                        eval = -negamax(depth - 1, -beta, -alpha, &line, board);
                    }
                }
            }


            take_back();

            if (eval > best.eval) {

                best.move = move;
                best.eval = eval;

                if (eval > alpha) {

                    alpha = eval;
                    hashf = hashfEXACT;

                    pline->moves[0] = move;
                    memcpy(pline->moves + 1, line.moves, line.length * 4);
                    pline->length = line.length + 1;

                }

                if (eval >= beta) {

//                if (legalMoveCount > 10 && depth > 2){
//                    print_fen(board);
//                    printf("\n%d\n", legalMoveCount);
//                    print_move(move);
//                    printf("\n%d\n\n", board->ply);
//                }

                    if (!getcapture(move)) {
                        if (!board->helperThread) {
                            killer_moves[board->ply][1] = killer_moves[board->ply][0];
                            killer_moves[board->ply][0] = move;
                        }

                        history_moves[getpiece(move)][getsource(move)][gettarget(move)] += (float) (depth * depth *
                                                                                                    legalMoveCount);
                        historyCount += depth * depth * legalMoveCount;
                    }

                    RecordHash(depth, beta, best.move, hashfBETA, pline, board);

                    return beta;
                }
            }
        }

    }

    if (legalMoveCount == 0) {
        if (in_check) {
            return (-4900000) + board->ply;

        } else {
            return 0;
        }
    }

    RecordHash(depth, alpha, best.move, hashf, pline, board);
    return alpha;

}


typedef struct NegamaxArgs NegamaxArgs;
struct NegamaxArgs{
    Line *pline;
    Board board;
};

typedef struct Thread Thread;
struct Thread{
    pthread_t pthread;
    Line line;
    NegamaxArgs args;
};

void negamax_thread(void *args){
    NegamaxArgs *nargs = args;
    for (int i = 0; i < max_ply; ++i) {
        memset(nargs->pline, 0, sizeof (Line));

        int eval = negamax(i, DEF_ALPHA, DEF_BETA, nargs->pline, &nargs->board);

        memcpy(&nargs->board.pv_line, &nargs->pline, sizeof(Line));

        if (stop)
            return;
    }
}

int willMakeNextDepth(int curd, const float *times){

    if (curd < 4)
        return 1;

    float multiplier = 0;
    float divisor = 0;

    for (int i = curd - 3; i < curd; ++i) {
        if (times[i-1] > 1) {
            multiplier += (times[i] / times[i - 1]);
            divisor += 1;
        }
    }

    if (divisor == 0)
        return 1;

    multiplier = multiplier / divisor;
    float timepred = (multiplier * times[curd-1]);
    float timeleft = (float)(moveTime - (get_time_ms() - startingTime));

    return (timepred < (timeleft * 2)) ? 1 : 0;
}

void *search_position(void *arg){
    //Table bases

    Board board;
    memcpy(&board, &UciBoard, sizeof(Board));

    int depth = *(int*)arg;

    if (get_wdl(&board) != TB_RESULT_FAILED) {
        int move = get_root_move(&board);

        //this is shit code but thats okay its not my fault :)
        if (move != 0) {
            printf("bestmove ");
            print_move(move);
            printf("\n");
            return NULL;
        } else{
            tbsearch = 1;
        }
    }

    start_time();

    stop = 0;
    nodes = 0;
    qnodes = 0;
    tbHits = 0;
    selDepth = 0;
    historyCount = 0;
    aspwindow = DEF_ASPWINDOW;

    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_moves, 0, sizeof(history_moves));

    Line negamax_line;
    negamax_line.length = 0;

    int alpha = DEF_ALPHA;
    int beta = DEF_BETA;

    int eval;

    //best move from previous depth
    int prevBestMove = 0;

    //how long did it take to search to current depth
    float depthTime[max_ply];
    memset(depthTime, 0, sizeof depthTime);

//    int hash_move = no_move;
//    int staticeval = 0;
//    int hash_lookup = ProbeHash(6, alpha, beta, &hash_move, &staticeval, &pv_line, &board);
//    if ((hash_lookup) != valUNKNOWN) {
//        dynamicTimeManagment = 0;
//    }
    int tmp[4] = {0,0,0,0};
    reset_hash_table();

    moveList legalMoves;
    generate_moves(&legalMoves, &board);
    remove_illigal_moves(&legalMoves, &board);
    sort_moves(&legalMoves, tmp, &board);
    int numThreads = threadCount < legalMoves.count ? (int) threadCount : (int) legalMoves.count;
    Thread threads[numThreads];

    if (threadCount > 1) {

        memset(&threads, 0, sizeof threads);

        for (int i = 0; i < numThreads; ++i) {
            threads[i].args = (struct NegamaxArgs) {.pline = &threads[i].line, .board = board};
            make_move(legalMoves.moves[i], all_moves, 1, &threads[i].args.board);

            pthread_create(&threads[i].pthread, NULL, (void *(*)(void *)) negamax_thread, &threads[i].args);
        }
    }

#ifdef NO_LMR
    printf("info string This build is without Late Move Reduction, and should be used for debugging purposes only.\n");
#endif

    for (int currentDepth = 1; currentDepth <= depth; currentDepth++){

        board.ply = 0;

        follow_pv = 1;
        found_pv = 0;

        if (dynamicTimeManagment && !willMakeNextDepth(currentDepth, depthTime))
            break;

        depthTime[currentDepth] = (float )get_time_ms();

        int nmRes = negamax(currentDepth, alpha, beta, &negamax_line, &board);

        if (stop) {
            break;
        }

        //if the evaluation is outside of aspiration window bounds, reset alpha and beta and continue the search
        if ((nmRes >= beta) || (nmRes <= alpha)){
            board.ply = 0;
            selDepth = 0;

            follow_pv = 1;
            found_pv = 0;

            if (currentDepth > 2)
                aspwindow *= 2;

            printf("info string aspiration research. Window = %d\n", aspwindow/64);

            alpha = DEF_ALPHA;
            beta = DEF_BETA;

            memset(&negamax_line, 0, sizeof negamax_line);

            nmRes = negamax(currentDepth, alpha, beta, &negamax_line, &board);

        } else {
            aspwindow -= (aspwindow/2);
        }

        //if time ran out during aspiration research, break.
        if (stop){
            break;
        }

        depthTime[currentDepth] = (float)get_time_ms() - depthTime[currentDepth];

        eval = nmRes;

        alpha = nmRes - aspwindow;
        beta = nmRes + aspwindow;

        memcpy(&board.pv_line, &negamax_line, sizeof negamax_line);
        memset(&negamax_line, 0, sizeof negamax_line);

        //TIME MANAGMENT
        if (dynamicTimeManagment) {
            if (board.pv_line.moves[0] == prevBestMove) {
                moveTime -= (moveTime / 8);
            } else {
                moveTime += (moveTime / 10);
            }

            if (abs(nmRes)/60 < 180) {
                float floatingAspWindow = (float) aspwindow / 60;
                float floatingMoveTime = (float) moveTime;

                floatingMoveTime += floatingMoveTime * ((floatingAspWindow - 25) / 200);
                moveTime = (int) floatingMoveTime;
            }
        }

        prevBestMove = board.pv_line.moves[0];

        printf("info score %s %d depth %d seldepth %d nodes %ld qnodes %ld tbhits %ld time %d pv ",
               (abs(eval) > 4000000) ? "mate" : "cp" , (abs(eval) > 4000000) ? (4900000 - abs(eval)) * (eval / abs(eval)) : eval/64,
               currentDepth, selDepth, nodes, qnodes, tbHits, (get_time_ms() - startingTime));

        for (int i = 0; i < currentDepth; i++){
            if (board.pv_line.moves[i] == 0)
                break;
            print_move(board.pv_line.moves[i]);
            printf(" ");
        }

        printf("\n");

        if ((abs(eval) > 3000000)){
            break;
        }

    }

    if (threadCount > 1) {
        int originalStop = stop;

        stop = 1;
        for (int i = 0; i < numThreads; ++i) {
            pthread_join(threads[i].pthread, NULL);
        }

        stop = originalStop;
    }

    dynamicTimeManagment = 0;
    tbsearch = 0;

    printf("bestmove ");
    print_move(board.pv_line.moves[0]);
    printf("\n");

}
