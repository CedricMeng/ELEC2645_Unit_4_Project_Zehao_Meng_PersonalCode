#include "Game_3.h"

// include headers
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "LCD.h"
#include "Utils.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "InputHandler.h"
#include "main.h"
#include "adc.h"
#include "PWM.h"
#include "stm32l4xx_hal_adc.h"

// define constants
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// screen and map settings
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define MAP_W 16
#define MAP_H 16

#define FOV_RAD (1.134464f) // 65 degrees
#define HALF_FOV_RAD (FOV_RAD * 0.5f)
#define MAX_VIEW_DISTANCE 12.0f
#define MAX_VIEW_DISTANCE_SQ (MAX_VIEW_DISTANCE * MAX_VIEW_DISTANCE)
#define RAY_STEP 0.035f

// player move settings
#define PLAYER_MOVE_SPEED 0.085f
#define PLAYER_TURN_SPEED 0.065f
#define PLAYER_RADIUS 0.16f

// damage cooldown
#define DAMAGE_COOLDOWN_MS 700u
#define BASE_SPAWN_INTERVAL_MS 1650u
#define MIN_SPAWN_INTERVAL_MS 340u
#define ZOMBIE_SPEED 0.020f
#define ZOMBIE_ATTACK_RANGE 0.42f
#define MAX_CARRY_CLIPS 3u

// kill rewards
#define KILL_HP_REWARD    8u   // health per kill
#define KILL_AMMO_REWARD  5u   // ammo per kill

// sound settings
#define HURT_BEEP_FREQ 320u
#define BEEP_VOLUME 45u
#define BEEP_MS 25u

// how many kills are needed with the last gun
#define ODIN_VICTORY_KILLS 10u
#define START_RESERVE_AMMO (GHOST_MAG_SIZE * MAX_CARRY_CLIPS)

// mag sizes
#define GHOST_MAG_SIZE 13u
#define VANDAL_MAG_SIZE 25u
#define ODIN_MAG_SIZE 100u

// cooldown times
#define GHOST_COOLDOWN_MS 132u
#define VANDAL_COOLDOWN_MS 99u
#define ODIN_COOLDOWN_START_MS 99u
#define ODIN_COOLDOWN_FAST_MS 66u
#define ODIN_SPINUP_RESET_MS 132u
#define ODIN_SPINUP_MAX 5u

// reload times
#define GHOST_RELOAD_MS 1500u
#define VANDAL_RELOAD_MS 2500u
#define ODIN_RELOAD_MS 5000u

// ranges
#define GHOST_RANGE 8.8f
#define VANDAL_RANGE 10.4f
#define ODIN_RANGE 10.0f

// cone angles
#define GHOST_CONE 0.074f
#define VANDAL_CONE 0.096f
#define ODIN_CONE 0.135f

// recoil
#define GHOST_RECOIL_PER_SHOT 0.010f
#define VANDAL_RECOIL_PER_SHOT 0.014f
#define ODIN_RECOIL_PER_SHOT 0.009f

#define GHOST_RECOIL_DECAY 0.011f
#define VANDAL_RECOIL_DECAY 0.007f
#define ODIN_RECOIL_DECAY 0.005f

#define GHOST_RECOIL_CAP 0.045f
#define VANDAL_RECOIL_CAP 0.110f
#define ODIN_RECOIL_CAP 0.150f

#define GHOST_MOVE_SPREAD 0.016f
#define VANDAL_MOVE_SPREAD 0.050f
#define ODIN_MOVE_SPREAD 0.070f

#define ZOMBIE_MAX_ENEMIES 24
#define ZOMBIE_IMPACT_MAX 6

typedef enum {
    ZOMBIE_WEAPON_GHOST = 0,
    ZOMBIE_WEAPON_VANDAL,
    ZOMBIE_WEAPON_ODIN
} ZombieWeapon_t;

typedef struct {
    float x;
    float y;
    uint8_t alive;
} ZombieEnemy_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t ttl;
    uint8_t color;
} ZombieImpactFx_t;

typedef struct {
    float player_x;
    float player_y;
    float player_angle;

    uint8_t health;
    uint16_t score;
    uint8_t game_over;
    uint8_t victory;

    ZombieEnemy_t enemies[ZOMBIE_MAX_ENEMIES];
    uint8_t enemies_alive;

    uint32_t last_spawn_tick;
    uint32_t spawn_interval_ms;
    uint32_t last_shot_tick;
    uint32_t last_damage_tick;
    uint32_t reload_finish_tick;

    uint8_t shot_fired_this_frame;
    uint8_t hit_this_frame;
    uint8_t hurt_this_frame;
    uint8_t impacts_head;
    uint8_t shot_trace_ttl;
    int16_t shot_trace_x;
    int16_t shot_trace_y;

    ZombieWeapon_t current_weapon;
    uint16_t ammo_in_mag;
    uint16_t ammo_reserve;
    uint16_t stage_kills;
    uint16_t final_weapon_kills;
    float recoil_rad;
    float move_spread_rad;
    uint8_t reload_active;
    uint8_t shoot_was_down;
    uint8_t odin_spinup;

    uint8_t audio_busy;
    uint8_t audio_step_idx;
    uint8_t audio_step_count;
    uint32_t audio_step_end_tick;
    uint16_t audio_step_freq[5];
    uint8_t audio_step_vol[5];
    uint8_t audio_step_dur[5];

    ZombieImpactFx_t impacts[ZOMBIE_IMPACT_MAX];
} ZombieEngine_t;

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    GPIO_PinState active_level;
} Button_t;

static Button_t shoot_buttons[] = {
    {BTN2_GPIO_Port, BTN2_Pin, GPIO_PIN_RESET},   // Right joystick SW / shoot (active-low)
    {B1_GPIO_Port, B1_Pin, GPIO_PIN_RESET}        // Optional fallback shoot button
};

static Button_t reload_button = {BTN3_GPIO_Port, BTN3_Pin, GPIO_PIN_RESET}; // Left joystick SW / reload (active-low)

static Joystick_cfg_t joystick_move_cfg = {
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_1,
    .y_channel = ADC_CHANNEL_2,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
};

static Joystick_cfg_t joystick_look_cfg = {
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_5,
    .y_channel = ADC_CHANNEL_6,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
};

static Joystick_t joystick_move_data;
static Joystick_t joystick_look_data;
static ZombieEngine_t zombie_engine;
static volatile uint8_t game_over_flag = 0;
static uint32_t muzzle_led_off_tick = 0;
static uint16_t current_fps = 0;

// forward declarations
static void trigger_beep(ZombieEngine_t* engine, uint32_t freq, uint16_t duration_ms);
static const uint8_t world_map[MAP_H][MAP_W];
static void add_impact_fx(ZombieEngine_t* engine, int16_t sx, int16_t sy, uint8_t color, uint8_t ttl);
static uint8_t world_to_screen(const ZombieEngine_t* engine, float wx, float wy, int16_t* out_x, int16_t* out_y);
static uint8_t first_wall_hit(const ZombieEngine_t* engine, float ang, float* out_x, float* out_y);
static void ZombieEngine_StopAudio(ZombieEngine_t* engine);
static const char* get_weapon_label(ZombieWeapon_t weapon);
static uint16_t get_weapon_mag_size(ZombieWeapon_t weapon);
static uint16_t get_weapon_cooldown_ms(const ZombieEngine_t* engine);
static uint16_t get_weapon_reload_time(ZombieWeapon_t weapon);
static float get_weapon_recoil_decay(ZombieWeapon_t weapon);
static float get_weapon_move_spread(ZombieWeapon_t weapon);
static uint16_t get_weapon_reserve_cap(const ZombieEngine_t* engine);
static uint8_t weapon_is_semi_auto(ZombieWeapon_t weapon);
extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;
extern Buzzer_cfg_t buzzer_cfg;

static void ShootButtons_Init(void);
static uint8_t ShootButtons_IsDown(void);
static void update_game(Joystick_t* move_input, Joystick_t* look_input, uint8_t shoot_pressed, uint8_t reload_pressed);
static void render_game(void);
static void draw_intro_animation(void);

static uint16_t get_weapon_reserve_cap(const ZombieEngine_t* engine)
{
    return (uint16_t)(get_weapon_mag_size(engine->current_weapon) * MAX_CARRY_CLIPS);
}

/* Queue a short buzzer envelope and start playing its first step right away. */
static void start_audio_sequence(ZombieEngine_t* engine,
                                 const uint16_t* freqs,
                                 const uint8_t* vols,
                                 const uint8_t* durs,
                                 uint8_t count)
{
    if (count == 0u) {
        return;
    }
    if (count > 5u) {
        count = 5u;
    }

    for (uint8_t i = 0; i < count; i++) {
        engine->audio_step_freq[i] = freqs[i];
        engine->audio_step_vol[i] = vols[i];
        engine->audio_step_dur[i] = durs[i];
    }

    engine->audio_step_count = count;
    engine->audio_step_idx = 0u;
    engine->audio_busy = 1u;
    buzzer_tone(&buzzer_cfg, engine->audio_step_freq[0], engine->audio_step_vol[0]);
    engine->audio_step_end_tick = HAL_GetTick() + (uint32_t)engine->audio_step_dur[0];
}

