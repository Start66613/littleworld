/* ---- snake ------------------------------------------------------------- */

typedef struct {
    arcade_snake_t model;
    arcade_rng_t rng;
    lv_obj_t *playfield;
    lv_obj_t *segments[ARCADE_SNAKE_MAX_LEN];
    int segment_count;
    lv_obj_t *food;
    lv_obj_t *score;
    lv_obj_t *extra;
    lv_obj_t *panel;
    lv_timer_t *timer;
    arcade_point_t press;
    int pressed;
    int over;
} snake_ui_t;

static snake_ui_t s_snake;

static void snake_teardown(void)
{
    if (s_snake.timer != NULL) {
        lv_timer_delete(s_snake.timer);
        s_snake.timer = NULL;
    }
    s_snake.panel = NULL;
    s_snake.playfield = NULL;
    s_snake.segment_count = 0;
}

static uint32_t snake_color(int index, int head)
{
    if (head) {
        return 0x3FA97C;
    }
    return (index & 1) ? 0x8AD9B6 : 0xA9E6CC;
}

static void snake_refresh(void)
{
    if (s_snake.playfield == NULL) {
        return;
    }
    for (int i = 0; i < s_snake.model.length; i++) {
        if (i >= s_snake.segment_count) {
            lv_obj_t *seg = lv_obj_create(s_snake.playfield);
            lv_obj_remove_style_all(seg);
            lv_obj_set_size(seg, 32, 32);
            lv_obj_set_style_radius(seg, 9, 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
            lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
            s_snake.segments[i] = seg;
            s_snake.segment_count = i + 1;
        }
        lv_obj_set_style_bg_color(s_snake.segments[i], lv_color_hex(snake_color(i, i == 0)), 0);
        lv_obj_set_pos(s_snake.segments[i], s_snake.model.body[i].x * 36 + 2, s_snake.model.body[i].y * 36 + 2);
        lv_obj_remove_flag(s_snake.segments[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = s_snake.model.length; i < s_snake.segment_count; i++) {
        lv_obj_add_flag(s_snake.segments[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(s_snake.food, s_snake.model.food.x * 36 + 4, s_snake.model.food.y * 36 + 4);
    lv_label_set_text_fmt(s_snake.score, "得分 %u", (unsigned)s_snake.model.score);
    lv_label_set_text_fmt(s_snake.extra, "长度 %d", (int)s_snake.model.length);
}

static void snake_restart(void)
{
    if (s_snake.panel != NULL) {
        lv_obj_delete(s_snake.panel);
        s_snake.panel = NULL;
    }
    s_snake.over = 0;
    arcade_snake_init(&s_snake.model, &s_snake.rng);
    snake_refresh();
    if (s_snake.timer != NULL) {
        lv_timer_set_period(s_snake.timer, arcade_snake_step_ms(&s_snake.model));
        lv_timer_resume(s_snake.timer);
    }
}

static void snake_result_panel(void)
{
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();
    if (s_snake.model.score > state->arcade_best[ARCADE_GAME_SNAKE]) {
        state->arcade_best[ARCADE_GAME_SNAKE] = s_snake.model.score;
        lw_app_state_save();
    }

    s_snake.panel = lw_card(s_overlay, 0, 0, 420, 260);
    lv_obj_center(s_snake.panel);
    lw_text(s_snake.panel, "游戏结束", &lw_font_28, palette->text, 32, 28);
    lv_obj_t *body = lw_text(s_snake.panel, "", &lw_font_22, palette->dim, 32, 78);
    lv_label_set_text_fmt(body, "本局得分 %u\n最高分 %u", (unsigned)s_snake.model.score,
                          (unsigned)state->arcade_best[ARCADE_GAME_SNAKE]);

    lv_obj_t *again = lw_button(s_snake.panel, "再来一局", 150, 48, palette->accent, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(again, 32, 190);
    lv_obj_add_event_cb(again, (lv_event_cb_t)snake_restart, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = lw_button(s_snake.panel, "返回游戏中心", 170, 48, palette->card_soft, palette->text, &lw_font_18);
    lv_obj_set_pos(back, 210, 190);
    lv_obj_add_event_cb(back, close_clicked, LV_EVENT_CLICKED, NULL);
}

static void snake_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_snake.over) {
        return;
    }
    int events = arcade_snake_step(&s_snake.model, &s_snake.rng);
    if (events & ARCADE_SNAKE_EVENT_DIED) {
        s_snake.over = 1;
        if (s_snake.timer != NULL) {
            lv_timer_pause(s_snake.timer);
        }
        snake_result_panel();
        return;
    }
    if (events & ARCADE_SNAKE_EVENT_WON) {
        s_snake.over = 1;
        if (s_snake.timer != NULL) {
            lv_timer_pause(s_snake.timer);
        }
        snake_result_panel();
        return;
    }
    if (events & ARCADE_SNAKE_EVENT_MOVED) {
        snake_refresh();
    }
    if (events & ARCADE_SNAKE_EVENT_ATE) {
        lv_timer_set_period(s_snake.timer, arcade_snake_step_ms(&s_snake.model));
    }
}

static void snake_pressed(lv_event_t *event)
{
    (void)event;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    s_snake.press.x = (int16_t)point.x;
    s_snake.press.y = (int16_t)point.y;
    s_snake.pressed = 1;
}

static void snake_released(lv_event_t *event)
{
    (void)event;
    if (!s_snake.pressed || s_snake.over) {
        return;
    }
    s_snake.pressed = 0;
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    int dx = (int)point.x - s_snake.press.x;
    int dy = (int)point.y - s_snake.press.y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < 24 && ady < 24) {
        return;
    }
    arcade_dir_t dir;
    if (adx >= ady) {
        dir = dx > 0 ? ARCADE_DIR_RIGHT : ARCADE_DIR_LEFT;
    } else {
        dir = dy > 0 ? ARCADE_DIR_DOWN : ARCADE_DIR_UP;
    }
    (void)arcade_snake_turn(&s_snake.model, dir);
}

static void open_game_snake(void)
{
    const lw_palette_t *palette = lw_palette();
    s_snake.rng.state = 0x2F6E2B1u;
    arcade_snake_init(&s_snake.model, &s_snake.rng);
    s_snake.over = 0;
    s_snake.panel = NULL;
    s_snake.segment_count = 0;

    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LW_SCREEN_W, LW_SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(palette->bg_top), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_move_foreground(overlay);
    s_overlay = overlay;
    s_game_teardown = snake_teardown;

    lv_obj_t *back = lw_button(overlay, "返回游戏中心", 180, 46, palette->card, palette->text, &lw_font_18);
    lv_obj_set_pos(back, 20, 12);
    lv_obj_add_event_cb(back, close_clicked, LV_EVENT_CLICKED, NULL);

    s_snake.score = lw_pill(overlay, "得分 0", palette->card_soft, palette->accent_deep);
    lv_obj_set_pos(s_snake.score, 700, 20);
    s_snake.extra = lw_pill(overlay, "长度 3", palette->card_soft, palette->lav_deep);
    lv_obj_set_pos(s_snake.extra, 850, 20);

    lv_obj_t *field = lv_obj_create(overlay);
    lv_obj_remove_style_all(field);
    lv_obj_set_pos(field, 8, 66);
    lv_obj_set_size(field, 1008, 468);
    lv_obj_set_style_radius(field, 22, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0xEAF9F1), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(field, snake_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(field, snake_released, LV_EVENT_RELEASED, NULL);
    s_snake.playfield = field;

    s_snake.food = lv_obj_create(field);
    lv_obj_remove_style_all(s_snake.food);
    lv_obj_set_size(s_snake.food, 28, 28);
    lv_obj_set_style_radius(s_snake.food, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_snake.food, lv_color_hex(palette->accent_deep), 0);
    lv_obj_set_style_bg_opa(s_snake.food, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_snake.food, LV_OBJ_FLAG_CLICKABLE);

    snake_refresh();
    s_snake.timer = lv_timer_create(snake_tick_cb, arcade_snake_step_ms(&s_snake.model), NULL);

    lw_text(overlay, "滑动手势控制方向，撞墙或撞到自己就结束", &lw_font_18, palette->dim, 20, 548);
}

/* ---- breakout ---------------------------------------------------------- */

typedef struct {
    arcade_breakout_t model;
    lv_obj_t *playfield;
    lv_obj_t *bricks[ARCADE_BREAKOUT_ROWS][ARCADE_BREAKOUT_COLS];
    lv_obj_t *paddle;
    lv_obj_t *ball;
    lv_obj_t *score;
    lv_obj_t *extra;
    lv_obj_t *panel;
    lv_timer_t *timer;
    uint32_t last_tick;
    uint8_t last_lives;
    uint32_t last_score;
    int over;
} breakout_ui_t;

static breakout_ui_t s_breakout;

static void breakout_teardown(void)
{
    if (s_breakout.timer != NULL) {
        lv_timer_delete(s_breakout.timer);
        s_breakout.timer = NULL;
    }
    s_breakout.panel = NULL;
    s_breakout.playfield = NULL;
}

static uint32_t breakout_row_color(int row)
{
    static const uint32_t colors[ARCADE_BREAKOUT_ROWS] = {0xFFC2D4, 0xFFD9C2, 0xE7D6FF,
                                                          0xD6E4FF, 0xBFEBD8, 0xF7E9A8};
    return colors[row % ARCADE_BREAKOUT_ROWS];
}

static void breakout_refresh(void)
{
    if (s_breakout.playfield == NULL) {
        return;
    }
    for (int row = 0; row < ARCADE_BREAKOUT_ROWS; row++) {
        for (int col = 0; col < ARCADE_BREAKOUT_COLS; col++) {
            if (s_breakout.model.brick[row][col]) {
                lv_obj_remove_flag(s_breakout.bricks[row][col], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_breakout.bricks[row][col], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    lv_obj_set_pos(s_breakout.paddle, (int)(s_breakout.model.paddle_x >> 8), ARCADE_BREAKOUT_PADDLE_Y);
    lv_obj_set_pos(s_breakout.ball, (int)(s_breakout.model.x >> 8) - ARCADE_BREAKOUT_BALL_R,
                   (int)(s_breakout.model.y >> 8) - ARCADE_BREAKOUT_BALL_R);
    if (s_breakout.model.lives != s_breakout.last_lives) {
        s_breakout.last_lives = s_breakout.model.lives;
        lv_label_set_text_fmt(s_breakout.extra, "生命 %u", (unsigned)s_breakout.model.lives);
    }
    if (s_breakout.model.score != s_breakout.last_score) {
        s_breakout.last_score = s_breakout.model.score;
        lv_label_set_text_fmt(s_breakout.score, "得分 %u", (unsigned)s_breakout.model.score);
    }
}

static void breakout_restart(void)
{
    if (s_breakout.panel != NULL) {
        lv_obj_delete(s_breakout.panel);
        s_breakout.panel = NULL;
    }
    s_breakout.over = 0;
    arcade_breakout_init(&s_breakout.model);
    s_breakout.last_lives = s_breakout.model.lives;
    s_breakout.last_score = 0;
    s_breakout.last_tick = lv_tick_get();
    breakout_refresh();
    if (s_breakout.timer != NULL) {
        lv_timer_resume(s_breakout.timer);
    }
}

static void breakout_result_panel(int won)
{
    const lw_palette_t *palette = lw_palette();
    lw_state_t *state = lw_app_state();
    if (s_breakout.model.score > state->arcade_best[ARCADE_GAME_BREAKOUT]) {
        state->arcade_best[ARCADE_GAME_BREAKOUT] = s_breakout.model.score;
        lw_app_state_save();
    }
    s_breakout.panel = lw_card(s_overlay, 0, 0, 420, 260);
    lv_obj_center(s_breakout.panel);
    lw_text(s_breakout.panel, won ? "通关啦" : "游戏结束", &lw_font_28, palette->text, 32, 28);
    lv_obj_t *body = lw_text(s_breakout.panel, "", &lw_font_22, palette->dim, 32, 78);
    lv_label_set_text_fmt(body, "本局得分 %u\n最高分 %u", (unsigned)s_breakout.model.score,
                          (unsigned)state->arcade_best[ARCADE_GAME_BREAKOUT]);
    lv_obj_t *again = lw_button(s_breakout.panel, "再来一局", 150, 48, palette->accent, 0xFFFFFF, &lw_font_18);
    lv_obj_set_pos(again, 32, 190);
    lv_obj_add_event_cb(again, (lv_event_cb_t)breakout_restart, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = lw_button(s_breakout.panel, "返回游戏中心", 170, 48, palette->card_soft, palette->text, &lw_font_18);
    lv_obj_set_pos(back, 210, 190);
    lv_obj_add_event_cb(back, close_clicked, LV_EVENT_CLICKED, NULL);
}

static void breakout_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_breakout.over || s_breakout.playfield == NULL) {
        return;
    }
    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - s_breakout.last_tick;
    if (elapsed < ARCADE_BREAKOUT_TICK_MS) {
        return;
    }
    uint32_t steps = elapsed / ARCADE_BREAKOUT_TICK_MS;
    if (steps > 3) {
        steps = 3;
    }
    s_breakout.last_tick += steps * ARCADE_BREAKOUT_TICK_MS;
    for (uint32_t i = 0; i < steps; i++) {
        arcade_breakout_tick(&s_breakout.model);
    }
    breakout_refresh();

    if (s_breakout.model.state == ARCADE_STATE_WON || s_breakout.model.state == ARCADE_STATE_OVER) {
        s_breakout.over = 1;
        if (s_breakout.timer != NULL) {
            lv_timer_pause(s_breakout.timer);
        }
        breakout_result_panel(s_breakout.model.state == ARCADE_STATE_WON);
    }
}

static void breakout_pointer(lv_event_t *event)
{
    (void)event;
    if (s_breakout.over || s_breakout.playfield == NULL) {
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    lv_area_t area;
    lv_obj_get_coords(s_breakout.playfield, &area);
    int x = (int)point.x - (int)area.x1;
    if (x < 0) {
        x = 0;
    }
    if (x > ARCADE_BREAKOUT_W) {
        x = ARCADE_BREAKOUT_W;
    }
    arcade_breakout_move_paddle(&s_breakout.model, x * ARCADE_FP_ONE);
    if (s_breakout.model.state == ARCADE_STATE_READY) {
        arcade_breakout_launch(&s_breakout.model);
    }
    breakout_refresh();
}

static void open_game_breakout(void)
{
    const lw_palette_t *palette = lw_palette();
    arcade_breakout_init(&s_breakout.model);
    s_breakout.over = 0;
    s_breakout.panel = NULL;
    s_breakout.last_lives = s_breakout.model.lives;
    s_breakout.last_score = 0;
    s_breakout.last_tick = lv_tick_get();

    lv_obj_t *overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LW_SCREEN_W, LW_SCREEN_H);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(palette->bg_top), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_move_foreground(overlay);
    s_overlay = overlay;
    s_game_teardown = breakout_teardown;

    lv_obj_t *back = lw_button(overlay, "返回游戏中心", 180, 46, palette->card, palette->text, &lw_font_18);
    lv_obj_set_pos(back, 20, 12);
    lv_obj_add_event_cb(back, close_clicked, LV_EVENT_CLICKED, NULL);

    s_breakout.score = lw_pill(overlay, "得分 0", palette->card_soft, palette->accent_deep);
    lv_obj_set_pos(s_breakout.score, 700, 20);
    s_breakout.extra = lw_pill(overlay, "生命 3", palette->card_soft, palette->lav_deep);
    lv_obj_set_pos(s_breakout.extra, 850, 20);

    lv_obj_t *field = lv_obj_create(overlay);
    lv_obj_remove_style_all(field);
    lv_obj_set_pos(field, 8, 66);
    lv_obj_set_size(field, ARCADE_BREAKOUT_W, ARCADE_BREAKOUT_H);
    lv_obj_set_style_radius(field, 22, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0xFFF0F4), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(field, breakout_pointer, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(field, breakout_pointer, LV_EVENT_PRESSING, NULL);
    s_breakout.playfield = field;

    for (int row = 0; row < ARCADE_BREAKOUT_ROWS; row++) {
        for (int col = 0; col < ARCADE_BREAKOUT_COLS; col++) {
            int x, y, w, h;
            arcade_breakout_brick_rect(row, col, &x, &y, &w, &h);
            lv_obj_t *brick = lv_obj_create(field);
            lv_obj_remove_style_all(brick);
            lv_obj_set_pos(brick, x, y);
            lv_obj_set_size(brick, w, h);
            lv_obj_set_style_radius(brick, 8, 0);
            lv_obj_set_style_bg_color(brick, lv_color_hex(breakout_row_color(row)), 0);
            lv_obj_set_style_bg_opa(brick, LV_OPA_COVER, 0);
            lv_obj_remove_flag(brick, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(brick, LV_OBJ_FLAG_SCROLLABLE);
            s_breakout.bricks[row][col] = brick;
        }
    }

    s_breakout.paddle = lv_obj_create(field);
    lv_obj_remove_style_all(s_breakout.paddle);
    lv_obj_set_size(s_breakout.paddle, ARCADE_BREAKOUT_PADDLE_W, ARCADE_BREAKOUT_PADDLE_H);
    lv_obj_set_style_radius(s_breakout.paddle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_breakout.paddle, lv_color_hex(palette->accent_deep), 0);
    lv_obj_set_style_bg_opa(s_breakout.paddle, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_breakout.paddle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_breakout.paddle, LV_OBJ_FLAG_SCROLLABLE);

    s_breakout.ball = lv_obj_create(field);
    lv_obj_remove_style_all(s_breakout.ball);
    lv_obj_set_size(s_breakout.ball, ARCADE_BREAKOUT_BALL_R * 2, ARCADE_BREAKOUT_BALL_R * 2);
    lv_obj_set_style_radius(s_breakout.ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_breakout.ball, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_breakout.ball, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_breakout.ball, 2, 0);
    lv_obj_set_style_border_color(s_breakout.ball, lv_color_hex(palette->accent_deep), 0);
    lv_obj_remove_flag(s_breakout.ball, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_breakout.ball, LV_OBJ_FLAG_SCROLLABLE);

    breakout_refresh();
    s_breakout.timer = lv_timer_create(breakout_tick_cb, ARCADE_BREAKOUT_TICK_MS / 2, NULL);

    lw_text(overlay, "在屏幕上拖动挡板，轻触发球；掉球会损失一条命", &lw_font_18, palette->dim, 20, 548);
}
