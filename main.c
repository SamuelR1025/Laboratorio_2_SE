#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"

static const gpio_num_t ROW_PINS[8] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_12, GPIO_NUM_14,
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18
};

static const gpio_num_t COL_PINS[8] = {
    GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23,
    GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_33
};

#define BTN_P1_UP    GPIO_NUM_13
#define BTN_P1_DOWN  GPIO_NUM_2
#define BTN_P2_UP    GPIO_NUM_32
#define BTN_P2_DOWN  GPIO_NUM_0

#define MATRIX_W      8
#define MATRIX_H      8
#define PADDLE_SIZE   2
#define WINNING_SCORE 5
#define GAME_TICK_MS  300
#define INPUT_TICK_MS 40
#define PADDLE_MOVE_MS 200
#define REFRESH_HZ    800

#define C_OFF   0
#define C_RED   1
#define C_GREEN 2

typedef enum {
    STATE_SHOW_SCORE,
    STATE_PLAYING,
    STATE_GOAL,
    STATE_GAME_OVER
} game_state_t;

static volatile uint8_t framebuffer[MATRIX_H][MATRIX_W];
static int paddle1_y = 3, paddle2_y = 3;
static int ball_x = 3, ball_y = 3, ball_vx = 1, ball_vy = 1;
static int score1 = 0, score2 = 0;
static game_state_t state = STATE_SHOW_SCORE;
static volatile uint8_t current_row = 0;
static int64_t last_p1_move_ms = 0;
static int64_t last_p2_move_ms = 0;

static void IRAM_ATTR on_refresh_timer(void *arg) {
    for (int r = 0; r < 8; r++) gpio_set_level(ROW_PINS[r], 1);
    for (int c = 0; c < 8; c++) {
        uint8_t cell = framebuffer[current_row][c];
        bool on = (cell != C_OFF);
        gpio_set_level(COL_PINS[c], on ? 0 : 1);
    }
    gpio_set_level(ROW_PINS[current_row], 0);
    current_row = (current_row + 1) & 0x07;
}

static void fb_clear(void) {
    for (int y = 0; y < MATRIX_H; y++)
        for (int x = 0; x < MATRIX_W; x++)
            framebuffer[y][x] = C_OFF;
}

static void draw_scene(void) {
    fb_clear();
    for (int i = 0; i < PADDLE_SIZE; i++) framebuffer[paddle1_y + i][0] = C_RED;
    for (int i = 0; i < PADDLE_SIZE; i++) framebuffer[paddle2_y + i][7] = C_GREEN;
    framebuffer[ball_y][ball_x] = (ball_x < 4) ? C_RED : C_GREEN;
}

static void draw_scoreboard(void) {
    fb_clear();
    for (int i = 0; i < score1 && i < 4; i++) framebuffer[0][i] = C_RED;
    for (int i = 0; i < score2 && i < 4; i++) framebuffer[0][7 - i] = C_GREEN;
}

static int64_t millis(void) { return esp_timer_get_time() / 1000; }

static void read_inputs(void) {
    int64_t now = millis();
    bool p1_up   = (gpio_get_level(BTN_P1_UP)   == 0);
    bool p1_down = (gpio_get_level(BTN_P1_DOWN) == 0);
    bool p2_up   = (gpio_get_level(BTN_P2_UP)   == 0);
    bool p2_down = (gpio_get_level(BTN_P2_DOWN) == 0);

    if (now - last_p1_move_ms >= PADDLE_MOVE_MS) {
        if (p1_up && paddle1_y > 0) { paddle1_y--; last_p1_move_ms = now; }
        else if (p1_down && paddle1_y < MATRIX_H - PADDLE_SIZE) { paddle1_y++; last_p1_move_ms = now; }
    }
    if (now - last_p2_move_ms >= PADDLE_MOVE_MS) {
        if (p2_up && paddle2_y > 0) { paddle2_y--; last_p2_move_ms = now; }
        else if (p2_down && paddle2_y < MATRIX_H - PADDLE_SIZE) { paddle2_y++; last_p2_move_ms = now; }
    }
}

static int update_ball(void) {
    int nx = ball_x + ball_vx;
    int ny = ball_y + ball_vy;
    if (ny < 0) { ny = 0; ball_vy = 1; }
    if (ny > 7) { ny = 7; ball_vy = -1; }
    if (nx <= 0) {
        if (ny >= paddle1_y && ny < paddle1_y + PADDLE_SIZE) { nx = 0; ball_vx = 1; }
        else return 2;
    }
    if (nx >= 7) {
        if (ny >= paddle2_y && ny < paddle2_y + PADDLE_SIZE) { nx = 7; ball_vx = -1; }
        else return 1;
    }
    ball_x = nx; ball_y = ny;
    return 0;
}