/* Pick a canned buzzer envelope that loosely mimics each weapon. */
static void play_weapon_shot_sound(ZombieEngine_t* engine, ZombieWeapon_t weapon)
{
    /* Buzzer limits are huge, so we fake gun timbre with multi-stage decay envelopes. */
    static const uint16_t ghost_f[] = {3120u, 2480u, 1900u, 1380u};
    static const uint8_t  ghost_v[] = {82u,   62u,   42u,   24u};
    static const uint8_t  ghost_d[] = {3u,    4u,    5u,    8u};

    static const uint16_t vandal_f[] = {3520u, 2960u, 2360u, 1760u, 1240u};
    static const uint8_t  vandal_v[] = {100u,  86u,   70u,   50u,   28u};
    static const uint8_t  vandal_d[] = {3u,    4u,    5u,    6u,    8u};

    static const uint16_t odin_f[] = {1780u, 1520u, 1280u, 1080u};
    static const uint8_t  odin_v[] = {100u,  88u,   70u,   48u};
    static const uint8_t  odin_d[] = {2u,    3u,    4u,    6u};

    switch (weapon) {
        case ZOMBIE_WEAPON_VANDAL:
            start_audio_sequence(engine, vandal_f, vandal_v, vandal_d, 5u);
            break;
        case ZOMBIE_WEAPON_ODIN:
            start_audio_sequence(engine, odin_f, odin_v, odin_d, 4u);
            break;
        case ZOMBIE_WEAPON_GHOST:
        default:
            start_audio_sequence(engine, ghost_f, ghost_v, ghost_d, 4u);
            break;
    }
}

/* Return the short weapon name shown in the HUD. */
static const char* get_weapon_label(ZombieWeapon_t weapon)
{
    if (weapon == ZOMBIE_WEAPON_VANDAL) {
        return "VANDAL";
    }
    if (weapon == ZOMBIE_WEAPON_ODIN) {
        return "ODIN";
    }
    return "GHOST";
}

// return weapon mag size
static uint16_t get_weapon_mag_size(ZombieWeapon_t weapon)
{
    // if VANDAL
    if (weapon == ZOMBIE_WEAPON_VANDAL) {
        return VANDAL_MAG_SIZE;
    }
    // if ODIN
    if (weapon == ZOMBIE_WEAPON_ODIN) {
        return ODIN_MAG_SIZE;
    }
    // default GHOST
    return GHOST_MAG_SIZE;
}

/* Return the current weapon's fire interval in milliseconds. */
static uint16_t get_weapon_cooldown_ms(const ZombieEngine_t* engine)
{
    if (engine->current_weapon == ZOMBIE_WEAPON_VANDAL) {
        return VANDAL_COOLDOWN_MS;
    }
    if (engine->current_weapon == ZOMBIE_WEAPON_ODIN) {
        uint8_t spin = engine->odin_spinup;
        uint16_t slow = ODIN_COOLDOWN_START_MS;
        uint16_t fast = ODIN_COOLDOWN_FAST_MS;
        uint16_t drop = (uint16_t)(slow - fast);

        if (spin >= ODIN_SPINUP_MAX) {
            return fast;
        }

        return (uint16_t)(slow - ((drop * spin) / ODIN_SPINUP_MAX));
    }
    return GHOST_COOLDOWN_MS;
}

/* Return how long reloading takes for the current weapon. */
static uint16_t get_weapon_reload_time(ZombieWeapon_t weapon)
{
    if (weapon == ZOMBIE_WEAPON_VANDAL) {
        return VANDAL_RELOAD_MS;
    }
    if (weapon == ZOMBIE_WEAPON_ODIN) {
        return ODIN_RELOAD_MS;
    }
    return GHOST_RELOAD_MS;
}

/* Return how fast recoil disappears each frame. */
static float get_weapon_recoil_decay(ZombieWeapon_t weapon)
{
    if (weapon == ZOMBIE_WEAPON_VANDAL) {
        return VANDAL_RECOIL_DECAY;
    }
    if (weapon == ZOMBIE_WEAPON_ODIN) {
        return ODIN_RECOIL_DECAY;
    }
    return GHOST_RECOIL_DECAY;
}

/* Return how much movement increases spread for the current weapon. */
static float get_weapon_move_spread(ZombieWeapon_t weapon)
{
    if (weapon == ZOMBIE_WEAPON_VANDAL) {
        return VANDAL_MOVE_SPREAD; 
    }
    if (weapon == ZOMBIE_WEAPON_ODIN) {
        return ODIN_MOVE_SPREAD;
    }
    return GHOST_MOVE_SPREAD;
}

/* Ghost behaves as a tap-fire sidearm, the other weapons can be held for auto fire. */
static uint8_t weapon_is_semi_auto(ZombieWeapon_t weapon)
{
    return weapon == ZOMBIE_WEAPON_GHOST;
}

// draw weapon model and muzzle flash
static void draw_weapon_model(const ZombieEngine_t* engine)
{
    // calculate kick
    int kick = engine->shot_fired_this_frame ? 6 : 0;
    int y_base = SCREEN_HEIGHT - 36 + kick;

    // draw based on weapon
    if (engine->current_weapon == ZOMBIE_WEAPON_VANDAL) {
        // draw stock
        LCD_Draw_Rect(86, (uint16_t)(y_base + 7), 20, 10, 13, 1);
        // draw body
        LCD_Draw_Rect(108, (uint16_t)(y_base + 4), 62, 12, 13, 1);
        // draw barrel
        LCD_Draw_Rect(170, (uint16_t)(y_base + 7), 32, 6, 13, 1);
        // draw grip
        LCD_Draw_Rect(114, (uint16_t)(y_base + 16), 10, 16, 13, 1);
        // draw mag
        LCD_Draw_Rect(126, (uint16_t)(y_base + 14), 16, 22, 4, 1);
        // draw lines
        LCD_Draw_Line(108, (uint16_t)(y_base + 5), 166, (uint16_t)(y_base + 5), 14);
        LCD_Draw_Line(108, (uint16_t)(y_base + 16), 168, (uint16_t)(y_base + 16), 1);
        LCD_Draw_Line(120, (uint16_t)(y_base + 7), 120, (uint16_t)(y_base + 13), 14);
        LCD_Draw_Line(132, (uint16_t)(y_base + 8), 146, (uint16_t)(y_base + 8), 14);
        // if shot, draw flash
        if (engine->shot_fired_this_frame) {
            LCD_Draw_Rect(202, (uint16_t)(y_base + 6), 8, 8, 14, 1);
            LCD_Draw_Line(210, (uint16_t)(y_base + 10), 216, (uint16_t)(y_base + 10), 14);
        }
    } else if (engine->current_weapon == ZOMBIE_WEAPON_ODIN) {
        // draw ODIN
        LCD_Draw_Rect(70, (uint16_t)(y_base + 9), 30, 14, 3, 1);
        LCD_Draw_Rect(102, (uint16_t)(y_base + 4), 72, 16, 3, 1);
        LCD_Draw_Rect(176, (uint16_t)(y_base + 8), 34, 8, 3, 1);
        LCD_Draw_Rect(120, (uint16_t)(y_base + 20), 28, 14, 3, 1);
        LCD_Draw_Rect(106, (uint16_t)(y_base + 20), 12, 16, 13, 1);
        LCD_Draw_Rect(150, (uint16_t)(y_base + 18), 12, 14, 13, 1);
        LCD_Draw_Line(102, (uint16_t)(y_base + 5), 176, (uint16_t)(y_base + 5), 1);
        LCD_Draw_Line(102, (uint16_t)(y_base + 18), 178, (uint16_t)(y_base + 18), 1);
        LCD_Draw_Line(110, (uint16_t)(y_base + 2), 148, (uint16_t)(y_base + 2), 14);
        LCD_Draw_Line(178, (uint16_t)(y_base + 13), 202, (uint16_t)(y_base + 13), 14);
        if (engine->shot_fired_this_frame) {
            LCD_Draw_Rect(204, (uint16_t)(y_base + 6), 10, 10, 14, 1);
            LCD_Draw_Line(210, (uint16_t)(y_base + 11), 220, (uint16_t)(y_base + 11), 14);
        }
    } else {
        // default GHOST
        LCD_Draw_Rect(96, (uint16_t)(y_base + 7), 40, 9, 13, 1);
        LCD_Draw_Rect(138, (uint16_t)(y_base + 9), 22, 5, 14, 1);
        LCD_Draw_Rect(108, (uint16_t)(y_base + 16), 11, 14, 13, 1);
        LCD_Draw_Line(98, (uint16_t)(y_base + 8), 136, (uint16_t)(y_base + 8), 14);
        LCD_Draw_Line(98, (uint16_t)(y_base + 15), 136, (uint16_t)(y_base + 15), 1);
        LCD_Draw_Line(120, (uint16_t)(y_base + 16), 112, (uint16_t)(y_base + 26), 14);
        if (engine->shot_fired_this_frame) {
            LCD_Draw_Rect(160, (uint16_t)(y_base + 8), 6, 5, 14, 1);
            LCD_Draw_Line(164, (uint16_t)(y_base + 10), 170, (uint16_t)(y_base + 10), 14);
        }
    }
}

/* Hard-coded map layout: 1 is a wall tile, 0 is open floor. */
static const uint8_t world_map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,1,1,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,1},
    {1,0,1,1,1,0,0,0,1,1,1,0,0,0,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1},
    {1,0,0,0,1,0,0,0,1,0,0,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,0,0,1,1,1,0,0,0,1,0,0,0,1,0,1},
    {1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,1,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static float depth_buffer[SCREEN_WIDTH];

/* Keep angles inside [-pi, pi] so wrap-around math stays stable. */
static float normalize_angle(float angle)
{
    while (angle > (float)M_PI) {
        angle -= 2.0f * (float)M_PI;
    }
    while (angle < -(float)M_PI) {
        angle += 2.0f * (float)M_PI;
    }
    return angle;
}

/* Treat out-of-bounds space as solid so movement and rays stay inside the map. */
static uint8_t is_wall(float x, float y)
{
    int mx = (int)x;
    int my = (int)y;

    if (mx < 0 || my < 0 || mx >= MAP_W || my >= MAP_H) {
        return 1;
    }
    return world_map[my][mx] != 0u;
}

/* Check the player's collision radius against nearby wall tiles. */
static uint8_t is_walkable(float x, float y)
{
    if (is_wall(x, y)) {
        return 0;
    }

    if (is_wall(x - PLAYER_RADIUS, y)) return 0;
    if (is_wall(x + PLAYER_RADIUS, y)) return 0;
    if (is_wall(x, y - PLAYER_RADIUS)) return 0;
    if (is_wall(x, y + PLAYER_RADIUS)) return 0;

    return 1;
}

/* Convenience wrapper for a single short beep. */
static void trigger_beep(ZombieEngine_t* engine, uint32_t freq, uint16_t duration_ms)
{
    uint16_t f[1];
    uint8_t v[1];
    uint8_t d[1];
    f[0] = (uint16_t)freq;
    v[0] = BEEP_VOLUME;
    d[0] = (duration_ms > 255u) ? 255u : (uint8_t)duration_ms;
    start_audio_sequence(engine, f, v, d, 1u);
}

/* Advance the current audio envelope once its active tone expires. */
static void update_beep(ZombieEngine_t* engine, uint32_t now)
{
    if (!engine->audio_busy) {
        return;
    }

    if (now < engine->audio_step_end_tick) {
        return;
    }

    engine->audio_step_idx++;
    if (engine->audio_step_idx >= engine->audio_step_count) {
        buzzer_off(&buzzer_cfg);
        engine->audio_busy = 0u;
        engine->audio_step_count = 0u;
        engine->audio_step_idx = 0u;
        return;
    }

    buzzer_tone(&buzzer_cfg,
                engine->audio_step_freq[engine->audio_step_idx],
                engine->audio_step_vol[engine->audio_step_idx]);
    engine->audio_step_end_tick = now + (uint32_t)engine->audio_step_dur[engine->audio_step_idx];
}

/* Clear one-frame feedback flags and age temporary impact effects. */
static void reset_frame_events(ZombieEngine_t* engine)
{
    engine->shot_fired_this_frame = 0;
    engine->hit_this_frame = 0;
    engine->hurt_this_frame = 0;

    if (engine->shot_trace_ttl > 0u) {
        engine->shot_trace_ttl--;
    }
    for (uint8_t i = 0; i < ZOMBIE_IMPACT_MAX; i++) {
        if (engine->impacts[i].ttl > 0u) {
            engine->impacts[i].ttl--;
        }
    }
}

/* Determine whether the current weapon should promote or end the game. */
static void promote_weapon_if_needed(ZombieEngine_t* engine)
{
    if (engine->current_weapon == ZOMBIE_WEAPON_GHOST && engine->stage_kills >= 3u) {
        engine->current_weapon = ZOMBIE_WEAPON_VANDAL;
        engine->stage_kills = 0u;
        engine->reload_active = 0;
        engine->shoot_was_down = 0u;
        engine->odin_spinup = 0u;
        engine->ammo_in_mag = get_weapon_mag_size(ZOMBIE_WEAPON_VANDAL);
        trigger_beep(engine, 2800u, 45u);
    } else if (engine->current_weapon == ZOMBIE_WEAPON_VANDAL && engine->stage_kills >= 5u) {
        engine->current_weapon = ZOMBIE_WEAPON_ODIN;
        engine->stage_kills = 0u;
        engine->reload_active = 0;
        engine->shoot_was_down = 0u;
        engine->odin_spinup = 0u;
        engine->ammo_in_mag = get_weapon_mag_size(ZOMBIE_WEAPON_ODIN);
        trigger_beep(engine, 2800u, 45u);
    } else if (engine->current_weapon == ZOMBIE_WEAPON_ODIN && engine->stage_kills >= 10u) {
        engine->victory = 1u;
        engine->game_over = 1u;
        trigger_beep(engine, 4000u, 70u);
    }
}

/* Start a reload timer if the weapon actually needs ammo. */
static void start_reload(ZombieEngine_t* engine, uint32_t now)
{
    uint16_t mag_size = get_weapon_mag_size(engine->current_weapon);

    if (engine->reload_active) {
        return;
    }
    if (engine->ammo_in_mag >= mag_size) {
        return;
    }
    if (engine->ammo_reserve == 0) {
        return;
    }

    engine->reload_active = 1;
    engine->odin_spinup = 0u;
    engine->reload_finish_tick = now + (uint32_t)get_weapon_reload_time(engine->current_weapon);
    trigger_beep(engine, 560u, 24u);
}

/* Finish a reload once its timer expires and move ammo from reserve to mag. */
static void update_reload(ZombieEngine_t* engine, uint32_t now)
{
    if (!engine->reload_active || now < engine->reload_finish_tick) {
        return;
    }

    uint16_t mag_size = get_weapon_mag_size(engine->current_weapon);
    uint16_t need = (uint16_t)(mag_size - engine->ammo_in_mag);
    uint16_t take = (engine->ammo_reserve < need) ? engine->ammo_reserve : need;

    engine->ammo_in_mag = (uint16_t)(engine->ammo_in_mag + take);
    engine->ammo_reserve = (uint16_t)(engine->ammo_reserve - take);
    engine->reload_active = 0;
    trigger_beep(engine, 980u, 20u);
}

/* Let recoil settle back toward zero a little bit each frame. */
static void update_recoil(ZombieEngine_t* engine)
{
    float recoil_decay = get_weapon_recoil_decay(engine->current_weapon);

    if (engine->recoil_rad > recoil_decay) {
        engine->recoil_rad -= recoil_decay;
    } else {
        engine->recoil_rad = 0.0f;
    }
}

/* Spawn one zombie on a random open tile that is not too close to the player. */
static void spawn_enemy(ZombieEngine_t* engine)
{
    if (engine->enemies_alive >= ZOMBIE_MAX_ENEMIES) {
        return;
    }

    for (uint8_t attempt = 0; attempt < 30; attempt++) {
        uint16_t rx = Random_U16((uint16_t)(MAP_W - 2)) + 1;
        uint16_t ry = Random_U16((uint16_t)(MAP_H - 2)) + 1;

        float ex = (float)rx + 0.5f;
        float ey = (float)ry + 0.5f;

        if (is_wall(ex, ey)) {
            continue;
        }

        float dx = ex - engine->player_x;
        float dy = ey - engine->player_y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 < 9.0f) {
            continue;
        }

        for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
            if (!engine->enemies[i].alive) {
                engine->enemies[i].x = ex;
                engine->enemies[i].y = ey;
                engine->enemies[i].alive = 1;
                engine->enemies_alive++;
                return;
            }
        }
    }
}

/* Grant health and ammo rewards when the player kills zombies. */
static void apply_kill_rewards(ZombieEngine_t* engine, uint8_t kill_count)
{
    if (kill_count == 0u) {
        return;
    }
    
    /* Restore health per kill (capped at 100) */
    uint16_t new_health = (uint16_t)engine->health + (uint16_t)(kill_count * KILL_HP_REWARD);
    engine->health = (new_health > 100u) ? 100u : (uint8_t)new_health;
    
    /* Restore ammo per kill to reserve (capped to three magazines of current weapon) */
    uint16_t reserve_cap = get_weapon_reserve_cap(engine);
    uint16_t new_reserve = (uint16_t)engine->ammo_reserve + (uint16_t)(kill_count * KILL_AMMO_REWARD);
    engine->ammo_reserve = (new_reserve > reserve_cap) ? reserve_cap : new_reserve;
}