static void reset_positions(int serve_to_left) {
    paddle1_y = 3; paddle2_y = 3;
    ball_x = serve_to_left ? 4 : 3;
    ball_y = 3 + (esp_random() & 1);
    ball_vx = serve_to_left ? -1 : 1;
    ball_vy = (esp_random() & 1) ? 1 : -1;
}

static void anim_goal(int scorer) {
    int color = (scorer == 1) ? C_RED : C_GREEN;
    for (int blink = 0; blink < 3; blink++) {
        fb_clear();
        for (int y = 0; y < 8; y++) 
            for (int x = (scorer==1?0:4); x < (scorer==1?4:8); x++) framebuffer[y][x] = color;
        vTaskDelay(pdMS_TO_TICKS(200));
        fb_clear(); vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void anim_game_over(int winner) {
    int win_x_ini = (winner == 1) ? 0 : 4;
    int win_x_fin = (winner == 1) ? 4 : 8;
    int lose_x_ini = (winner == 1) ? 4 : 0;
    int lose_x_fin = (winner == 1) ? 8 : 4;
    int win_color = (winner == 1) ? C_RED : C_GREEN;
    int lose_color = (winner == 1) ? C_GREEN : C_RED;

    for (int b = 0; b < 10; b++) {
        for (int y = 0; y < 8; y++) {
            for (int x = win_x_ini; x < win_x_fin; x++) framebuffer[y][x] = win_color;
            for (int x = lose_x_ini; x < lose_x_fin; x++) framebuffer[y][x] = lose_color;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        for (int y = 0; y < 8; y++) {
            for (int x = win_x_ini; x < win_x_fin; x++) framebuffer[y][x] = C_OFF;
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    fb_clear();
}

static void gpio_init_all(void) {
    uint64_t out_mask = 0;
    for (int i = 0; i < 8; i++) {
        out_mask |= (1ULL << ROW_PINS[i]);
        out_mask |= (1ULL << COL_PINS[i]);
    }
    gpio_config_t out_cfg = {
        .pin_bit_mask = out_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&out_cfg);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(ROW_PINS[i], 1);
        gpio_set_level(COL_PINS[i], 1);
    }

    uint64_t in_mask = (1ULL << BTN_P1_UP) | (1ULL << BTN_P1_DOWN) | (1ULL << BTN_P2_UP) | (1ULL << BTN_P2_DOWN);
    gpio_config_t in_cfg = {
        .pin_bit_mask = in_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&in_cfg);
}

static void refresh_timer_init(void) {
    const esp_timer_create_args_t timer_args = {
        .callback = on_refresh_timer,
        .arg = NULL,
        .name = "refresh"
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000000 / REFRESH_HZ));
}

void app_main(void) {
    gpio_init_all();
    fb_clear();
    refresh_timer_init();
    reset_positions(0);
    int64_t last_game_tick = 0;
    int64_t score_shown_since = millis();
    int last_scorer = 0;

    while (1) {
        int64_t now = millis();
        switch (state) {
            case STATE_SHOW_SCORE:
                draw_scoreboard();
                if (now - score_shown_since >= 1000) state = STATE_PLAYING;
                break;
            case STATE_PLAYING:
                read_inputs();
                if (now - last_game_tick >= GAME_TICK_MS) {
                    int result = update_ball();
                    last_game_tick = now;
                    if (result > 0) {
                        if (result == 1) score1++; else score2++;
                        last_scorer = result;
                        state = STATE_GOAL;
                    }
                }
                draw_scene();
                break;
            case STATE_GOAL:
                anim_goal(last_scorer);
                if (score1 >= WINNING_SCORE || score2 >= WINNING_SCORE) {
                    state = STATE_GAME_OVER;
                } else {
                    reset_positions(last_scorer == 1 ? 0 : 1);
                    score_shown_since = millis();
                    state = STATE_SHOW_SCORE;
                }
                break;
            case STATE_GAME_OVER:
                anim_game_over(score1 >= WINNING_SCORE ? 1 : 2);
                score1 = 0; score2 = 0;
                reset_positions(0);
                score_shown_since = millis();
                state = STATE_SHOW_SCORE;
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(INPUT_TICK_MS));
    }
}