/* March along a segment to check whether walls block the shot or sight line. */
static uint8_t line_of_sight_clear(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);

    if (len <= 0.0001f) {
        return 1;
    }

    float sx = dx / len;
    float sy = dy / len;

    for (float t = 0.18f; t < len; t += 0.10f) {
        float px = x0 + sx * t;
        float py = y0 + sy * t;
        if (is_wall(px, py)) {
            return 0;
        }
    }

    return 1;
}

/* Push a new hit spark into the circular FX buffer and expose its trace point. */
static void add_impact_fx(ZombieEngine_t* engine, int16_t sx, int16_t sy, uint8_t color, uint8_t ttl)
{
    if (sx < 3 || sx >= (SCREEN_WIDTH - 3) || sy < 16 || sy >= (SCREEN_HEIGHT - 3)) {
        return;
    }

    uint8_t idx = engine->impacts_head;
    engine->impacts[idx].x = sx;
    engine->impacts[idx].y = sy;
    engine->impacts[idx].ttl = ttl;
    engine->impacts[idx].color = color;
    engine->impacts_head = (uint8_t)((idx + 1u) % ZOMBIE_IMPACT_MAX);

    engine->shot_trace_ttl = 2u;
    engine->shot_trace_x = sx;
    engine->shot_trace_y = sy;
}

/* Project a world-space point into screen-space for simple hit effects. */
static uint8_t world_to_screen(const ZombieEngine_t* engine, float wx, float wy, int16_t* out_x, int16_t* out_y)
{
    float dx = wx - engine->player_x;
    float dy = wy - engine->player_y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.01f) {
        return 0;
    }

    float rel = normalize_angle(atan2f(dy, dx) - engine->player_angle);
    if (fabsf(rel) > HALF_FOV_RAD + 0.03f) {
        return 0;
    }

    float sx = (SCREEN_WIDTH * 0.5f) + (rel / HALF_FOV_RAD) * (SCREEN_WIDTH * 0.5f);
    float proj_plane = (SCREEN_WIDTH * 0.5f) / tanf(HALF_FOV_RAD);
    float sy = (SCREEN_HEIGHT * 0.5f) + (proj_plane / dist) * 0.10f;
    if (sy > (SCREEN_HEIGHT - 12)) {
        sy = (float)(SCREEN_HEIGHT - 12);
    }

    *out_x = (int16_t)sx;
    *out_y = (int16_t)sy;
    return 1;
}

/* Trace forward until the shot ray reaches the first wall tile. */
static uint8_t first_wall_hit(const ZombieEngine_t* engine, float ang, float* out_x, float* out_y)
{
    float dx = cosf(ang);
    float dy = sinf(ang);
    float dist = 0.20f;

    while (dist < MAX_VIEW_DISTANCE) {
        float px = engine->player_x + dx * dist;
        float py = engine->player_y + dy * dist;
        if (is_wall(px, py)) {
            *out_x = px;
            *out_y = py;
            return 1;
        }
        dist += 0.06f;
    }

    return 0;
}

/* Find the nearest visible zombie inside the requested aim cone and range. */
static int acquire_target(const ZombieEngine_t* engine,
                          float aim_angle,
                          float cone_rad,
                          float range_sq,
                          const uint8_t already_hit[ZOMBIE_MAX_ENEMIES])
{
    int best_idx = -1;
    float best_dist_sq = range_sq + 1.0f;

    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        if (!engine->enemies[i].alive) {
            continue;
        }
        if (already_hit != NULL && already_hit[i]) {
            continue;
        }

        float dx = engine->enemies[i].x - engine->player_x;
        float dy = engine->enemies[i].y - engine->player_y;
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq > range_sq || dist_sq >= best_dist_sq) {
            continue;
        }

        float ang = atan2f(dy, dx);
        float delta = normalize_angle(ang - aim_angle);
        if (fabsf(delta) > cone_rad) {
            continue;
        }

        if (!line_of_sight_clear(engine->player_x, engine->player_y,
                                 engine->enemies[i].x, engine->enemies[i].y)) {
            continue;
        }

        best_dist_sq = dist_sq;
        best_idx = (int)i;
    }

    return best_idx;
}

/* Apply all kills found this frame and return how many zombies were removed. */
static uint8_t remove_hit_enemies(ZombieEngine_t* engine, const uint8_t enemy_hit[ZOMBIE_MAX_ENEMIES])
{
    uint8_t kills = 0;

    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        if (!enemy_hit[i] || !engine->enemies[i].alive) {
            continue;
        }
        engine->enemies[i].alive = 0;
        engine->enemies_alive--;
        engine->score++;
        kills++;
    }

    /* Apply kill rewards: restore health and ammo */
    apply_kill_rewards(engine, kills);

    if (kills > 0u) {
        engine->stage_kills = (uint16_t)(engine->stage_kills + kills);
        if (engine->current_weapon == ZOMBIE_WEAPON_ODIN) {
            engine->final_weapon_kills = (uint16_t)(engine->final_weapon_kills + kills);
        }
    }

    return kills;
}

/* Resolve one trigger pull: ammo, targeting, hit FX, score and weapon audio. */
static void try_shoot(ZombieEngine_t* engine)
{
    uint16_t cooldown_ms = get_weapon_cooldown_ms(engine);
    float weapon_range = GHOST_RANGE;
    float base_cone = GHOST_CONE;
    float recoil_per_shot = GHOST_RECOIL_PER_SHOT;
    float recoil_cap = GHOST_RECOIL_CAP;
    uint32_t now = HAL_GetTick();
    uint8_t enemy_hit[ZOMBIE_MAX_ENEMIES];
    float range_sq;
    float effective_cone;

    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        enemy_hit[i] = 0u;
    }

    if (engine->current_weapon == ZOMBIE_WEAPON_VANDAL) {
        weapon_range = VANDAL_RANGE;
        base_cone = VANDAL_CONE;
        recoil_per_shot = VANDAL_RECOIL_PER_SHOT;
        recoil_cap = VANDAL_RECOIL_CAP;
    } else if (engine->current_weapon == ZOMBIE_WEAPON_ODIN) {
        weapon_range = ODIN_RANGE;
        base_cone = ODIN_CONE;
        recoil_per_shot = ODIN_RECOIL_PER_SHOT;
        recoil_cap = ODIN_RECOIL_CAP;
    }

    range_sq = weapon_range * weapon_range;
    effective_cone = base_cone; // No random spread, but keep base aiming cone

    if (engine->reload_active) {
        engine->odin_spinup = 0u;
        return;
    }
    if ((now - engine->last_shot_tick) < (uint32_t)cooldown_ms) {
        return;
    }
    if (engine->ammo_in_mag == 0u) {
        engine->odin_spinup = 0u;
        start_reload(engine, now);
        return;
    }

    engine->last_shot_tick = now;
    engine->shot_fired_this_frame = 1;
    engine->ammo_in_mag--;
    engine->recoil_rad += recoil_per_shot;
    if (engine->recoil_rad > recoil_cap) {
        engine->recoil_rad = recoil_cap;
    }

    if (engine->current_weapon == ZOMBIE_WEAPON_ODIN) {
        if (engine->odin_spinup < ODIN_SPINUP_MAX) {
            engine->odin_spinup++;
        }
    } else {
        engine->odin_spinup = 0u;
    }

    {
        int idx = acquire_target(engine, engine->player_angle, effective_cone, range_sq, NULL);
        if (idx >= 0) {
            enemy_hit[idx] = 1u;
        }
    }

    uint8_t any_enemy_hit = 0u;
    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        if (enemy_hit[i]) {
            any_enemy_hit = 1u;
            break;
        }
    }

    uint8_t kills = remove_hit_enemies(engine, enemy_hit);
    int16_t impact_x = SCREEN_WIDTH / 2;
    int16_t impact_y = SCREEN_HEIGHT / 2;
    uint8_t impact_drawn = 0u;

    /* Prefer drawing the hit spark where the resolved enemy or wall actually was. */
    if (any_enemy_hit) {
        for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
            if (!enemy_hit[i]) {
                continue;
            }

            if (world_to_screen(engine, engine->enemies[i].x, engine->enemies[i].y, &impact_x, &impact_y)) {
                add_impact_fx(engine, impact_x, impact_y, 2u, 8u);
                impact_drawn = 1u;
            }
        }
    } else {
        float hx = 0.0f;
        float hy = 0.0f;
        if (first_wall_hit(engine, engine->player_angle, &hx, &hy) &&
            world_to_screen(engine, hx, hy, &impact_x, &impact_y)) {
            add_impact_fx(engine, impact_x, impact_y, 1u, 9u);
            impact_drawn = 1u;
        }
    }

    if (!impact_drawn) {
        int16_t jitter = (int16_t)(Random_U16(7u)) - 3;
        add_impact_fx(engine, (int16_t)(SCREEN_WIDTH / 2 + jitter), (int16_t)(SCREEN_HEIGHT / 2 + jitter), 1u, 6u);
    }

    if (kills > 0u) {
        engine->hit_this_frame = 1;

        /* Odin acts as the final challenge weapon and can end the run in victory. */
        if (engine->current_weapon == ZOMBIE_WEAPON_ODIN) {
            if (engine->final_weapon_kills >= ODIN_VICTORY_KILLS) {
                engine->victory = 1u;
                engine->game_over = 1u;
                /* Victory chime: rising short arpeggio */
                {
                    static const uint16_t vf[] = {1200u, 1600u, 2100u, 2600u, 3200u};
                    static const uint8_t vv[] = {55u, 60u, 65u, 72u, 80u};
                    static const uint8_t vd[] = {18u, 18u, 22u, 24u, 40u};
                    start_audio_sequence(engine, vf, vv, vd, 5u);
                }
            }
        }
    }
    if (!engine->victory) {
        play_weapon_shot_sound(engine, engine->current_weapon);
    }

    if (engine->ammo_in_mag == 0u) {
        start_reload(engine, now);
    }
}

// update player position based on input
static void update_player(ZombieEngine_t* engine, Joystick_t* move_input, Joystick_t* look_input)
{
    // init variables
    float turn = 0.0f;
    float forward = 0.0f;
    float strafe = 0.0f;
    float move_amount = 0.0f;

    // left stick for forward and strafe
    if (move_input->coord_mapped.y > 0.02f || move_input->coord_mapped.y < -0.02f) {
        forward = move_input->coord_mapped.y;
    }
    if (move_input->coord_mapped.x > 0.02f || move_input->coord_mapped.x < -0.02f) {
        strafe = move_input->coord_mapped.x;
    }

    // right stick for turn
    if (look_input->coord_mapped.x > 0.02f || look_input->coord_mapped.x < -0.02f) {
        turn = look_input->coord_mapped.x;
    }

    // calculate move for spread
    move_amount = fabsf(forward) + fabsf(strafe);
    if (move_amount > 1.0f) {
        move_amount = 1.0f;
    }
    engine->move_spread_rad = move_amount * get_weapon_move_spread(engine->current_weapon);

    // update angle
    engine->player_angle = normalize_angle(engine->player_angle + turn * PLAYER_TURN_SPEED);

    // calculate direction vectors
    float fwd_x = cosf(engine->player_angle);
    float fwd_y = sinf(engine->player_angle);
    float right_x = cosf(engine->player_angle + (float)M_PI / 2.0f);
    float right_y = sinf(engine->player_angle + (float)M_PI / 2.0f);

    // calculate new position
    float new_x = engine->player_x + fwd_x * forward * PLAYER_MOVE_SPEED + right_x * strafe * PLAYER_MOVE_SPEED;
    float new_y = engine->player_y + fwd_y * forward * PLAYER_MOVE_SPEED + right_y * strafe * PLAYER_MOVE_SPEED;

    // check collision
    if (is_walkable(new_x, engine->player_y)) {
        engine->player_x = new_x;
    }
    if (is_walkable(engine->player_x, new_y)) {
        engine->player_y = new_y;
    }
}

/* Move zombies toward the player and apply bite damage on close contact. */
static void update_enemies(ZombieEngine_t* engine)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        if (!engine->enemies[i].alive) {
            continue;
        }

        float dx = engine->player_x - engine->enemies[i].x;
        float dy = engine->player_y - engine->enemies[i].y;
        float dist_sq = dx * dx + dy * dy;
        float dist = sqrtf(dist_sq);

        if (dist > 0.001f) {
            float vx = dx / dist;
            float vy = dy / dist;

            float nx = engine->enemies[i].x + vx * ZOMBIE_SPEED;
            float ny = engine->enemies[i].y + vy * ZOMBIE_SPEED;

            /* Axis-separated movement keeps simple wall sliding from getting stuck. */
            if (!is_wall(nx, engine->enemies[i].y)) {
                engine->enemies[i].x = nx;
            }
            if (!is_wall(engine->enemies[i].x, ny)) {
                engine->enemies[i].y = ny;
            }
        }

        if (dist < ZOMBIE_ATTACK_RANGE && (now - engine->last_damage_tick) >= DAMAGE_COOLDOWN_MS) {
            engine->last_damage_tick = now;
            if (engine->health > 2u) {
                engine->health -= 2u;
            } else {
                engine->health = 0u;
            }
            engine->hurt_this_frame = 1;
            trigger_beep(engine, HURT_BEEP_FREQ, (uint16_t)(BEEP_MS + 12u));
            if (engine->health == 0u) {
                engine->game_over = 1u;
                ZombieEngine_StopAudio(engine);
                return;
            }
        }
    }
}

// init engine, reset all states
static void ZombieEngine_Init(ZombieEngine_t* engine)
{
    // set player start position
    engine->player_x = 2.5f;
    engine->player_y = 2.5f;
    engine->player_angle = 0.0f;

    // set health and score
    engine->health = 100;
    engine->score = 0;
    engine->game_over = 0;
    engine->victory = 0;

    // reset enemies
    engine->enemies_alive = 0;
    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        engine->enemies[i].alive = 0;
        engine->enemies[i].x = 0.0f;
        engine->enemies[i].y = 0.0f;
    }

    // get current time
    uint32_t now = HAL_GetTick();
    engine->last_spawn_tick = now;
    engine->spawn_interval_ms = BASE_SPAWN_INTERVAL_MS;
    engine->last_shot_tick = 0;
    engine->last_damage_tick = 0;

    // set start weapon
    engine->current_weapon = ZOMBIE_WEAPON_GHOST;
    engine->ammo_in_mag = get_weapon_mag_size(engine->current_weapon);
    engine->ammo_reserve = START_RESERVE_AMMO;
    engine->stage_kills = 0u;
    engine->final_weapon_kills = 0u;
    engine->recoil_rad = 0.0f;
    engine->move_spread_rad = 0.0f;
    engine->reload_active = 0u;
    engine->shoot_was_down = 0u;
    engine->odin_spinup = 0u;
    engine->audio_busy = 0u;
    engine->audio_step_idx = 0u;
    engine->audio_step_count = 0u;
    engine->audio_step_end_tick = 0u;
    engine->reload_finish_tick = 0u;
    engine->impacts_head = 0u;
    engine->shot_trace_ttl = 0u;
    engine->shot_trace_x = SCREEN_WIDTH / 2;
    engine->shot_trace_y = SCREEN_HEIGHT / 2;
    for (uint8_t i = 0; i < ZOMBIE_IMPACT_MAX; i++) {
        engine->impacts[i].ttl = 0u;
        engine->impacts[i].x = 0;
        engine->impacts[i].y = 0;
        engine->impacts[i].color = 1u;
    }

    // reset frame events
    reset_frame_events(engine);

    // spawn initial enemies
    spawn_enemy(engine);
    spawn_enemy(engine);
    spawn_enemy(engine);
    spawn_enemy(engine);
    spawn_enemy(engine);
}

// update one frame
static void ZombieEngine_Update(ZombieEngine_t* engine, Joystick_t* move_input, Joystick_t* look_input, uint8_t shoot_pressed, uint8_t reload_pressed)
{
    if (engine->game_over) {
        return;
    }

    uint32_t now = HAL_GetTick();

    /* Housekeeping first so later gameplay logic sees fresh timers/state. */
    update_beep(engine, now);
    update_reload(engine, now);
    update_recoil(engine);
    reset_frame_events(engine);

    /* Player actions happen before AI so shots feel immediate. */
    update_player(engine, move_input, look_input);

    if (reload_pressed) {
        start_reload(engine, now);
    }

    if (!shoot_pressed) {
        engine->shoot_was_down = 0u;
        if (engine->current_weapon == ZOMBIE_WEAPON_ODIN &&
            (now - engine->last_shot_tick) >= ODIN_SPINUP_RESET_MS) {
            engine->odin_spinup = 0u;
        }
    }

    if (shoot_pressed && (!weapon_is_semi_auto(engine->current_weapon) || !engine->shoot_was_down)) {
        try_shoot(engine);
    }
    if (shoot_pressed) {
        engine->shoot_was_down = 1u;
    }

    promote_weapon_if_needed(engine);
    if (engine->game_over) {
        return;
    }

    update_enemies(engine);

    if (engine->game_over) {
        return;
    }

    /* Higher score means faster spawns and a larger desired zombie population. */
    uint32_t desired_interval = BASE_SPAWN_INTERVAL_MS;
    uint32_t score_drop = (uint32_t)engine->score * 70u;
    if (score_drop < (BASE_SPAWN_INTERVAL_MS - MIN_SPAWN_INTERVAL_MS)) {
        desired_interval -= score_drop;
    } else {
        desired_interval = MIN_SPAWN_INTERVAL_MS;
    }
    engine->spawn_interval_ms = desired_interval;

    uint8_t target_alive = (uint8_t)(8u + (engine->score / 2u));
    if (target_alive > ZOMBIE_MAX_ENEMIES) {
        target_alive = ZOMBIE_MAX_ENEMIES;
    }

    if ((now - engine->last_spawn_tick) >= engine->spawn_interval_ms && engine->enemies_alive < target_alive) {
        engine->last_spawn_tick = now;
        spawn_enemy(engine);
    }
}

// render one frame with walls, sprites, HUD and effects
static void ZombieEngine_Draw(ZombieEngine_t* engine)
{
    // calculate projection plane
    const float proj_plane = (SCREEN_WIDTH * 0.5f) / tanf(HALF_FOV_RAD);

    // clear screen
    LCD_Fill_Buffer(9);
    LCD_Draw_Rect(0, SCREEN_HEIGHT / 2, SCREEN_WIDTH, SCREEN_HEIGHT / 2, 12, 1);

    // first pass: ray-march walls and fill depth buffer for sprite occlusion
    for (int screen_x = 0; screen_x < SCREEN_WIDTH; screen_x++) {
        // calculate ray offset
        float ray_offset = (((float)screen_x / (float)(SCREEN_WIDTH - 1)) - 0.5f) * FOV_RAD;
        float ray_angle = engine->player_angle + ray_offset;
        float ray_dx = cosf(ray_angle);
        float ray_dy = sinf(ray_angle);

        // cast ray
        float distance = 0.08f;
        while (distance < MAX_VIEW_DISTANCE) {
            float px = engine->player_x + ray_dx * distance;
            float py = engine->player_y + ray_dy * distance;
            if (is_wall(px, py)) {
                break;
            }
            distance += RAY_STEP;
        }

        // correct distance
        float corrected_dist = distance * cosf(ray_offset);
        if (corrected_dist < 0.05f) {
            corrected_dist = 0.05f;
        }
        if (corrected_dist > MAX_VIEW_DISTANCE) {
            corrected_dist = MAX_VIEW_DISTANCE;
        }

        // calculate wall height
        int wall_height = (int)(proj_plane / corrected_dist);
        if (wall_height > SCREEN_HEIGHT) {
            wall_height = SCREEN_HEIGHT;
        }

        // calculate wall position
        int y_start = (SCREEN_HEIGHT / 2) - (wall_height / 2);
        int y_end = (SCREEN_HEIGHT / 2) + (wall_height / 2);
        if (y_start < 0) y_start = 0;
        if (y_end >= SCREEN_HEIGHT) y_end = SCREEN_HEIGHT - 1;

        // choose wall color
        uint8_t wall_color = 1;
        if (corrected_dist > 2.5f) wall_color = 13;
        if (corrected_dist > 4.5f) wall_color = 7;
        if (corrected_dist > 7.0f) wall_color = 8;

        // set depth buffer
        depth_buffer[screen_x] = corrected_dist;
        // draw wall
        LCD_Draw_Line((uint16_t)screen_x, (uint16_t)y_start, (uint16_t)screen_x, (uint16_t)y_end, wall_color);

        // add edge texture
        if ((screen_x & 3) == 0) {
            LCD_Set_Pixel((uint16_t)screen_x, (uint16_t)y_start, 14);
            if (y_end > y_start) {
                LCD_Set_Pixel((uint16_t)screen_x, (uint16_t)y_end, 14);
            }
        }
    }

    /* Sort zombies back-to-front so nearer sprites overwrite farther ones cleanly. */
    float dist_arr[ZOMBIE_MAX_ENEMIES];
    int idx_arr[ZOMBIE_MAX_ENEMIES];
    int count = 0;

    for (uint8_t i = 0; i < ZOMBIE_MAX_ENEMIES; i++) {
        if (!engine->enemies[i].alive) {
            continue;
        }
        float dx = engine->enemies[i].x - engine->player_x;
        float dy = engine->enemies[i].y - engine->player_y;
        dist_arr[count] = dx * dx + dy * dy;
        idx_arr[count] = (int)i;
        count++;
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (dist_arr[i] < dist_arr[j]) {
                float td = dist_arr[i];
                int ti = idx_arr[i];
                dist_arr[i] = dist_arr[j];
                idx_arr[i] = idx_arr[j];
                dist_arr[j] = td;
                idx_arr[j] = ti;
            }
        }
    }

    /* Third pass: render zombies as layered pixel sprites with depth testing. */
    for (int n = 0; n < count; n++) {
        ZombieEnemy_t* z = &engine->enemies[idx_arr[n]];
        float dx = z->x - engine->player_x;
        float dy = z->y - engine->player_y;
        float dist_sq = dx * dx + dy * dy;

        if (dist_sq < 0.04f || dist_sq > MAX_VIEW_DISTANCE_SQ) {
            continue;
        }

        float dist = sqrtf(dist_sq);
        float ang = atan2f(dy, dx);
        float rel = normalize_angle(ang - engine->player_angle);

        if (fabsf(rel) > HALF_FOV_RAD + 0.2f) {
            continue;
        }

        float screen_x = (SCREEN_WIDTH * 0.5f) + (rel / HALF_FOV_RAD) * (SCREEN_WIDTH * 0.5f);
        int sprite_h = (int)(proj_plane / dist);
        if (sprite_h < 6) sprite_h = 6;
        if (sprite_h > 180) sprite_h = 180;

        int sprite_w = sprite_h / 2;
        if (sprite_w < 4) sprite_w = 4;
        int x0 = (int)screen_x - sprite_w / 2;
        int y0 = (SCREEN_HEIGHT / 2) - sprite_h / 2;
        int bob = ((HAL_GetTick() / 90u) + (uint32_t)idx_arr[n]) & 1u;
        y0 += bob ? 1 : -1;

        int center_x = sprite_w / 2;
        int head_bottom = (sprite_h * 27) / 100;
        int torso_top = (sprite_h * 21) / 100;
        int torso_bottom = (sprite_h * 70) / 100;
        int leg_top = (sprite_h * 66) / 100;
        int torso_span = torso_bottom - torso_top;
        if (torso_span < 1) torso_span = 1;

        for (int x = 0; x < sprite_w; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= SCREEN_WIDTH) {
                continue;
            }

            if (dist > depth_buffer[sx]) {
                continue;
            }

            int local_x = x - center_x;
            int abs_x = local_x < 0 ? -local_x : local_x;

            for (int y = 0; y < sprite_h; y++) {
                int sy = y0 + y;
                if (sy < 0 || sy >= SCREEN_HEIGHT) {
                    continue;
                }

                uint8_t c = 255u;
                int shape_edge = 0;

                if (y < head_bottom) {
                    int head_mid = head_bottom / 2;
                    int head_curve = y - head_mid;
                    if (head_curve < 0) head_curve = -head_curve;
                    int head_half = (sprite_w * 30) / 100 + ((head_mid - head_curve) * sprite_w) / (head_bottom * 5 + 1);
                    if (head_half < 1) head_half = 1;

                    if (abs_x <= head_half) {
                        c = 3;
                        shape_edge = (abs_x == head_half || y == 0 || y == head_bottom - 1);
                    }
                } else if (y >= torso_top && y < torso_bottom) {
                    int t = y - torso_top;
                    int torso_half = (sprite_w / 2) - ((t * sprite_w) / (torso_span * 7));
                    if (torso_half < sprite_w / 4) torso_half = sprite_w / 4;

                    if (abs_x <= torso_half) {
                        c = 13;
                        shape_edge = (abs_x >= torso_half - 1);

                        if (((x + y + idx_arr[n]) & 7) == 0) {
                            c = 1;
                        }
                        if (abs_x <= 1 && y > torso_top + 1 && y < torso_bottom - 2) {
                            c = 2;
                        } else if ((x < center_x && y > torso_top + torso_span / 3 && ((y + idx_arr[n]) & 5) == 0) ||
                                   (x > center_x && y > torso_top + torso_span / 2 && ((y + idx_arr[n]) & 6) == 0)) {
                            c = 4;
                        }
                    } else if (y > torso_top + 2 && y < torso_bottom - 3) {
                        int arm_sway = bob ? 1 : -1;
                        int left_arm = center_x - torso_half - ((y - torso_top) / 5) - 1 + arm_sway;
                        int right_arm = center_x + torso_half + ((y - torso_top) / 5) + 1 + arm_sway;

                        if ((x >= left_arm - 1 && x <= left_arm + 1) ||
                            (x >= right_arm - 1 && x <= right_arm + 1)) {
                            c = 3;
                            shape_edge = 1;
                        }
                    }
                } else if (y >= leg_top) {
                    int leg_half = sprite_w / 8;
                    if (leg_half < 1) leg_half = 1;
                    int step = bob ? 1 : -1;
                    int left_leg = center_x - sprite_w / 6 - ((y - leg_top) / 8) + step;
                    int right_leg = center_x + sprite_w / 6 + ((y - leg_top) / 8) - step;

                    if ((x >= left_leg - leg_half && x <= left_leg + leg_half) ||
                        (x >= right_leg - leg_half && x <= right_leg + leg_half)) {
                        c = (y > sprite_h - 6) ? 0 : 13;
                        shape_edge = ((x == left_leg - leg_half) || (x == left_leg + leg_half) ||
                                      (x == right_leg - leg_half) || (x == right_leg + leg_half));
                    }
                }

                if (c == 255u) {
                    continue;
                }
                if (shape_edge && sprite_h > 14) {
                    c = 0;
                } else if (x < center_x / 2 && c != 2) {
                    c = (c == 3) ? 13 : c;
                } else if (x > center_x + center_x / 2 && c == 13) {
                    c = 8;
                }

                LCD_Set_Pixel((uint16_t)sx, (uint16_t)sy, c);
            }
        }

        int eye_y = y0 + sprite_h / 7;
        int eye_w = sprite_w / 8;
        if (eye_w < 1) eye_w = 1;
        if (sprite_h > 10) {
            for (int ex = 0; ex < eye_w; ex++) {
                int lx = (int)screen_x - sprite_w / 6 + ex;
                int rx = (int)screen_x + sprite_w / 6 + ex;
                if (lx >= 0 && lx < SCREEN_WIDTH && eye_y >= 0 && eye_y < SCREEN_HEIGHT && dist <= depth_buffer[lx]) {
                    LCD_Set_Pixel((uint16_t)lx, (uint16_t)eye_y, 2);
                }
                if (rx >= 0 && rx < SCREEN_WIDTH && eye_y >= 0 && eye_y < SCREEN_HEIGHT && dist <= depth_buffer[rx]) {
                    LCD_Set_Pixel((uint16_t)rx, (uint16_t)eye_y, 2);
                }
            }
        }

        if (sprite_h > 18) {
            int mouth_y = y0 + (sprite_h * 19) / 100;
            int mouth_l = (int)screen_x - sprite_w / 9;
            int mouth_r = (int)screen_x + sprite_w / 9;
            if (mouth_y >= 0 && mouth_y < SCREEN_HEIGHT) {
                if (mouth_l >= 0 && mouth_l < SCREEN_WIDTH && dist <= depth_buffer[mouth_l]) {
                    LCD_Set_Pixel((uint16_t)mouth_l, (uint16_t)mouth_y, 0);
                }
                if (mouth_r >= 0 && mouth_r < SCREEN_WIDTH && dist <= depth_buffer[mouth_r]) {
                    LCD_Set_Pixel((uint16_t)mouth_r, (uint16_t)mouth_y, 0);
                }
            }
        }

        if (sprite_h > 20) {
            int collar_y = y0 + torso_top + 1;
            int tie_x = (int)screen_x;
            int collar_l = tie_x - sprite_w / 7;
            int collar_r = tie_x + sprite_w / 7;
            if (collar_y >= 0 && collar_y < SCREEN_HEIGHT) {
                if (collar_l >= 0 && collar_l < SCREEN_WIDTH && dist <= depth_buffer[collar_l]) {
                    LCD_Set_Pixel((uint16_t)collar_l, (uint16_t)collar_y, 1);
                }
                if (collar_r >= 0 && collar_r < SCREEN_WIDTH && dist <= depth_buffer[collar_r]) {
                    LCD_Set_Pixel((uint16_t)collar_r, (uint16_t)collar_y, 1);
                }
                if (tie_x >= 0 && tie_x < SCREEN_WIDTH && dist <= depth_buffer[tie_x]) {
                    LCD_Set_Pixel((uint16_t)tie_x, (uint16_t)(collar_y + 1), 2);
                    LCD_Set_Pixel((uint16_t)tie_x, (uint16_t)(collar_y + 2), 2);
                }
            }
        }

        if (sprite_h > 30) {
            int scar_x = (int)screen_x + sprite_w / 10;
            int scar_y = y0 + sprite_h / 10;
            for (int s = 0; s < 3; s++) {
                int px = scar_x + s;
                int py = scar_y + s;
                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT && dist <= depth_buffer[px]) {
                    LCD_Set_Pixel((uint16_t)px, (uint16_t)py, 2);
                }
            }
        }
    }

    /* Finally draw aim feedback, impact FX, HUD and the weapon overlay. */
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;
    int cross_gap = 4 + (int)((engine->recoil_rad + engine->move_spread_rad) * 42.0f);
    if (cross_gap > 13) {
        cross_gap = 13;
    }
    uint8_t cross_col = engine->shot_fired_this_frame ? 2 : 3;
    LCD_Draw_Line((uint16_t)(cx - cross_gap), (uint16_t)cy, (uint16_t)(cx + cross_gap), (uint16_t)cy, cross_col);
    LCD_Draw_Line((uint16_t)cx, (uint16_t)(cy - cross_gap), (uint16_t)cx, (uint16_t)(cy + cross_gap), cross_col);

    if (engine->hit_this_frame) {
        LCD_Draw_Line((uint16_t)(cx - 8), (uint16_t)(cy - 8), (uint16_t)(cx - 5), (uint16_t)(cy - 5), 2);
        LCD_Draw_Line((uint16_t)(cx + 5), (uint16_t)(cy + 5), (uint16_t)(cx + 8), (uint16_t)(cy + 8), 2);
        LCD_Draw_Line((uint16_t)(cx - 8), (uint16_t)(cy + 8), (uint16_t)(cx - 5), (uint16_t)(cy + 5), 2);
        LCD_Draw_Line((uint16_t)(cx + 5), (uint16_t)(cy - 5), (uint16_t)(cx + 8), (uint16_t)(cy - 8), 2);
    }

    if (engine->shot_trace_ttl > 0u) {
        int mx = SCREEN_WIDTH / 2;
        int my = SCREEN_HEIGHT - 22;
        LCD_Draw_Line((uint16_t)mx, (uint16_t)my,
                      (uint16_t)engine->shot_trace_x, (uint16_t)engine->shot_trace_y, 14);
    }

    for (uint8_t i = 0; i < ZOMBIE_IMPACT_MAX; i++) {
        if (engine->impacts[i].ttl == 0u) {
            continue;
        }
        uint8_t c = engine->impacts[i].color;
        int16_t ix = engine->impacts[i].x;
        int16_t iy = engine->impacts[i].y;
        LCD_Set_Pixel((uint16_t)ix, (uint16_t)iy, c);
        LCD_Set_Pixel((uint16_t)(ix - 1), (uint16_t)iy, c);
        LCD_Set_Pixel((uint16_t)(ix + 1), (uint16_t)iy, c);
        LCD_Set_Pixel((uint16_t)ix, (uint16_t)(iy - 1), c);
        LCD_Set_Pixel((uint16_t)ix, (uint16_t)(iy + 1), c);
    }

    draw_weapon_model(engine);

    char hud[40];
    (void)sprintf(hud, "HP:%3u K:%u", engine->health, engine->score);
    LCD_printString(hud, 5, 5, 0, 2);

    if (engine->reload_active) {
        (void)sprintf(hud, "%s RLD", get_weapon_label(engine->current_weapon));
    } else {
        (void)sprintf(hud, "%s %u/%u", get_weapon_label(engine->current_weapon), engine->ammo_in_mag, engine->ammo_reserve);
    }
    LCD_printString(hud, 5, 22, 0, 1);

    (void)sprintf(hud, "FPS:%u", current_fps);
    LCD_printString(hud, 172, 6, 0, 2);

    (void)sprintf(hud, "OD:%u/%u", engine->final_weapon_kills, ODIN_VICTORY_KILLS);
    LCD_printString(hud, 5, 36, 0, 1);
}

// stop sound
static void ZombieEngine_StopAudio(ZombieEngine_t* engine)
{
    buzzer_off(&buzzer_cfg);
    engine->audio_busy = 0u;
    engine->audio_step_idx = 0u;
    engine->audio_step_count = 0u;
    engine->audio_step_end_tick = 0u;
}

static void ShootButtons_Init(void)
{
    for (uint32_t i = 0; i < (sizeof(shoot_buttons) / sizeof(shoot_buttons[0])); i++) {
        (void)HAL_GPIO_ReadPin(shoot_buttons[i].port, shoot_buttons[i].pin);
    }
}

static uint8_t ShootButtons_IsDown(void)
{
    for (uint32_t i = 0; i < (sizeof(shoot_buttons) / sizeof(shoot_buttons[0])); i++) {
        if (HAL_GPIO_ReadPin(shoot_buttons[i].port, shoot_buttons[i].pin) ==
            shoot_buttons[i].active_level) {
            return 1;
        }
    }
    return 0;
}

static void update_game(Joystick_t* move_input, Joystick_t* look_input, uint8_t shoot_pressed, uint8_t reload_pressed)
{
    ZombieEngine_Update(&zombie_engine, move_input, look_input, shoot_pressed, reload_pressed);
    if (zombie_engine.game_over) {
        if (zombie_engine.victory) {
            printf("Victory! Final Kills: %d, Odin Kills: %d\n",
                   zombie_engine.score,
                   zombie_engine.final_weapon_kills);
        } else {
            printf("Game Over! Final Kills: %d\n", zombie_engine.score);
        }
        game_over_flag = 1;
    }
}

static void render_game(void)
{
    ZombieEngine_Draw(&zombie_engine);
    LCD_Refresh(&cfg0);
}

static void draw_intro_zombie(int x, int y, uint8_t step)
{
    int sway = step ? 2 : -2;

    LCD_Draw_Rect((uint16_t)(x + 9), (uint16_t)y, 18, 16, 3, 1);
    LCD_Draw_Rect((uint16_t)(x + 8), (uint16_t)(y + 3), 20, 10, 0, 0);
    LCD_Set_Pixel((uint16_t)(x + 14), (uint16_t)(y + 6), 2);
    LCD_Set_Pixel((uint16_t)(x + 22), (uint16_t)(y + 6), 2);
    LCD_Draw_Line((uint16_t)(x + 16), (uint16_t)(y + 11), (uint16_t)(x + 21), (uint16_t)(y + 11), 0);

    LCD_Draw_Rect((uint16_t)(x + 7), (uint16_t)(y + 16), 24, 25, 13, 1);
    LCD_Draw_Rect((uint16_t)(x + 17), (uint16_t)(y + 18), 4, 20, 2, 1);
    LCD_Draw_Line((uint16_t)(x + 11), (uint16_t)(y + 17), (uint16_t)(x + 16), (uint16_t)(y + 20), 1);
    LCD_Draw_Line((uint16_t)(x + 26), (uint16_t)(y + 17), (uint16_t)(x + 21), (uint16_t)(y + 20), 1);

    LCD_Draw_Line((uint16_t)(x + 7), (uint16_t)(y + 22), (uint16_t)(x + 1 + sway), (uint16_t)(y + 34), 3);
    LCD_Draw_Line((uint16_t)(x + 31), (uint16_t)(y + 22), (uint16_t)(x + 37 + sway), (uint16_t)(y + 34), 3);

    LCD_Draw_Rect((uint16_t)(x + 11 + sway), (uint16_t)(y + 41), 6, 13, 13, 1);
    LCD_Draw_Rect((uint16_t)(x + 23 - sway), (uint16_t)(y + 41), 6, 13, 13, 1);
    LCD_Draw_Rect((uint16_t)(x + 8 + sway), (uint16_t)(y + 54), 10, 4, 0, 1);
    LCD_Draw_Rect((uint16_t)(x + 22 - sway), (uint16_t)(y + 54), 10, 4, 0, 1);
}

static void draw_intro_animation(void)
{
    for (uint8_t frame = 0; frame < 18u; frame++) {
        Input_Read();
        if (current_input.btn2_pressed || current_input.btn3_pressed) {
            break;
        }

        LCD_Fill_Buffer(9);
        LCD_Draw_Rect(0, 152, SCREEN_WIDTH, 88, 12, 1);
        for (int gx = 0; gx < SCREEN_WIDTH; gx += 16) {
            LCD_Draw_Line((uint16_t)gx, 152, (uint16_t)gx, 239, 8);
        }
        for (int gy = 168; gy < SCREEN_HEIGHT; gy += 16) {
            LCD_Draw_Line(0, (uint16_t)gy, SCREEN_WIDTH - 1, (uint16_t)gy, 8);
        }

        LCD_printString("ZOMBIE BLOCK", 36, 18, 1, 2);
        LCD_printString("Survive the horde", 34, 46, 1, 1);
        LCD_printString("Move Left Stick", 34, 66, 1, 1);
        LCD_printString("Aim Right Stick", 34, 82, 1, 1);
        LCD_printString("Shoot BT2 Reload BT3", 34, 98, 1, 1);

        int zombie_x = 8 + (int)frame * 6;
        draw_intro_zombie(zombie_x, 112, frame & 1u);

        LCD_Draw_Rect(22, 207, 196, 7, 0, 0);
        LCD_Draw_Rect(24, 209, (uint16_t)(frame * 10), 3, 3, 1);
        LCD_printString("Press BT2/BT3 to skip", 34, 222, 1, 1);

        LCD_Refresh(&cfg0);
        HAL_Delay(150);
    }
}

MenuState Game3_Run(void)
{
    game_over_flag = 0;
    muzzle_led_off_tick = 0;

    // Initialize joysticks for movement and view/look input
    Joystick_Init(&joystick_move_cfg);
    Joystick_Init(&joystick_look_cfg);
    Joystick_Calibrate(&joystick_move_cfg);
    Joystick_Calibrate(&joystick_look_cfg);

    draw_intro_animation();

    ZombieEngine_Init(&zombie_engine);

    PWM_SetDuty(&pwm_cfg, 0);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    uint32_t last_tick = HAL_GetTick();
    uint32_t fps_tick = last_tick;
    uint16_t frames_this_second = 0;
    current_fps = 0;
    ShootButtons_Init();
    GPIO_PinState prev_reload_state = HAL_GPIO_ReadPin(reload_button.port, reload_button.pin);

    while (!game_over_flag) {
        Input_Read();

        uint32_t now = HAL_GetTick();
        last_tick = now;

        Joystick_Read(&joystick_move_cfg, &joystick_move_data);
        Joystick_Read(&joystick_look_cfg, &joystick_look_data);

        uint8_t shoot_pressed = ShootButtons_IsDown();
        uint8_t reload_pressed = 0;
        GPIO_PinState reload_state = HAL_GPIO_ReadPin(reload_button.port, reload_button.pin);
        if (prev_reload_state == GPIO_PIN_SET && reload_state == GPIO_PIN_RESET) {
            reload_pressed = 1;
        }
        prev_reload_state = reload_state;

        update_game(&joystick_move_data, &joystick_look_data, shoot_pressed, reload_pressed);

        if (zombie_engine.shot_fired_this_frame) {
            uint8_t flash_duty = 82u;
            uint16_t flash_ms = 42u;

            if (zombie_engine.current_weapon == ZOMBIE_WEAPON_VANDAL) {
                flash_duty = 88u;
                flash_ms = 24u;
            } else if (zombie_engine.current_weapon == ZOMBIE_WEAPON_ODIN) {
                flash_duty = 100u;
                flash_ms = 52u;
            }

            PWM_SetDuty(&pwm_cfg, flash_duty);
            muzzle_led_off_tick = now + (uint32_t)flash_ms;
        } else if (muzzle_led_off_tick != 0u && now >= muzzle_led_off_tick) {
            PWM_SetDuty(&pwm_cfg, 0);
            muzzle_led_off_tick = 0u;
        }

        render_game();
        frames_this_second++;
        if ((now - fps_tick) >= 1000u) {
            current_fps = frames_this_second;
            frames_this_second = 0;
            fps_tick = now;
        }
    }

    ZombieEngine_StopAudio(&zombie_engine);
    uint8_t victory = zombie_engine.victory;

    if (victory) {
        while (1) {
            Input_Read();
            if (current_input.btn3_pressed) {
                break;
            }

            LCD_Fill_Buffer(0);
            LCD_printString("VICTORY!", 42, 24, 1, 3);
            LCD_printString("ODIN CHALLENGE CLEAR", 12, 54, 1, 1);

            char info[40];
            sprintf(info, "Final Kills: %u", zombie_engine.score);
            LCD_printString(info, 20, 84, 1, 2);
            sprintf(info, "Odin Kills: %u", zombie_engine.final_weapon_kills);
            LCD_printString(info, 20, 108, 1, 2);

            LCD_printString("You Escaped The Block", 20, 146, 1, 1);
            LCD_Refresh(&cfg0);
            HAL_Delay(260);
        }
    } else {
        int16_t line_offset = 0;
        while (1) {
            Input_Read();
            if (current_input.btn3_pressed) {
                break;
            }

            LCD_Fill_Buffer(0);
            LCD_printString("Game Over!", 20 , 0 + line_offset, 1, 3);
            char score_str[32];
            sprintf(score_str, "Kills: %d", zombie_engine.score);
            LCD_printString(score_str, 20 , 20 + line_offset, 1, 2);
            LCD_Refresh(&cfg0);
            HAL_Delay(500);
            line_offset += 10;
            if (line_offset > 220) {
                line_offset = 0;
            }
        }
    }

    PWM_SetDuty(&pwm_cfg, 0);
    return MENU_STATE_HOME;
}
