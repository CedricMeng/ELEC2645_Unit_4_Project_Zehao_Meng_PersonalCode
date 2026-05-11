#include "main.h"
#include "Menu.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include <stdio.h>
#include <math.h>

extern ST7789V2_cfg_t cfg0;
extern InputState current_input;
extern ADC_HandleTypeDef hadc1;
extern RNG_HandleTypeDef hrng;
extern PWM_cfg_t pwm_cfg;
extern PWM_cfg_t p1_led_cfg;
extern Buzzer_cfg_t buzzer_cfg;

// Local helpers
static void menuLoop(void);
static void battleLoop(void);
static void pickHeroes(void);
static void pickMap(void);
static void minigameStart(void);
static void minigameSelect(void);
static void hockeyMatch(void);
static void endScreen(void);
static void runRealBattle(void);
static void DrawHeart(uint16_t x, uint16_t y, uint16_t size, uint8_t colour);
static void DrawLightning(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t colour);
static void DrawWarriorBattle(int16_t cx, int16_t cy, uint8_t frame, uint8_t moving);
static void DrawAssassinBattle(int16_t cx, int16_t cy, uint8_t frame, uint8_t moving);
static void DrawMageBattle(int16_t cx, int16_t cy, uint8_t frame, uint8_t moving);
static void updateSkillLights(void);
static void triggerSkill(uint8_t player);
static void DrawWarriorAura(int16_t x, int16_t y, int16_t w, int16_t h);
static void DrawDart(int16_t x, int16_t y, float angle);
static void playTone(uint32_t freq, uint16_t duration_ms);
static void buzzerTick(void);

// Buzzer
#define JOYSTICK_BEEP_FREQ 600u    // 600Hz joystick tone
#define ATTACK_BEEP_FREQ 800u      // 800Hz attack tone
#define HURT_BEEP_FREQ 320u        // 320Hz hurt tone
#define SKILL_PICKUP_FREQ 1000u    // 1kHz skill pickup tone
#define HEALTH_PICKUP_FREQ 1200u   // 1.2kHz health pickup tone
#define SKILL_ACTIVATE_FREQ 1500u  // 1.5kHz skill activation tone
#define VICTORY_FREQ 800u          // Victory music base frequency: 800Hz
#define WARNING_FREQ 2000u         // 2000Hz warning tone
#define BEEP_VOLUME 45u            // Quick UI beep volume
#define BEEP_MS 25u                // Short beep duration
#define HURT_BEEP_MS 40u           // Hurt tone length
#define PICKUP_BEEP_MS 30u         // Pickup tone length
#define SKILL_BEEP_MS 50u          // skill beep length
#define WARNING_BEEP_MS 500u       // Warning tone length
#define DEATH_NOTE_MS 150u         // death-note length
#define EXIT_BEEP_MS 100u          // Exit tone length

// Hero info
typedef struct {
    uint8_t max_hp;
    uint8_t damage;
    uint8_t move_speed;
    uint8_t attack_reach;
    uint8_t effect_color;
} HeroInfo;

static const HeroInfo heroInfo[3] = {
    {180, 6, 2, 30, 10},   // Warrior 
    { 85, 11, 4, 28,  1},   // Assassin 
    {100, 8, 2, 50,  4}    // Mage 
};

// Skill stuff
typedef struct {
    int16_t x, y;
    int16_t vx, vy;
    uint8_t active;
} Dart;  // Assassin skill darts

#define MAX_DARTS 9

static void doDartUpdate(Dart darts[], uint8_t max_darts);
static void launchDart(Dart darts[], uint8_t max_darts, int16_t start_x, int16_t start_y, float angle, int16_t speed);

// Collision boxes
typedef struct {
    int16_t x, y;
    int16_t w, h;
} Hitbox;

static Hitbox GetHeroBody(int16_t x, int16_t y, int8_t hero) {
    if (hero == 0) return (Hitbox){x + 8, y + 8, 32, 32};
    if (hero == 1) return (Hitbox){x + 6, y + 6, 28, 28};
    return (Hitbox){x + 7, y + 6, 26, 28};
}

static int16_t GetHeroWidth(int8_t hero) {
    return (hero == 0) ? 48 : 40;
}

static int16_t GetHeroHeight(int8_t hero) {
    return (hero == 0) ? 48 : 40;
}

static float NormalizeAngle(float angle) {
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

static uint8_t PointInAABB(int16_t px, int16_t py, Hitbox box) {
    return px >= box.x && px < box.x + box.w && py >= box.y && py < box.y + box.h;
}

static uint8_t LineSegmentsIntersect(int x1, int y1, int x2, int y2,
                                     int x3, int y3, int x4, int y4) {
    int d1 = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1);
    int d2 = (x2 - x1) * (y4 - y1) - (y2 - y1) * (x4 - x1);
    int d3 = (x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3);
    int d4 = (x4 - x3) * (y2 - y3) - (y4 - y3) * (x2 - x3);
    return ((d1 ^ d2) < 0) && ((d3 ^ d4) < 0);
}

static uint8_t LineIntersectsAABB(int x1, int y1, int x2, int y2, Hitbox box) {
    if (PointInAABB(x1, y1, box) || PointInAABB(x2, y2, box)) return 1;
    if (LineSegmentsIntersect(x1, y1, x2, y2, box.x, box.y, box.x + box.w, box.y)) return 1;
    if (LineSegmentsIntersect(x1, y1, x2, y2, box.x + box.w, box.y, box.x + box.w, box.y + box.h)) return 1;
    if (LineSegmentsIntersect(x1, y1, x2, y2, box.x + box.w, box.y + box.h, box.x, box.y + box.h)) return 1;
    if (LineSegmentsIntersect(x1, y1, x2, y2, box.x, box.y + box.h, box.x, box.y)) return 1;
    return 0;
}

static uint8_t AABBOverlap(Hitbox b1, Hitbox b2) {
    return !(b1.x + b1.w <= b2.x || b2.x + b2.w <= b1.x || b1.y + b1.h <= b2.y || b2.y + b2.h <= b1.y);
}

static uint8_t WarriorAttackHits(int16_t cx, int16_t cy, float angle, int16_t reach, Hitbox target) {
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    float fx = sinf(rad);
    float fy = -cosf(rad);
    float px = -fy;
    float py = fx;
    float max_forward = reach;
    float half_width = 22.0f;

    int16_t sample_x[7] = {target.x, target.x + target.w, target.x, target.x + target.w, target.x + target.w / 2, target.x + 4, target.x + target.w - 4};
    int16_t sample_y[7] = {target.y, target.y, target.y + target.h, target.y + target.h, target.y + target.h / 2, target.y + target.h / 2, target.y + target.h / 2};

    for (int i = 0; i < 7; i++) {
        float rx = (float)sample_x[i] - cx;
        float ry = (float)sample_y[i] - cy;
        float forward = rx * fx + ry * fy;
        float side = fabsf(rx * px + ry * py);
        if (forward >= -8.0f && forward <= max_forward && side <= half_width) {
            return 1;
        }
    }
    return 0;
}

static uint8_t SlashAttackHits(int16_t cx, int16_t cy, float angle, int16_t reach, Hitbox target) {
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    float fx = sinf(rad);
    float fy = -cosf(rad);
    float px = -fy;
    float py = fx;
    float max_forward = reach + 8.0f;
    float half_width = 8.0f;

    float sample_x[9] = {
        (float)target.x,
        (float)(target.x + target.w),
        (float)target.x,
        (float)(target.x + target.w),
        (float)(target.x + target.w / 2),
        (float)(target.x + target.w / 2),
        (float)(target.x + target.w / 3),
        (float)(target.x + target.w * 2 / 3),
        (float)(target.x + target.w / 2)
    };
    float sample_y[9] = {
        (float)target.y,
        (float)target.y,
        (float)(target.y + target.h),
        (float)(target.y + target.h),
        (float)target.y,
        (float)(target.y + target.h),
        (float)(target.y + target.h / 2),
        (float)(target.y + target.h / 2),
        (float)(target.y + target.h / 2)
    };
    for (int i = 0; i < 9; i++) {
        float rx = sample_x[i] - cx;
        float ry = sample_y[i] - cy;
        float forward = rx * fx + ry * fy;
        if (forward < 0.0f || forward > max_forward) continue;
        float side = fabsf(rx * px + ry * py);
        if (side <= half_width) {
            return 1;
        }
    }
    return 0;
}

static uint8_t LaserAttackHits(int16_t cx, int16_t cy, float angle, int16_t reach, int16_t width, Hitbox target) {
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    int16_t extended_reach = reach + 5;
    float dir_x = sinf(rad);
    float dir_y = -cosf(rad);
    float perp_x = -dir_y;
    float perp_y = dir_x;
    int16_t half_width = width / 2;
    int16_t step = (half_width > 0) ? half_width : 1;

    for (int16_t offset = -half_width; offset <= half_width; offset += step) {
        int16_t ox = (int16_t)(perp_x * offset);
        int16_t oy = (int16_t)(perp_y * offset);
        int16_t sx = cx + ox;
        int16_t sy = cy + oy;
        int16_t ex = sx + (int16_t)(dir_x * extended_reach);
        int16_t ey = sy + (int16_t)(dir_y * extended_reach);
        if (LineIntersectsAABB(sx, sy, ex, ey, target)) {
            return 1;
        }
    }
    return 0;
}

static uint8_t AttackHitsBody(int16_t src_x, int16_t src_y, int8_t hero, float attack_angle, int16_t reach, Hitbox target, uint8_t enhanced) {
    int16_t cx = src_x + GetHeroWidth(hero) / 2;
    int16_t cy = src_y + GetHeroHeight(hero) / 2;
    float angle = (attack_angle < 0.0f) ? 90.0f : NormalizeAngle(attack_angle);
    if (hero == 0) {
        return WarriorAttackHits(cx, cy, angle, reach, target);
    }
    if (hero == 2) {
        int16_t actual_reach = enhanced ? reach * 2 : reach;
        int16_t width = enhanced ? 6 : 4;
        return LaserAttackHits(cx, cy, angle, actual_reach, width, target);
    }
    return SlashAttackHits(cx, cy, angle, reach, target);
}

static void DrawWarriorEffect(int16_t cx, int16_t cy, float angle, uint8_t color, uint8_t reach) {
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    float dir_x = sinf(rad);
    float dir_y = -cosf(rad);
    float perp_x = -dir_y;
    float perp_y = dir_x;
    int16_t length = reach;
    float half_width = 10.0f;

    // fill the rectangle line by line
    for (float f = -half_width; f <= half_width + 0.1f; f += 0.5f) {
        int16_t ox = (int16_t)(perp_x * f);
        int16_t oy = (int16_t)(perp_y * f);
        LCD_Draw_Line(cx + ox, cy + oy, cx + ox + (int16_t)(dir_x * length), cy + oy + (int16_t)(dir_y * length), color);
    }
}

static void DrawSlashEffect(int16_t cx, int16_t cy, float angle, uint8_t color, uint8_t reach) {
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    float dir_x = sinf(rad);
    float dir_y = -cosf(rad);
    float perp_x = -dir_y;
    float perp_y = dir_x;
    int16_t length = reach;
    float half_width = 4.0f;

    for (float f = -half_width; f <= half_width + 0.1f; f += 0.5f) {
        int16_t ox = (int16_t)(perp_x * f);
        int16_t oy = (int16_t)(perp_y * f);
        LCD_Draw_Line(cx + ox, cy + oy, cx + ox + (int16_t)(dir_x * length), cy + oy + (int16_t)(dir_y * length), color);
    }
}

static void DrawLaserEffect(int16_t cx, int16_t cy, float angle, uint8_t color, uint8_t reach, int16_t width) {
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    float dir_x = sinf(rad);
    float dir_y = -cosf(rad);
    float perp_x = -dir_y;
    float perp_y = dir_x;
    int16_t length = reach + 5;
    float half_width = width / 2.0f;

    for (float f = -half_width; f <= half_width + 0.1f; f += 0.5f) {
        int16_t ox = (int16_t)(perp_x * f);
        int16_t oy = (int16_t)(perp_y * f);
        LCD_Draw_Line(cx + ox, cy + oy, cx + ox + (int16_t)(dir_x * length), cy + oy + (int16_t)(dir_y * length), color);
    }

    int16_t tip_x = cx + (int16_t)(dir_x * length);
    int16_t tip_y = cy + (int16_t)(dir_y * length);
    LCD_Draw_Circle(tip_x, tip_y, 3, color, 1);
}

static void DrawAttackEffect(int16_t x, int16_t y, int8_t hero, float angle, uint8_t enhanced) {
    uint8_t color = heroInfo[hero].effect_color;
    uint8_t reach = heroInfo[hero].attack_reach;
    float draw_angle = (angle < 0.0f) ? 90.0f : angle;
    int16_t cx = x + GetHeroWidth(hero) / 2;
    int16_t cy = y + GetHeroHeight(hero) / 2;

    if (hero == 0) {
        DrawWarriorEffect(cx, cy, draw_angle, color, reach);
    } else if (hero == 2) {
        if (enhanced) {
            color = 2;  // Enhanced mage skill uses red
        }
        int16_t actual_reach = enhanced ? reach * 2 : reach;
        int16_t width = enhanced ? 6 : 4;  // Thicker during skill
        DrawLaserEffect(cx, cy, draw_angle, color, actual_reach, width);
    } else {
        DrawSlashEffect(cx, cy, draw_angle, color, reach);
    }
}

// Hero Icon Pixel Sprites
static const uint8_t HeroShieldSprite[10][10] = {
    {255,255,0,0,0,0,0,0,255,255},
    {255,0,10,10,10,10,10,10,0,255},
    {0,10,10,10,10,10,10,10,10,0},
    {0,10,10,10,10,10,10,10,10,0},
    {0,10,10,10,10,10,10,10,10,0},
    {0,10,10,10,10,10,10,10,10,0},
    {255,0,10,10,10,10,10,10,0,255},
    {255,255,0,10,10,10,10,0,255,255},
    {255,255,255,0,10,10,0,255,255,255},
    {255,255,255,255,0,0,255,255,255,255}
};

static const uint8_t PixelCrownSprite[6][10] = {
    {1,0,0,0,1,1,0,0,0,1},
    {1,1,0,0,1,1,0,0,1,1},
    {1,1,1,0,1,1,0,1,1,1},
    {1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1},
};

static const uint8_t HeroSwordSprite[10][10] = {
    {255,255,255,1,255,255,255,255,255,255},
    {255,255,255,1,1,255,255,255,255,255},
    {255,255,255,1,1,255,255,255,255,255},
    {255,255,255,1,1,1,255,255,255,255},
    {255,255,255,1,1,1,255,255,255,255},
    {255,255,255,0,0,1,255,255,255,255},
    {255,255,255,0,0,0,255,255,255,255},
    {255,0,0,0,0,0,0,0,255,255},
    {255,255,0,12,12,12,0,255,255,255},
    {255,255,255,0,12,0,255,255,255,255}
};

static const uint8_t HeroMagicHatSprite[10][10] = {
    {255,255,255,0,0,0,0,255,255,255},
    {255,255,0,0,0,0,0,0,255,255},
    {255,0,0,0,0,0,0,0,0,255},
    {0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0},
    {255,0,0,0,0,0,0,0,0,255},
    {255,255,0,0,0,0,0,0,255,255},
    {255,255,255,0,8,8,0,255,255,255},
    {255,255,255,0,8,8,0,255,255,255},
    {255,255,255,255,0,0,255,255,255,255}
};

static const uint8_t HeroWarriorBaseSprite[8][8] = {
    {255,255,12,12,12,255,255,255},
    {255,12,1,1,1,12,255,255},
    {255,12,12,12,12,12,255,255},
    {255,12,10,2,10,12,255,255},
    {255,12,12,12,12,12,255,255},
    {255,255,12,12,12,13,255,255},
    {255,255,12,12,12,13,255,255},
    {255,255,255,13,13,255,255,255}
};

static const uint8_t HeroAssassinBaseSprite[8][8] = {
    {255,255,2,0,2,255,255,255},
    {255,255,0,1,0,255,255,255},
    {255,0,0,0,0,0,255,255},
    {255,0,0,0,0,0,255,255},
    {255,255,0,0,0,255,255,255},
    {255,255,0,255,0,13,255,255},
    {255,255,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,255}
};

static const uint8_t HeroAssassinWalkSprite1[8][8] = {
    {255,255,2,0,2,255,255,255},
    {255,255,0,1,0,255,255,255},
    {255,0,0,0,0,0,255,255},
    {255,0,0,0,0,0,255,255},
    {255,255,0,0,0,255,255,255},
    {255,255,255,0,255,13,255,255},
    {255,0,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,255}
};

static const uint8_t HeroAssassinWalkSprite2[8][8] = {
    {255,255,2,0,2,255,255,255},
    {255,255,0,1,0,255,255,255},
    {255,0,0,0,0,0,255,255},
    {255,0,0,0,0,0,255,255},
    {255,255,0,0,0,255,255,255},
    {255,255,0,255,255,255,255,13},
    {255,255,255,255,255,0,255,255},
    {255,255,255,255,255,255,255,255}
};

static const uint8_t HeroMageBaseSprite[8][8] = {
    {255,255,255,255,255,255,255,255},
    {255,255,255,1,1,255,255,255},
    {255,255,4,4,4,4,255,255},
    {255,255,4,14,4,4,255,255},
    {255,255,255,4,4,255,255,255},
    {255,255,255,4,4,255,255,255},
    {255,255,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,255}
};

// Battle Global Variables
static int8_t p1_hero = 0;
static int8_t p2_hero = 0;
static int16_t p1_x = 50;
static int16_t p1_y = 140;
static int16_t p2_x = 170;
static int16_t p2_y = 140;
static uint8_t p1_health = 100;
static uint8_t p2_health = 100;
static uint8_t p1_charge = 0;
static uint8_t p2_charge = 0;
static float p1_facing_angle = 90.0f;
static float p2_facing_angle = 270.0f;

// Confirmation Status
static uint8_t p1_confirmed = 0;
static uint8_t p2_confirmed = 0;

// Battle Initialization Flags
static uint8_t needResetNow = 0;
static uint8_t realBattleNeedsReset = 1;  // Yeah, this one says we should reinit the real battle next time

// State Reset Flags
static uint8_t reset_fighting_state = 1;
static uint8_t reset_heroselect_state = 1;

// Attack Cooldown
static uint32_t p1_last_swing = 0;
static uint32_t p2_last_swing = 0;
#define ATTACK_COOLDOWN 600
#define SKILL_MAX 20
#define MAX_ITEMS 5
#define MAX_CACTUS 12
#define MAX_PUDDLES 10

typedef struct {
    int16_t x, y;
    uint8_t type; // 0 health, 1 skill
    uint8_t active;
} Item;

typedef struct {
    int16_t x, y;
    int16_t vx, vy;  // Velocity
    uint8_t bounce_count;  // Bounce count (0-2)
    uint8_t active;
} Cactus;

typedef struct {
    int16_t x, y;
    uint32_t expire_time;
    uint8_t active;
} WaterPuddle;

static Item items[MAX_ITEMS];
static Cactus cactus[MAX_CACTUS];
static WaterPuddle puddles[MAX_PUDDLES];
static Dart p1_darts[MAX_DARTS];
static Dart p2_darts[MAX_DARTS];
static uint32_t battle_start_time = 0;
static uint32_t p1_freeze_end = 0;
static uint32_t p2_freeze_end = 0;
static uint32_t p1_led_flash_end = 0;
static uint32_t p2_led_flash_end = 0;
static uint8_t battle_live = 0;

// skill timers
static uint32_t p1_effect_end = 0;      // Warrior shield or assassin darts
static uint32_t p2_effect_end = 0;
static uint32_t p1_beam_end = 0;  // Mage laser enhancement duration
static uint32_t p2_beam_end = 0;
static uint32_t p1_shield_end = 0;  // Warrior damage reduction duration
static uint32_t p2_shield_end = 0;

static void updateSkillLights(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t p1_full_juice = (p1_charge == SKILL_MAX);
    uint8_t p2_full_juice = (p2_charge == SKILL_MAX);
    uint8_t p1_beat_up = (now < p1_led_flash_end);
    uint8_t p2_beat_up = (now < p2_led_flash_end);

    uint8_t p1_led_amt = 0;
    uint8_t p2_led_amt = 0;

    if (battle_live) {
        // dim LEDs unless someone took damage or is ready to use a skill
        if (p1_full_juice) {
            p1_led_amt = 100;
        } else if (p1_beat_up) {
            p1_led_amt = 50;
        }

        if (p2_full_juice) {
            p2_led_amt = 100;
        } else if (p2_beat_up) {
            p2_led_amt = 50;
        }
    }

    PWM_SetDuty(&p1_led_cfg, p1_led_amt);
    PWM_SetDuty(&pwm_cfg, p2_led_amt);
}

// skill button handling
static void triggerSkill(uint8_t player)
{
    uint32_t now = HAL_GetTick();
    if (player == 0 && p1_charge == SKILL_MAX) {
        p1_charge = 0;  // clear the skill bar
        playTone(SKILL_ACTIVATE_FREQ, SKILL_BEEP_MS);
        if (p1_hero == 0) {  // Warrior
            p1_shield_end = now + 10000;  // 10 seconds damage reduction
        } else if (p1_hero == 1) {  // Assassin
            p1_effect_end = now + 7000;  // 7 seconds dart time
            // Fire nine darts
            float angle = p1_facing_angle;
            for (int i = 0; i < 9; i++) {
                launchDart(p1_darts, MAX_DARTS, p1_x + GetHeroWidth(p1_hero)/2, p1_y + GetHeroHeight(p1_hero)/2, angle, 8);
                angle += 40.0f;  // 360/9 = 40 degree intervals
            }
        } else {  // Mage
            p1_beam_end = now + 10000;  // 10 seconds enhanced laser
        }
    } else if (player == 1 && p2_charge == SKILL_MAX) {
        p2_charge = 0;
        playTone(SKILL_ACTIVATE_FREQ, SKILL_BEEP_MS);
        if (p2_hero == 0) {  // Warrior
            p2_shield_end = now + 10000;  // 10 seconds damage reduction
        } else if (p2_hero == 1) {  // Assassin
            p2_effect_end = now + 7000;
            // Fire nine darts
            float angle = p2_facing_angle;
            for (int i = 0; i < 9; i++) {
                launchDart(p2_darts, MAX_DARTS, p2_x + GetHeroWidth(p2_hero)/2, p2_y + GetHeroHeight(p2_hero)/2, angle, 8);
                angle += 40.0f;
            }
        } else {  // Mage
            p2_beam_end = now + 10000;  // 10 seconds enhanced laser
        }
    }
}

// Draw warrior aura
static void DrawWarriorAura(int16_t x, int16_t y, int16_t w, int16_t h)
{
    // Adjust circle center position to make aura more centered
    int16_t cx = x + w / 2 - 6;  // Shift left more
    int16_t cy = y + h / 2 - 6;  // Shift up more
    int16_t radius = (w > h ? w : h) / 2;  // Circle just covers the warrior
    // Double the width: draw two concentric circles
    LCD_Draw_Circle(cx, cy, radius, 6, 0);      // Yellow hollow circle
    LCD_Draw_Circle(cx, cy, radius + 1, 6, 0);  // Double width
}

// Draw dart (using assassin knife graphic)
static void DrawDart(int16_t x, int16_t y, float angle)
{
    // Rotate knife graphic
    float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);
    
    // Knife pixels (further enlarged)
    int16_t points[32][2] = {
        {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0},
        {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1},
        {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2},
        {3, 3}, {4, 3}, {5, 3}, {6, 3}, {7, 3}
    };
    
    for (int i = 0; i < 32; i++) {
        int16_t px = (int16_t)(points[i][0] * cos_a - points[i][1] * sin_a);
        int16_t py = (int16_t)(points[i][0] * sin_a + points[i][1] * cos_a);
        LCD_Set_Pixel(x + px, y + py, 1);  // White knife
    }
}

// Keep track of whether that little speaker is currently buzzing
static uint8_t buzzer_active = 0;        // Is the buzzer on right now?
static uint32_t buzzer_end_time = 0;     // When to shut it off

// Some helper bits for the buzzer
/* Make the buzzer chirp once */
static void playTone(uint32_t freq, uint16_t duration_ms)
{
    buzzer_tone(&buzzer_cfg, freq, BEEP_VOLUME);
    buzzer_active = 1;
    buzzer_end_time = HAL_GetTick() + duration_ms;
}

/* Check if the beep is finished and shut it up */
static void buzzerTick(void)
{
    if (buzzer_active && HAL_GetTick() >= buzzer_end_time) {
        buzzer_off(&buzzer_cfg);
        buzzer_active = 0;
    }
}

// Update dart positions
static void doDartUpdate(Dart darts[], uint8_t max_darts)
{
    for (int i = 0; i < max_darts; i++) {
        if (darts[i].active) {
            darts[i].x += darts[i].vx;
            darts[i].y += darts[i].vy;
            
            // Boundary check
            if (darts[i].x < 0 || darts[i].x > 240 || darts[i].y < 0 || darts[i].y > 240) {
                darts[i].active = 0;
            }
        }
    }
}

// Fire dart
static void launchDart(Dart darts[], uint8_t max_darts, int16_t start_x, int16_t start_y, float angle, int16_t speed)
{
    for (int i = 0; i < max_darts; i++) {
        if (!darts[i].active) {
            darts[i].x = start_x;
            darts[i].y = start_y;
            float rad = NormalizeAngle(angle) * 3.14159f / 180.0f;
            darts[i].vx = (int16_t)(cosf(rad) * speed);
            darts[i].vy = (int16_t)(sinf(rad) * speed);
            darts[i].active = 1;
            break;
        }
    }
}

// Hearts & Lightning
static void DrawHeart(uint16_t x, uint16_t y, uint16_t size, uint8_t colour)
{
    int16_t r = size / 3 + 4;
    int16_t lx = x - size / 3.8f;
    int16_t rx = x + size / 3.8f;

    LCD_Draw_Circle(lx, y - size / 5.5f, r, colour, 1);
    LCD_Draw_Circle(rx, y - size / 5.5f, r, colour, 1);

    for (int16_t i = 0; i <= size / 2 + 6; i++) {
        int16_t width = size - i * 1.65f;
        LCD_Draw_Line(x - width / 2, y + size / 4 + i,
                      x + width / 2, y + size / 4 + i, colour);
    }
}

static void DrawLightning(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t colour)
{
    for (int8_t offset = -4; offset <= 4; offset++) {
        LCD_Draw_Line(x1 + offset, y1, x1 + 12 + offset, y1 - 16, colour);
        LCD_Draw_Line(x1 + 12 + offset, y1 - 16, x1 + 22 + offset, y1 + 11, colour);
        LCD_Draw_Line(x1 + 22 + offset, y1 + 11, x1 + 32 + offset, y1 - 13, colour);
        LCD_Draw_Line(x1 + 32 + offset, y1 - 13, x2 + offset, y2, colour);
    }
    LCD_Draw_Circle(x1 + 15, y1 - 11, 3, 5, 1);
    LCD_Draw_Circle(x1 + 25, y1 + 6, 2, 5, 1);
}

static void DrawWarriorBattle(int16_t x, int16_t y, uint8_t frame, uint8_t moving)
{
    const uint8_t scale = 6;
    LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroWarriorBaseSprite, scale);
    int16_t shield_x = x + 6;
    if (moving) {
        shield_x += (frame % 4 - 2); // oscillate shield x when moving
    }
    LCD_Draw_Sprite_Scaled(shield_x, y + 16, 10, 10, (uint8_t*)HeroShieldSprite, 2);
}

static void DrawAssassinBattle(int16_t x, int16_t y, uint8_t frame, uint8_t moving)
{
    const uint8_t scale = 5;
    if (moving) {
        if ((frame % 4) < 2) {
            LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroAssassinWalkSprite1, scale);
        } else {
            LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroAssassinWalkSprite2, scale);
        }
    } else {
        LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroAssassinBaseSprite, scale);
    }
}

static void DrawMageBattle(int16_t x, int16_t y, uint8_t frame, uint8_t moving)
{
    const uint8_t scale = 5;
    LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroMageBaseSprite, scale);
    int16_t wand_y = y + 6;
    if (moving) {
        wand_y += (frame % 4 - 2);
    }
    LCD_Draw_Line(x + 30, wand_y, x + 30, y + 32, 14);
    LCD_Draw_Circle(x + 30, wand_y, 3, 5, 1);
    LCD_Draw_Line(x + 22, y + 10, x + 28, y + 12, 4);
}

typedef enum {
    GAME_MENU,
    GAME_FIGHTING,
    GAME_HERO_SELECT,
    GAME_SCENE_SELECT,
    GAME_MINIGAME,
    GAME_MINIGAME_SELECT,
    GAME_HOCKEY,
    GAME_RESULT,
    GAME_REAL_BATTLE
} GameInnerState;

static GameInnerState innerState = GAME_MENU;
static GameInnerState last_global_state = 0xFF;  // Global state tracking for detecting returns across multiple states
static int8_t menuSelection = 0;
static int8_t selected_scene = 0;
static Direction last_direction = CENTRE;

// two joysticks
static Joystick_cfg_t js1 = {
    .adc = &hadc1, .x_channel = ADC_CHANNEL_1, .y_channel = ADC_CHANNEL_2,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5, .deadzone = JOYSTICK_DEADZONE
};

static Joystick_cfg_t js2 = {
    .adc = &hadc1, .x_channel = ADC_CHANNEL_5, .y_channel = ADC_CHANNEL_6,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5, .deadzone = JOYSTICK_DEADZONE
};

static Joystick_t d1, d2;
static int exitgame = 0;
static uint8_t animation_frame = 0;

// Game1_Run
MenuState Game1_Run(void)
{
    // Ensure buzzer is off when entering Game1
    buzzer_off(&buzzer_cfg);

    Joystick_Init(&js1);
    Joystick_Init(&js2);
    Joystick_Calibrate(&js1);
    Joystick_Calibrate(&js2);

    innerState = GAME_MENU;
    menuSelection = 0;
    last_direction = CENTRE;
    exitgame = 0;

    LCD_Fill_Buffer(0);
    LCD_Refresh(&cfg0);

    while (1)
    {
        Input_Read();
        Joystick_Read(&js1, &d1);
        Joystick_Read(&js2, &d2);

        switch (innerState)
        {
            case GAME_MENU:        menuLoop();             break;
            case GAME_FIGHTING:    battleLoop();          break;
            case GAME_HERO_SELECT: pickHeroes();        break;
            case GAME_SCENE_SELECT: pickMap();       break;
            case GAME_MINIGAME:    minigameStart();          break;
            case GAME_MINIGAME_SELECT: minigameSelect(); break;
            case GAME_HOCKEY:      hockeyMatch();        break;
            case GAME_RESULT:      endScreen();           break;
            case GAME_REAL_BATTLE: runRealBattle();        break;
        }
        
        // Update global state
        last_global_state = innerState;
        
        if (exitgame) break;

        HAL_Delay(16);
    }

    // Ensure buzzer is off when exiting Game1
    buzzer_off(&buzzer_cfg);

    return MENU_STATE_HOME;
}

static void menuLoop(void)
{
    // Set state reset flags so next entry to Fighting/HeroSelect can reset
    reset_fighting_state = 1;
    reset_heroselect_state = 1;
    realBattleNeedsReset = 1;  // Ensure next battle starts from 90s
    
    buzzerTick();

    LCD_Fill_Buffer(13);
    DrawHeart(65, 82, 46, 7);
    DrawHeart(175, 82, 46, 7);
    DrawLightning(105, 90, 135, 90, 5);

    LCD_printString("IT TAKES TWO", 18, 18, 2, 3);

    LCD_printString("1. FIGHTING",  52, 138, 1, 2);
    LCD_printString("2. MINIGAME",  52, 168, 1, 2);
    LCD_printString("3. EXIT",      52, 198, 1, 2);

    if (menuSelection == 0) { LCD_Draw_Rect(46, 130, 150, 28, 2, 0); LCD_printString(">>", 25, 138, 2, 2); }
    else if (menuSelection == 1) { LCD_Draw_Rect(46, 160, 150, 28, 2, 0); LCD_printString(">>", 25, 168, 2, 2); }
    else if (menuSelection == 2) { LCD_Draw_Rect(46, 190, 150, 28, 2, 0); LCD_printString(">>", 25, 198, 2, 2); }

    LCD_printString("P1Joystick select BT3 Confirm", 32, 224, 1, 1);
    LCD_Refresh(&cfg0);

    Direction current_direction = d1.direction;

    if (current_direction == N && last_direction != N) {
        menuSelection = (menuSelection + 2) % 3;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    else if (current_direction == S && last_direction != S) {
        menuSelection = (menuSelection + 1) % 3;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }

    last_direction = current_direction;

    if (current_input.btn3_pressed) {
        playTone(ATTACK_BEEP_FREQ, BEEP_MS);
        HAL_Delay(200);
        if (menuSelection == 0) innerState = GAME_FIGHTING;
        else if (menuSelection == 1) innerState = GAME_MINIGAME;
        else if (menuSelection == 2) exitgame = 1;
    }
}

static void battleLoop(void)
{
    static uint8_t p1_connected = 0;
    static uint8_t p2_connected = 0;
    static uint8_t button_debounce_p1 = 0;
    static uint8_t button_debounce_p2 = 0;

    // Check if state reset is needed
    if (reset_fighting_state) {
        p1_connected = 0;
        p2_connected = 0;
        button_debounce_p1 = 0;
        button_debounce_p2 = 0;
        reset_fighting_state = 0;
    }

    buzzerTick();

    // P1 connection check (using BTN3/joystick button)
    if (current_input.btn3_pressed && !button_debounce_p1) {
        p1_connected = 1;
        button_debounce_p1 = 1;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    if (!current_input.btn3_pressed) {
        button_debounce_p1 = 0;
    }

    // P2 connection check (using BTN2/joystick button)
    if (current_input.btn2_pressed && !button_debounce_p2) {
        p2_connected = 1;
        button_debounce_p2 = 1;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    if (!current_input.btn2_pressed) {
        button_debounce_p2 = 0;
    }

    // Display interface
    LCD_Fill_Buffer(13);
    LCD_printString("FIGHTING MODE", 8, 20, 2, 3);
    LCD_printString("Joystick Test", 35, 55, 1, 2);

    char buf1[30];
    sprintf(buf1, "P1 Dir:%d Mag:%.1f", d1.direction, d1.magnitude);
    LCD_printString(buf1, 15, 90, 1, 1);
    LCD_printString(p1_connected ? "P1 Button [OK]" : "P1 Button [Press]", 15, 105, 1, 2);

    char buf2[30];
    sprintf(buf2, "P2 Dir:%d Mag:%.1f", d2.direction, d2.magnitude);
    LCD_printString(buf2, 15, 125, 1, 1);
    LCD_printString(p2_connected ? "P2 Button [OK]" : "P2 Button [Press]", 15, 140, 1, 2);

    // Show start prompt when both players are ready
    if (p1_connected && p2_connected) {
        LCD_printString("Both Ready!", 45, 175, 2, 2);
        LCD_printString("Press P1 Button to Start", 15, 200, 1, 1);
    }

    LCD_Refresh(&cfg0);

    // Start game (both players ready, P1 presses joystick button)
    if (p1_connected && p2_connected && current_input.btn3_pressed) {
        playTone(ATTACK_BEEP_FREQ, BEEP_MS);
        HAL_Delay(300);
        reset_heroselect_state = 1;  // Set flag to reset when entering HeroSelect
        innerState = GAME_HERO_SELECT;
        // Reset connection status
        p1_connected = 0;
        p2_connected = 0;
        button_debounce_p1 = 0;
        button_debounce_p2 = 0;
    }
}

static void pickHeroes(void)
{
    static int8_t p1_choice = 0;
    static int8_t p2_choice = 0;
    static Direction last_p1 = CENTRE;
    static Direction last_p2 = CENTRE;

    // Check if state reset is needed
    if (reset_heroselect_state) {
        p1_choice = 0;
        p2_choice = 0;
        last_p1 = CENTRE;
        last_p2 = CENTRE;
        p1_confirmed = 0;
        p2_confirmed = 0;
        reset_heroselect_state = 0;
    }

    buzzerTick();

    LCD_Fill_Buffer(13);

    LCD_printString("HERO SELECT", 30, 15, 2, 3);

    for (int i = 0; i < 3; i++) {
        uint16_t x = 32 + i * 65;

        LCD_Draw_Rect(x, 55, 55, 55, 1, 0);

        if (i == p1_choice) LCD_Draw_Rect(x, 55, 55, 55, 2, 0);

        if (i == p2_choice) LCD_Draw_Rect(x + 2, 57, 51, 51, 4, 0);

        int16_t px = x + 3;
        int16_t py = 58;

        if (i == 0) {
            LCD_Draw_Sprite_Scaled(px, py, 10, 10, (uint8_t*)HeroShieldSprite, 5);
        }
        else if (i == 1) {
            LCD_Draw_Sprite_Scaled(px, py, 10, 10, (uint8_t*)HeroSwordSprite, 5);
        }
        else {
            LCD_Draw_Sprite_Scaled(px, py, 10, 10, (uint8_t*)HeroMagicHatSprite, 5);
        }
    }

    static const char* hero_names[3] = {"Ironfist", "Shadowblade", "Mystic"};
    static const uint16_t hero_attack[3] = {70, 110, 85};
    static const uint16_t hero_ability[3] = {40, 95, 60};
    static const uint16_t hero_life[3] = {180, 90, 110};
    static const char* hero_range[3] = {"Medium", "Short", "Long"};

    if (!p1_confirmed) {
        if (d1.direction == E && last_p1 != E) {
            p1_choice = (p1_choice + 1) % 3;
            playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        }
        if (d1.direction == W && last_p1 != W) {
            p1_choice = (p1_choice + 2) % 3;
            playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        }
    }
    if (!p2_confirmed) {
        if (d2.direction == E && last_p2 != E) {
            p2_choice = (p2_choice + 1) % 3;
            playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        }
        if (d2.direction == W && last_p2 != W) {
            p2_choice = (p2_choice + 2) % 3;
            playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        }
    }
    last_p1 = d1.direction;
    last_p2 = d2.direction;

    if (current_input.btn3_pressed) {
        if (p1_confirmed && p2_confirmed) {
            playTone(ATTACK_BEEP_FREQ, BEEP_MS);
            p1_hero = p1_choice;
            p2_hero = p2_choice;
            p1_health = heroInfo[p1_hero].max_hp;
            p2_health = heroInfo[p2_hero].max_hp;
            p1_charge = 0;
            p2_charge = 0;
            p1_x = 50;
            p1_y = 140;
            p2_x = 170;
            p2_y = 140;
            HAL_Delay(200);
            innerState = GAME_SCENE_SELECT;
        } else {
            p1_confirmed = 1;
            playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        }
    }
    if (current_input.btn2_pressed) {
        p2_confirmed = 1;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }

    uint8_t p1_box_colour = p1_confirmed ? 2 : 1;
    uint8_t p2_box_colour = p2_confirmed ? 2 : 1;

    LCD_Draw_Rect(8, 130, 105, 98, p1_box_colour, 0);
    LCD_Draw_Rect(10, 132, 101, 94, 13, 1);
    LCD_printString("P1", 18, 136, 2, 2);
    char p1_name[16];
    sprintf(p1_name, "%s", hero_names[p1_choice]);
    LCD_printString(p1_name, 18, 158, 1, 1);
    LCD_printString("Attack", 18, 176, 1, 1);
    char p1_stats[16];
    sprintf(p1_stats, "%d", hero_attack[p1_choice]);
    LCD_printString(p1_stats, 70, 176, 1, 1);
    LCD_printString("Ability", 18, 188, 1, 1);
    sprintf(p1_stats, "%d", hero_ability[p1_choice]);
    LCD_printString(p1_stats, 70, 188, 1, 1);
    LCD_printString("Life", 18, 200, 1, 1);
    sprintf(p1_stats, "%d", hero_life[p1_choice]);
    LCD_printString(p1_stats, 70, 200, 1, 1);
    LCD_printString("Range", 18, 212, 1, 1);
    sprintf(p1_stats, "%s", hero_range[p1_choice]);
    LCD_printString(p1_stats, 70, 212, 1, 1);

    LCD_Draw_Rect(127, 130, 105, 98, p2_box_colour, 0);
    LCD_Draw_Rect(129, 132, 101, 94, 13, 1);
    LCD_printString("P2", 137, 136, 2, 2);
    char p2_name[16];
    sprintf(p2_name, "%s", hero_names[p2_choice]);
    LCD_printString(p2_name, 137, 158, 1, 1);
    LCD_printString("Attack", 137, 176, 1, 1);
    char p2_stats[16];
    sprintf(p2_stats, "%d", hero_attack[p2_choice]);
    LCD_printString(p2_stats, 192, 176, 1, 1);
    LCD_printString("Ability", 137, 188, 1, 1);
    sprintf(p2_stats, "%d", hero_ability[p2_choice]);
    LCD_printString(p2_stats, 192, 188, 1, 1);
    LCD_printString("Life", 137, 200, 1, 1);
    sprintf(p2_stats, "%d", hero_life[p2_choice]);
    LCD_printString(p2_stats, 192, 200, 1, 1);
    LCD_printString("Range", 137, 212, 1, 1);
    sprintf(p2_stats, "%s", hero_range[p2_choice]);
    LCD_printString(p2_stats, 192, 212, 1, 1);

    if (p1_confirmed && p2_confirmed) {
        LCD_printString("Both Ready! Press P1 to Scene", 12, 230, 1, 1);
    }

    LCD_Refresh(&cfg0);
}

static void pickMap(void)
{
    static Direction last_scene_dir = CENTRE;

    buzzerTick();

    LCD_Fill_Buffer(13);
    LCD_printString("SCENE SELECT", 20, 15, 2, 3);

    static const char* scene_names[2] = {"DESERT", "FOREST"};

    if (selected_scene == 0) {
        LCD_Draw_Rect(40, 78, 160, 32, 2, 0);
        LCD_printString(">>", 15, 82, 2, 2);
    } else {
        LCD_Draw_Rect(40, 78, 160, 32, 1, 0);
    }
    LCD_printString(scene_names[0], 72, 84, 2, 2);

    if (selected_scene == 1) {
        LCD_Draw_Rect(40, 118, 160, 32, 2, 0);
        LCD_printString(">>", 15, 122, 2, 2);
    } else {
        LCD_Draw_Rect(40, 118, 160, 32, 1, 0);
    }
    LCD_printString(scene_names[1], 72, 124, 2, 2);

    LCD_printString("P1 UP/DOWN select", 25, 170, 1, 1);
    LCD_printString("P1 button to confirm", 20, 188, 1, 1);

    Direction current_direction = d1.direction;
    if (current_direction == N && last_scene_dir != N) {
        selected_scene = (selected_scene + 1) % 2;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    if (current_direction == S && last_scene_dir != S) {
        selected_scene = (selected_scene + 1) % 2;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    last_scene_dir = current_direction;

    LCD_Refresh(&cfg0);

    if (current_input.btn3_pressed) {
        playTone(ATTACK_BEEP_FREQ, BEEP_MS);
        HAL_Delay(200);
        
        // Show battle loading screen
        LCD_Fill_Buffer(13);
        LCD_printString("LOADING.....", 50, 80, 2, 2);
        
        // Draw progress bar background
        LCD_Draw_Rect(30, 120, 180, 20, 1, 0);  // Outer frame
        LCD_Draw_Rect(32, 122, 176, 16, 0, 1);  // Inner frame (black)
        
        // Progress bar animation
        for (int progress = 0; progress <= 100; progress += 5) {
            // Draw progress bar
            int bar_width = (progress * 172) / 100;  // 172 is internal width
            if (bar_width > 0) {
                LCD_Draw_Rect(34, 124, bar_width, 12, 10, 1);  // Green progress bar
            }
            
            // Show hint text
            LCD_printString("Use SKILL button for special attacks", 10, 160, 1, 1);
            LCD_printString("Use ATTACK button for normal attacks", 5, 175, 1, 1);
            LCD_printString("Watch out for the map hazards!", 15, 190, 1, 1);
            
            LCD_Refresh(&cfg0);
            HAL_Delay(50);  // Short delay for animation effect
        }
        
        realBattleNeedsReset = 1;  // Ensure battle starts from 90s
        needResetNow = 1;  // Maintain compatibility
        innerState = GAME_REAL_BATTLE;
    }
}

static void minigameStart(void)
{
    innerState = GAME_MINIGAME_SELECT;
}

static void minigameSelect(void)
{
    static int8_t miniMenuSelection = 0;
    static Direction last_mini_direction = CENTRE;
    
    buzzerTick();

    LCD_Fill_Buffer(13);
    LCD_printString("MINIGAME", 50, 15, 1, 3);  // White font
    
    LCD_printString("1. ICE HOCKEY", 35, 95, 1, 2);  // White font
    LCD_printString("2. COMING SOON", 35, 135, 1, 2);  // White font
    LCD_printString("3. EXIT", 35, 175, 1, 2);  // White font
    
    if (miniMenuSelection == 0) { LCD_Draw_Rect(25, 87, 190, 35, 2, 0); LCD_printString(">", 15, 95, 1, 3); }  // White arrow
    else if (miniMenuSelection == 1) { LCD_Draw_Rect(25, 127, 190, 35, 2, 0); LCD_printString(">", 15, 135, 1, 3); }  // White arrow
    else if (miniMenuSelection == 2) { LCD_Draw_Rect(25, 167, 190, 35, 2, 0); LCD_printString(">", 15, 175, 1, 3); }  // White arrow
    
    LCD_printString("P1 UP/DOWN select", 50, 210, 1, 1);  // White font, smaller, centered
    LCD_printString("BT3 to Confirm", 65, 225, 1, 1);  // White font, smaller, centered
    LCD_Refresh(&cfg0);
    
    Direction current_direction = d1.direction;
    if (current_direction == N && last_mini_direction != N) {
        miniMenuSelection = (miniMenuSelection + 2) % 3;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    else if (current_direction == S && last_mini_direction != S) {
        miniMenuSelection = (miniMenuSelection + 1) % 3;
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
    }
    
    last_mini_direction = current_direction;
    
    if (current_input.btn3_pressed) {
        playTone(ATTACK_BEEP_FREQ, BEEP_MS);
        HAL_Delay(200);
        if (miniMenuSelection == 0) {
            // Show hockey game loading screen
            LCD_Fill_Buffer(13);
            LCD_printString("LOADING ICE HOCKEY.....", 20, 80, 2, 2);
            
            // Draw progress bar background
            LCD_Draw_Rect(30, 120, 180, 20, 1, 0);  // Outer frame
            LCD_Draw_Rect(32, 122, 176, 16, 0, 1);  // Inner frame (black)
            
            // Progress bar animation
            for (int progress = 0; progress <= 100; progress += 5) {
                // Draw progress bar
                int bar_width = (progress * 172) / 100;  // 172 is internal width
                if (bar_width > 0) {
                    LCD_Draw_Rect(34, 124, bar_width, 12, 10, 1);  // Green progress bar
                }
                
                // Show hint text
                LCD_printString("Get ready to play!", 55, 160, 1, 1);
                LCD_printString("Use joysticks to move paddles", 30, 175, 1, 1);
                LCD_printString("Score goals to win!", 55, 190, 1, 1);
                
                LCD_Refresh(&cfg0);
                HAL_Delay(50);  // Short delay for animation effect
            }
            
            innerState = GAME_HOCKEY;
            current_input.btn3_pressed = 0;
        }
        else if (miniMenuSelection == 2) {
            innerState = GAME_MENU;
            menuSelection = 1;
            current_input.btn3_pressed = 0;
        }
    }
}

// ICE HOCKEY GAME
#define HOCKEY_PADDLE_WIDTH 4
#define HOCKEY_PADDLE_HEIGHT 30
#define HOCKEY_BALL_SIZE 3
#define HOCKEY_PADDLE_SPEED 8
#define HOCKEY_BALL_SPEED 5.0f
#define HOCKEY_MAX_SCORE 5

static int16_t hockey_p1_paddle_x = 10;
static int16_t hockey_p1_paddle_y = 100;
static int16_t hockey_p2_paddle_x = 226;
static int16_t hockey_p2_paddle_y = 100;
static float hockey_ball_x = 120.0f;
static float hockey_ball_y = 120.0f;
static float hockey_ball_vx = HOCKEY_BALL_SPEED;
static float hockey_ball_vy = 1.0f;
static uint8_t hockey_p1_score = 0;
static uint8_t hockey_p2_score = 0;
static uint32_t hockey_game_start = 0;
static uint8_t hockey_wait_for_start = 0;
static uint8_t hockey_collision_count = 0;  // Count collisions for speed increase

static void HockeyGame_Init(void)
{
    hockey_p1_paddle_x = 10;
    hockey_p1_paddle_y = 100;
    hockey_p2_paddle_x = 226;
    hockey_p2_paddle_y = 100;
    hockey_ball_x = 120.0f;
    hockey_ball_y = 120.0f;
    hockey_ball_vx = HOCKEY_BALL_SPEED;
    hockey_ball_vy = 1.0f;
    hockey_p1_score = 0;
    hockey_p2_score = 0;
    hockey_game_start = HAL_GetTick();
    hockey_wait_for_start = 0;
    hockey_collision_count = 0;
}

static void HockeyGame_Update(void)
{
    buzzerTick();

    if (hockey_wait_for_start) {
        if (current_input.btn3_pressed) {
            playTone(ATTACK_BEEP_FREQ, BEEP_MS);
            hockey_wait_for_start = 0;
            current_input.btn3_pressed = 0;
        }
        return;
    }
    
    // Update ball position
    hockey_ball_x += hockey_ball_vx;
    hockey_ball_y += hockey_ball_vy;
    
    // Top/bottom wall collision
    if (hockey_ball_y <= 0 || hockey_ball_y >= 240) {
        hockey_ball_vy = -hockey_ball_vy;
        hockey_ball_y = (hockey_ball_y <= 0) ? 2 : 238;
        hockey_collision_count++;
        if (hockey_collision_count >= 3) {
            hockey_collision_count = 0;
            if (hockey_ball_vx > 0) hockey_ball_vx += 1.0f;
            else hockey_ball_vx -= 1.0f;
        }
    }
    
    // Left/right wall collision (except goal areas)
    if (hockey_ball_x <= 0 && (hockey_ball_y < 80 || hockey_ball_y > 160)) {
        hockey_ball_vx = -hockey_ball_vx;
        hockey_ball_x = 2;
        hockey_collision_count++;
        if (hockey_collision_count >= 3) {
            hockey_collision_count = 0;
            if (hockey_ball_vy > 0) hockey_ball_vy += 1.0f;
            else hockey_ball_vy -= 1.0f;
        }
    }
    if (hockey_ball_x >= 240 && (hockey_ball_y < 80 || hockey_ball_y > 160)) {
        hockey_ball_vx = -hockey_ball_vx;
        hockey_ball_x = 238;
        hockey_collision_count++;
        if (hockey_collision_count >= 3) {
            hockey_collision_count = 0;
            if (hockey_ball_vy > 0) hockey_ball_vy += 1.0f;
            else hockey_ball_vy -= 1.0f;
        }
    }
    
    // Left paddle (P1) collision
    if (hockey_ball_x + HOCKEY_BALL_SIZE > hockey_p1_paddle_x &&
        hockey_ball_x < hockey_p1_paddle_x + HOCKEY_PADDLE_WIDTH &&
        hockey_ball_y + HOCKEY_BALL_SIZE > hockey_p1_paddle_y &&
        hockey_ball_y < hockey_p1_paddle_y + HOCKEY_PADDLE_HEIGHT &&
        hockey_ball_vx < 0) {
        hockey_ball_vx = -hockey_ball_vx;
        hockey_ball_x = hockey_p1_paddle_x + HOCKEY_PADDLE_WIDTH + 1;
        hockey_collision_count++;
        if (hockey_collision_count >= 3) {
            hockey_collision_count = 0;
            if (hockey_ball_vx > 0) hockey_ball_vx += 1.0f;
            else hockey_ball_vx -= 1.0f;
        }
    }

    // Right paddle (P2) collision
    if (hockey_ball_x < hockey_p2_paddle_x + HOCKEY_PADDLE_WIDTH &&
        hockey_ball_x + HOCKEY_BALL_SIZE > hockey_p2_paddle_x &&
        hockey_ball_y + HOCKEY_BALL_SIZE > hockey_p2_paddle_y &&
        hockey_ball_y < hockey_p2_paddle_y + HOCKEY_PADDLE_HEIGHT &&
        hockey_ball_vx > 0) {
        hockey_ball_vx = -hockey_ball_vx;
        hockey_ball_x = hockey_p2_paddle_x - HOCKEY_BALL_SIZE - 1;
        hockey_collision_count++;
        if (hockey_collision_count >= 3) {
            hockey_collision_count = 0;
            if (hockey_ball_vx > 0) hockey_ball_vx += 1.0f;
            else hockey_ball_vx -= 1.0f;
        }
    }
    
    // Goal detection - expanded goal zones
    if (hockey_ball_x < 0 && hockey_ball_y >= 80 && hockey_ball_y <= 160) {
        hockey_p2_score++;
        playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
        hockey_ball_x = 120.0f;
        hockey_ball_y = 120.0f;
        hockey_ball_vx = HOCKEY_BALL_SPEED;
        hockey_collision_count = 0;
        hockey_wait_for_start = 1;
    }
    if (hockey_ball_x > 240 && hockey_ball_y >= 80 && hockey_ball_y <= 160) {
        hockey_p1_score++;
        playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
        hockey_ball_x = 120.0f;
        hockey_ball_y = 120.0f;
        hockey_ball_vx = -HOCKEY_BALL_SPEED;
        hockey_collision_count = 0;
        hockey_wait_for_start = 1;
    }
    
    // Paddle control - 4-direction movement (up/down/left/right) with center line boundary
    // P1 paddle (left side): can move left from x=10 to x=5, right to x=120 (center line)
    if (d1.direction == N) {
        if (hockey_p1_paddle_y > 0) {
            hockey_p1_paddle_y -= HOCKEY_PADDLE_SPEED;
        }
    }
    if (d1.direction == S) {
        if (hockey_p1_paddle_y < 240 - HOCKEY_PADDLE_HEIGHT) {
            hockey_p1_paddle_y += HOCKEY_PADDLE_SPEED;
        }
    }
    if (d1.direction == E) {
        if (hockey_p1_paddle_x < 120 - HOCKEY_PADDLE_WIDTH) {  // Cannot cross center line
            hockey_p1_paddle_x += HOCKEY_PADDLE_SPEED;
        }
    }
    if (d1.direction == W) {
        if (hockey_p1_paddle_x > 5) {  // Left boundary
            hockey_p1_paddle_x -= HOCKEY_PADDLE_SPEED;
        }
    }
    
    // P2 paddle (right side): can move right to x=235, left to x=120 (center line)
    if (d2.direction == N) {
        if (hockey_p2_paddle_y > 0) {
            hockey_p2_paddle_y -= HOCKEY_PADDLE_SPEED;
        }
    }
    if (d2.direction == S) {
        if (hockey_p2_paddle_y < 240 - HOCKEY_PADDLE_HEIGHT) {
            hockey_p2_paddle_y += HOCKEY_PADDLE_SPEED;
        }
    }
    if (d2.direction == E) {
        if (hockey_p2_paddle_x < 236 - HOCKEY_PADDLE_WIDTH) {  // Right boundary
            hockey_p2_paddle_x += HOCKEY_PADDLE_SPEED;
        }
    }
    if (d2.direction == W) {
        if (hockey_p2_paddle_x > 120) {  // Cannot cross center line
            hockey_p2_paddle_x -= HOCKEY_PADDLE_SPEED;
        }
    }
}

static void HockeyGame_Draw(void)
{
    LCD_Fill_Buffer(13);
    
    // Draw scores - left and right format
    char score_buf[30];
    sprintf(score_buf, "P1: %d", hockey_p1_score);
    LCD_printString(score_buf, 10, 10, 1, 2);
    
    sprintf(score_buf, "%d :P2", hockey_p2_score);
    LCD_printString(score_buf, 180, 10, 1, 2);
    
    // Draw expanded goal zones (black color = 0)
    LCD_Draw_Rect(0, 80, 5, 80, 0, 1);   // Left goal (expanded)
    LCD_Draw_Rect(235, 80, 5, 80, 0, 1); // Right goal (expanded)
    
    // Draw paddles
    LCD_Draw_Rect(hockey_p1_paddle_x, hockey_p1_paddle_y, HOCKEY_PADDLE_WIDTH, HOCKEY_PADDLE_HEIGHT, 2, 1);
    LCD_Draw_Rect(hockey_p2_paddle_x, hockey_p2_paddle_y, HOCKEY_PADDLE_WIDTH, HOCKEY_PADDLE_HEIGHT, 2, 1);
    
    // Draw ball
    LCD_Draw_Rect((int16_t)hockey_ball_x, (int16_t)hockey_ball_y, HOCKEY_BALL_SIZE, HOCKEY_BALL_SIZE, 1, 1);
    
    // Draw center line
    for (int y = 40; y < 200; y += 10) {
        LCD_Set_Pixel(120, y, 13);
    }
    
    if (hockey_wait_for_start) {
        LCD_printString("P1 Press Joystick Button", 40, 200, 1, 1);
        LCD_printString("to Start Next Round", 50, 215, 1, 1);
    }
    
    LCD_Refresh(&cfg0);
}

static void hockeyMatch(void)
{
    static uint8_t hockey_init = 0;
    
    if (!hockey_init) {
        HockeyGame_Init();
        hockey_init = 1;
    }
    
    HockeyGame_Update();
    HockeyGame_Draw();
    
    // Check for winner or exit
    if (hockey_p1_score >= HOCKEY_MAX_SCORE || hockey_p2_score >= HOCKEY_MAX_SCORE) {
        // Display winner
        LCD_Fill_Buffer(13);
        if (hockey_p1_score >= HOCKEY_MAX_SCORE) {
            LCD_printString("P1 WINS!", 80, 100, 2, 3);
        } else {
            LCD_printString("P2 WINS!", 80, 100, 2, 3);
        }
        LCD_printString("Press P1 Button", 50, 160, 1, 1);
        LCD_printString("to Continue", 60, 175, 1, 1);
        LCD_Refresh(&cfg0);
        
        // Wait for button press
        current_input.btn3_pressed = 0;
        while (!current_input.btn3_pressed) {
            Input_Read();
            buzzerTick();  // keep buzzer state moving while waiting
            if (current_input.btn3_pressed) {
                playTone(ATTACK_BEEP_FREQ, BEEP_MS);
                break;
            }
            HAL_Delay(50);
        }
        
        HAL_Delay(200);
        hockey_init = 0;
        innerState = GAME_MINIGAME_SELECT;
        current_input.btn3_pressed = 0;
    } else if (current_input.btn6_pressed) {
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        HAL_Delay(200);
        hockey_init = 0;
        innerState = GAME_MINIGAME_SELECT;
        current_input.btn6_pressed = 0;
    }
}

static void DrawPixelCrown(int16_t x, int16_t y, uint8_t color)
{
    const uint8_t scale = 3;
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 10; col++) {
            if (PixelCrownSprite[row][col]) {
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        LCD_Set_Pixel(x + col * scale + dx, y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

static void DrawWinnerHero(int16_t x, int16_t y, uint8_t hero)
{
    const uint8_t scale = 8;
    static uint32_t last_frame_time = 0;
    static uint8_t animation_frame = 0;
    uint32_t current_time = HAL_GetTick();
    
    // Update animation frame every 100ms
    if (current_time - last_frame_time > 100) {
        animation_frame = (animation_frame + 1) % 8;
        last_frame_time = current_time;
    }
    
    // Display animation at original size
    if (hero == 0) {
        // Warrior: show shield swinging animation during battle movement
        LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroWarriorBaseSprite, scale);
        int16_t shield_x = x + 6;
        shield_x += (animation_frame % 4 - 2); // Shield swings left and right
        LCD_Draw_Sprite_Scaled(shield_x, y + 16, 10, 10, (uint8_t*)HeroShieldSprite, 2);
    } else if (hero == 1) {
        // Assassin: show walking animation
        if ((animation_frame % 4) < 2) {
            LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroAssassinWalkSprite1, scale);
        } else {
            LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroAssassinWalkSprite2, scale);
        }
    } else {
        // Mage: show wand moving up and down animation, position consistent with battle (adjusted proportionally)
        LCD_Draw_Sprite_Scaled(x, y, 8, 8, (uint8_t*)HeroMageBaseSprite, scale);
        int16_t wand_y = y + 10;  // Adjust base position to fit larger scale
        wand_y += (animation_frame % 4 - 2); // Wand moves up and down, always animated
        LCD_Draw_Line(x + 48, wand_y, x + 48, y + 40, 14);  // Adjust coordinates and length by 8/5 ratio
        LCD_Draw_Circle(x + 48, wand_y, 3, 5, 1);
        LCD_Draw_Line(x + 35, y + 16, x + 45, y + 19, 4);  // Adjust second line position
    }
}

static void endScreen(void)
{
    // Set state reset flags so normal restart is possible after returning to menu
    reset_fighting_state = 1;
    reset_heroselect_state = 1;
    realBattleNeedsReset = 1;  // Ensure next battle starts from 90s
    
    buzzerTick();

    // Determine winner by health
    uint8_t winner = 0;
    if (p1_health > p2_health) {
        winner = 1;
    } else if (p2_health > p1_health) {
        winner = 2;
    }

    LCD_Fill_Buffer(13);
    LCD_printString("GAME OVER", 45, 10, 2, 3);

    if (winner == 1) {
        LCD_printString("P1 WINS", 78, 36, 2, 2);
    } else if (winner == 2) {
        LCD_printString("P2 WINS", 78, 36, 2, 2);
    } else {
        LCD_printString("DRAW", 96, 36, 2, 2);
    }

    if (winner == 1) {
        DrawWinnerHero(88, 92, p1_hero);
        DrawPixelCrown(104, 69, 10);
    } else if (winner == 2) {
        DrawWinnerHero(88, 92, p2_hero);
        DrawPixelCrown(104, 69, 10);
    }

    LCD_printString("PRESS P1 BUTTON TO EXIT", 52, 210, 2, 1);
    LCD_Refresh(&cfg0);

    // Play victory music after interface display (if there is a winner)
    if (winner != 0) {
        // Victory melody: 7 notes C4 - E4 - G4 - C5 - G4 - E4 - C5 (about 3.5 seconds)
        uint32_t victory_notes[] = {523, 659, 784, 1047, 784, 659, 1047}; // C4, E4, G4, C5, G4, E4, C5
        uint16_t note_durations[] = {150, 150, 150, 200, 150, 150, 2200}; // Total duration about 3.5 seconds
        
        for (int i = 0; i < 7; i++) {
            playTone(victory_notes[i], note_durations[i]);
            HAL_Delay(note_durations[i] + 50); // Interval between notes
        }
    }

    // Wait for P1 joystick button to exit - continuously update animation during wait
    current_input.btn3_pressed = 0;
    while (!current_input.btn3_pressed) {
        Input_Read();
        buzzerTick();  // Also update buzzer in wait loop
        
        // Redraw interface to update hero animation
        LCD_Fill_Buffer(13);
        LCD_printString("GAME OVER", 45, 10, 2, 3);

        if (winner == 1) {
            LCD_printString("P1 WINS", 78, 36, 2, 2);
        } else if (winner == 2) {
            LCD_printString("P2 WINS", 78, 36, 2, 2);
        } else {
            LCD_printString("DRAW", 96, 36, 2, 2);
        }

        if (winner == 1) {
            DrawWinnerHero(88, 92, p1_hero);
            DrawPixelCrown(104, 69, 10);
        } else if (winner == 2) {
            DrawWinnerHero(88, 92, p2_hero);
            DrawPixelCrown(104, 69, 10);
        }

        LCD_printString("PRESS P1 BUTTON TO EXIT", 52, 210, 2, 1);
        LCD_Refresh(&cfg0);
        
        if (current_input.btn3_pressed) {
            playTone(ATTACK_BEEP_FREQ, BEEP_MS);
            break;
        }
        HAL_Delay(50);
    }

    innerState = GAME_MENU;
}

// Battle Mode
static void runRealBattle(void)
{
    uint8_t bg_color = 13;
    uint8_t dot_color = (selected_scene == 0) ? 6 : 12;

    // Initial setup - ensure complete reset every time entering battle
    static uint8_t last_spawn_time = 0;

    // Check if battle initialization is needed
    if (realBattleNeedsReset) {
        // Reset all battle-related variables
        battle_start_time = HAL_GetTick();  // Set start time - must reset every time
        last_spawn_time = 0;
        p1_freeze_end = 0;
        p2_freeze_end = 0;
        p1_shield_end = 0;
        p2_shield_end = 0;
        p1_effect_end = 0;
        p2_effect_end = 0;
        p1_beam_end = 0;
        p2_beam_end = 0;
        p1_led_flash_end = 0;
        p2_led_flash_end = 0;

        // Reset player status
        p1_health = heroInfo[p1_hero].max_hp;
        p2_health = heroInfo[p2_hero].max_hp;
        p1_charge = 0;
        p2_charge = 0;

        // Reset all game objects
        for (int i = 0; i < MAX_ITEMS; i++) {
            items[i].active = 0;
        }
        for (int i = 0; i < MAX_CACTUS; i++) {
            cactus[i].active = 0;
        }
        for (int i = 0; i < MAX_PUDDLES; i++) {
            puddles[i].active = 0;
            puddles[i].expire_time = 0;
        }
        for (int i = 0; i < MAX_DARTS; i++) {
            p1_darts[i].active = 0;
            p2_darts[i].active = 0;
        }

        realBattleNeedsReset = 0;  // Clear flag until next time needed
        battle_live = 1;
    }

    LCD_Fill_Buffer(bg_color);
    
    buzzerTick();
    animation_frame++;

    // Calculate the combat time

    uint32_t battle_time = HAL_GetTick() - battle_start_time;
    uint8_t battle_seconds = battle_time / 1000;
    uint8_t battle_remaining = (battle_seconds >= 90) ? 0 : (90 - battle_seconds);
    
    // The battle will end in 90 seconds.
    if (battle_seconds >= 90) {
        // Completely reset battle state, turn off LEDs
        PWM_SetDuty(&p1_led_cfg, 0);
        PWM_SetDuty(&pwm_cfg, 0);
        battle_live = 0;
        realBattleNeedsReset = 1;  // Mark that reset is needed next time entering battle
        innerState = GAME_RESULT;
        return;
    }

    // Display countdown timer
    char time_buf[16];
    sprintf(time_buf, "%02d", battle_remaining);
    LCD_printString(time_buf, 110, 2, 2, 2);

    // spawn a few items, but cap health and skill packs
    // It will start generating after 15 seconds.
    if (battle_seconds >= 15 && animation_frame % 50 == 0) {
        uint8_t health_count = 0, skill_count = 0, total_count = 0;
        for (int i = 0; i < 5; i++) {
            if (items[i].active) {
                total_count++;
                if (items[i].type == 0) health_count++;
                else skill_count++;
            }
        }
        
        // The total number of items does not exceed 4
        if (total_count < 4) {
            for (int i = 0; i < 5; i++) {
                if (!items[i].active) {
                    uint32_t random_val;
                    HAL_RNG_GenerateRandomNumber(&hrng, &random_val);
                    
                    // Prioritize generating the missing types
                    uint8_t new_type;
                    if (health_count >= 2 && skill_count < 2) {
                        new_type = 1;  // Generate Skill Pack
                    } else if (skill_count >= 2 && health_count < 2) {
                        new_type = 0;  // Generate blood bag
                    } else if (health_count < skill_count) {
                        new_type = 0;  // Blood bag is low.
                    } else {
                        new_type = 1;  // Few or equal skill packages
                    }
                    
                    // Expand the dispersion location
                    uint8_t zone = (random_val >> 24) % 6;
                    int16_t base_x, base_y;
                    switch(zone) {
                        case 0: base_x = 50; base_y = 90; break;
                        case 1: base_x = 120; base_y = 80; break;
                        case 2: base_x = 190; base_y = 100; break;
                        case 3: base_x = 60; base_y = 160; break;
                        case 4: base_x = 120; base_y = 180; break;
                        default: base_x = 180; base_y = 160; break;
                    }
                    items[i].x = base_x + ((random_val % 50) - 25);
                    items[i].y = base_y + (((random_val >> 8) % 50) - 25);
                    if (items[i].x < 30) items[i].x = 30;
                    if (items[i].x > 210) items[i].x = 210;
                    if (items[i].y < 70) items[i].y = 70;
                    if (items[i].y > 210) items[i].y = 210;
                    items[i].type = new_type;
                    items[i].active = 1;
                    break;
                }
            }
        }
    }

    // Time rule: Generation begins when the countdown reaches 70, and then it occurs every 10 seconds thereafter.
    uint8_t should_spawn = 0;
    uint8_t spawn_count = 0;

    if (battle_remaining == 70 && last_spawn_time != 70) {
        // Generation begins at 70 seconds countdown.
        should_spawn = 1;
        spawn_count = (selected_scene == 0) ? 10 : 5;
        last_spawn_time = 70;
    } else if (battle_remaining < 70 && battle_remaining % 10 == 0 && battle_remaining != last_spawn_time) {
        // Then, a new one is generated every 10 seconds.
        should_spawn = 1;
        spawn_count = (selected_scene == 0) ? 8 : 5;
        last_spawn_time = battle_remaining;
    }

    if (should_spawn) {
        uint8_t spawned = 0;
        if (selected_scene == 0) {
            // Desert red thorns - play warning music
            playTone(WARNING_FREQ, WARNING_BEEP_MS);
            HAL_Delay(150);
            playTone(WARNING_FREQ, WARNING_BEEP_MS);
            
            for (int i = 0; i < MAX_CACTUS && spawned < spawn_count; i++) {
                if (!cactus[i].active) {
                    uint32_t random_val;
                    HAL_RNG_GenerateRandomNumber(&hrng, &random_val);

                    cactus[i].x = 60 + (random_val % 120);
                    cactus[i].y = 80 + ((random_val >> 8) % 80);

                    uint8_t dir = (random_val >> 16) % 8;
                    int8_t spd = 4;
                    switch(dir) {
                        case 0: cactus[i].vx = spd; cactus[i].vy = 0; break;    // E
                        case 1: cactus[i].vx = spd; cactus[i].vy = spd; break;  // SE
                        case 2: cactus[i].vx = 0; cactus[i].vy = spd; break;    // S
                        case 3: cactus[i].vx = -spd; cactus[i].vy = spd; break; // SW
                        case 4: cactus[i].vx = -spd; cactus[i].vy = 0; break;   // W
                        case 5: cactus[i].vx = -spd; cactus[i].vy = -spd; break;// NW
                        case 6: cactus[i].vx = 0; cactus[i].vy = -spd; break;   // N
                        default: cactus[i].vx = spd; cactus[i].vy = -spd; break;// NE
                    }
                    cactus[i].bounce_count = 0;
                    cactus[i].active = 1;
                    spawned++;
                }
            }
        } else {
            // The forest has light blue puddles - play warning music
            playTone(WARNING_FREQ, WARNING_BEEP_MS);
            HAL_Delay(150);
            playTone(WARNING_FREQ, WARNING_BEEP_MS);
            
            for (int i = 0; i < MAX_PUDDLES && spawned < spawn_count; i++) {
                if (!puddles[i].active) {
                    uint32_t random_val;
                    HAL_RNG_GenerateRandomNumber(&hrng, &random_val);
                    uint8_t zone = (random_val >> 24) % 8;
                    int16_t base_x, base_y;
                    switch (zone) {
                        case 0: base_x = 30; base_y = 70; break;
                        case 1: base_x = 120; base_y = 70; break;
                        case 2: base_x = 210; base_y = 70; break;
                        case 3: base_x = 30; base_y = 120; break;
                        case 4: base_x = 120; base_y = 120; break;
                        case 5: base_x = 210; base_y = 120; break;
                        case 6: base_x = 30; base_y = 170; break;
                        case 7: base_x = 120; base_y = 170; break;
                        default: base_x = 210; base_y = 170; break;
                    }
                    puddles[i].x = base_x + ((random_val >> 8) % 80) - 40;
                    puddles[i].y = base_y + ((random_val >> 16) % 80) - 40;
                    puddles[i].active = 1;
                    puddles[i].expire_time = HAL_GetTick() + 10000;
                    spawned++;
                }
            }
        }
    }

    // Spike movement and boundary bounce
    for (int i = 0; i < MAX_CACTUS; i++) {
        if (!cactus[i].active) continue;
        cactus[i].x += cactus[i].vx;
        cactus[i].y += cactus[i].vy;

        if (cactus[i].x <= 12) {
            cactus[i].x = 12;
            cactus[i].vx = -cactus[i].vx;
            cactus[i].bounce_count++;
        }
        if (cactus[i].x >= 228) {
            cactus[i].x = 228;
            cactus[i].vx = -cactus[i].vx;
            cactus[i].bounce_count++;
        }
        if (cactus[i].y <= 12) {
            cactus[i].y = 12;
            cactus[i].vy = -cactus[i].vy;
            cactus[i].bounce_count++;
        }
        if (cactus[i].y >= 228) {
            cactus[i].y = 228;
            cactus[i].vy = -cactus[i].vy;
            cactus[i].bounce_count++;
        }
        if (cactus[i].bounce_count >= 3) {
            cactus[i].active = 0;
            continue;
        }

        // Spike collision with players
        if (cactus[i].active) {
            uint32_t now = HAL_GetTick();
            Hitbox p1_body = GetHeroBody(p1_x, p1_y, p1_hero);
            Hitbox p2_body = GetHeroBody(p2_x, p2_y, p2_hero);
            Hitbox cactus_hit = {cactus[i].x - 6, cactus[i].y - 6, 12, 12};
            if (AABBOverlap(p1_body, cactus_hit)) {
                uint16_t damage = 10;
                if (p1_hero == 0 && now < p1_shield_end) {
                    damage = damage / 2;  // Warrior 50% damage reduction
                }
                p1_health = (p1_health > damage) ? p1_health - damage : 0;
                p1_led_flash_end = HAL_GetTick() + 120;
                playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
                cactus[i].active = 0;
            }
            if (cactus[i].active && AABBOverlap(p2_body, cactus_hit)) {
                uint16_t damage = 10;
                if (p2_hero == 0 && now < p2_shield_end) {
                    damage = damage / 2;  // Warrior 50% damage reduction
                }
                p2_health = (p2_health > damage) ? p2_health - damage : 0;
                p2_led_flash_end = HAL_GetTick() + 120;
                playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
                cactus[i].active = 0;
            }
        }
    }

    if (selected_scene == 1) {
        uint32_t now = HAL_GetTick();
        Hitbox p1_body = GetHeroBody(p1_x, p1_y, p1_hero);
        Hitbox p2_body = GetHeroBody(p2_x, p2_y, p2_hero);
        for (int i = 0; i < MAX_PUDDLES; i++) {
            if (!puddles[i].active) continue;
            if (now >= puddles[i].expire_time) {
                puddles[i].active = 0;
                continue;
            }
            Hitbox puddle_hit = {puddles[i].x - 8, puddles[i].y - 8, 16, 16};
            if (AABBOverlap(p1_body, puddle_hit)) {
                p1_freeze_end = now + 2000;
                p1_charge = (p1_charge > 10) ? p1_charge - 10 : 0;
                playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
                puddles[i].active = 0;
                continue;
            }
            if (AABBOverlap(p2_body, puddle_hit)) {
                p2_freeze_end = now + 2000;
                p2_charge = (p2_charge > 10) ? p2_charge - 10 : 0;
                playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
                puddles[i].active = 0;
                continue;
            }
        }
    }

    for (int16_t y = 0; y < 240; y += 8) {
        for (int16_t x = 0; x < 240; x += 8) {
            if (((x * 13 + y * 17) % 23) < 6) {
                LCD_Draw_Rect(x, y, 2, 2, dot_color, 1);
            }
        }
    }

    // Health bar (white fill)
    uint8_t p1_bar_width = (p1_health * 86) / heroInfo[p1_hero].max_hp;
    uint8_t p2_bar_width = (p2_health * 86) / heroInfo[p2_hero].max_hp;
    uint8_t p1_skill_width = (p1_charge * 90) / SKILL_MAX;
    uint8_t p2_skill_width = (p2_charge * 90) / SKILL_MAX;

    LCD_Draw_Rect(10, 20, 90, 12, 1, 0);                                // P1 border
    if (p1_bar_width > 0) {
        LCD_Draw_Rect(98 - p1_bar_width, 22, p1_bar_width, 8, 2, 1);
    }
    LCD_Draw_Rect(10, 34, 90, 8, 1, 0);                                 // P1 skill bar frame
    if (p1_skill_width > 0) {
        if (p1_skill_width > 90) p1_skill_width = 90;
        uint8_t skill_color = (p1_charge == SKILL_MAX) ? 8 : 4;
        LCD_Draw_Rect(10, 34, p1_skill_width, 8, skill_color, 1);
    }
    uint32_t marker_time = HAL_GetTick();
    if (marker_time < p1_freeze_end) {
        LCD_Draw_Circle(104, 40, 2, 14, 1);
        LCD_Draw_Line(104, 42, 104, 44, 14);
    }

    LCD_Draw_Rect(135, 20, 90, 12, 1, 0);                               // P2 border
    if (p2_bar_width > 0) {
        LCD_Draw_Rect(137, 22, p2_bar_width, 8, 2, 1);
    }
    LCD_Draw_Rect(135, 34, 90, 8, 1, 0);                                // P2 skill bar frame
    if (p2_skill_width > 0) {
        if (p2_skill_width > 90) p2_skill_width = 90;
        uint8_t skill_color = (p2_charge == SKILL_MAX) ? 8 : 4;
        LCD_Draw_Rect(225 - p2_skill_width, 34, p2_skill_width, 8, skill_color, 1);
    }
    if (marker_time < p2_freeze_end) {
        LCD_Draw_Circle(131, 40, 2, 14, 1);
        LCD_Draw_Line(131, 42, 131, 44, 14);
    }

    LCD_printString("P1", 103, 22, 1, 1);
    LCD_printString("P2", 122, 22, 1, 1);
    LCD_printString("SK", 44, 35, 1, 1);
    LCD_printString("SK", 170, 35, 1, 1);
    char hp_buf[24];
    sprintf(hp_buf, "%d", p1_health);
    int16_t p1_text_x = 98 - p1_bar_width + 2;
    LCD_printString(hp_buf, p1_text_x, 22, 1, 1);
    sprintf(hp_buf, "%d", p2_health);
    int16_t p2_text_x = 137 + p2_bar_width - 12;
    LCD_printString(hp_buf, p2_text_x, 22, 1, 1);

    // Draw heroes
    uint8_t p1_moving = (d1.direction != CENTRE);
    uint8_t p2_moving = (d2.direction != CENTRE);
    if (p1_hero == 0) DrawWarriorBattle(p1_x, p1_y, animation_frame, p1_moving);
    else if (p1_hero == 1) DrawAssassinBattle(p1_x, p1_y, animation_frame, p1_moving);
    else DrawMageBattle(p1_x, p1_y, animation_frame, p1_moving);

    if (p2_hero == 0) DrawWarriorBattle(p2_x, p2_y, animation_frame, p2_moving);
    else if (p2_hero == 1) DrawAssassinBattle(p2_x, p2_y, animation_frame, p2_moving);
    else DrawMageBattle(p2_x, p2_y, animation_frame, p2_moving);

    // Draw skill effects
    uint32_t now_draw = HAL_GetTick();
    // Warrior aura
    if (p1_hero == 0 && now_draw < p1_shield_end) {
        DrawWarriorAura(p1_x, p1_y, GetHeroWidth(p1_hero), GetHeroHeight(p1_hero));
    }
    if (p2_hero == 0 && now_draw < p2_shield_end) {
        DrawWarriorAura(p2_x, p2_y, GetHeroWidth(p2_hero), GetHeroHeight(p2_hero));
    }

    // Draw darts
    for (int i = 0; i < MAX_DARTS; i++) {
        if (p1_darts[i].active) {
            float angle = atan2f(p1_darts[i].vy, p1_darts[i].vx) * 180.0f / 3.14159f;
            DrawDart(p1_darts[i].x, p1_darts[i].y, angle);
        }
        if (p2_darts[i].active) {
            float angle = atan2f(p2_darts[i].vy, p2_darts[i].vx) * 180.0f / 3.14159f;
            DrawDart(p2_darts[i].x, p2_darts[i].y, angle);
        }
    }

    // Draw sharp spikes (solid red triangles)
    if (selected_scene == 0) {
        for (int i = 0; i < MAX_CACTUS; i++) {
            if (cactus[i].active) {
                int16_t cx = cactus[i].x;
                int16_t cy = cactus[i].y;
                for (int dy = -6; dy <= 6; dy++) {
                    int16_t y = cy + dy;
                    int16_t left, right;
                    if (dy <= 0) {
                        left = cx + dy;
                        right = cx - dy;
                    } else {
                        left = cx - 6 + dy;
                        right = cx + 6 - dy;
                    }
                    if (left < right) {
                        LCD_Draw_Line(left, y, right, y, 2);
                    }
                }
            }
        }
    }

    // Forest map - light blue small puddles
    if (selected_scene == 1) {
        for (int i = 0; i < MAX_PUDDLES; i++) {
            if (puddles[i].active) {
                LCD_Draw_Circle(puddles[i].x, puddles[i].y, 8, 14, 1);
            }
        }
    }

    // Draw items
    for (int i = 0; i < 5; i++) {
        if (items[i].active) {
            uint8_t color = items[i].type ? 4 : 3; // blue for skill, green for health
            // Bold cross lines
            for (int offset = -1; offset <= 1; offset++) {
                LCD_Draw_Line(items[i].x - 6, items[i].y + offset, items[i].x + 6, items[i].y + offset, color);
                LCD_Draw_Line(items[i].x + offset, items[i].y - 6, items[i].x + offset, items[i].y + 6, color);
            }
        }
    }

    // Check item collection
    Hitbox p1_body = GetHeroBody(p1_x, p1_y, p1_hero);
    for (int i = 0; i < 5; i++) {
        if (items[i].active &&
            p1_body.x < items[i].x + 5 && items[i].x - 5 < p1_body.x + p1_body.w &&
            p1_body.y < items[i].y + 5 && items[i].y - 5 < p1_body.y + p1_body.h) {
            if (items[i].type == 0) {
                p1_health = (p1_health + 15 > heroInfo[p1_hero].max_hp) ? heroInfo[p1_hero].max_hp : p1_health + 15;
                playTone(HEALTH_PICKUP_FREQ, PICKUP_BEEP_MS);
            } else {
                p1_charge = (p1_charge + 5 > SKILL_MAX) ? SKILL_MAX : p1_charge + 5;
                playTone(SKILL_PICKUP_FREQ, PICKUP_BEEP_MS);
            }
            items[i].active = 0;
        }
    }
    Hitbox p2_body = GetHeroBody(p2_x, p2_y, p2_hero);
    for (int i = 0; i < 5; i++) {
        if (items[i].active &&
            p2_body.x < items[i].x + 5 && items[i].x - 5 < p2_body.x + p2_body.w &&
            p2_body.y < items[i].y + 5 && items[i].y - 5 < p2_body.y + p2_body.h) {
            if (items[i].type == 0) {
                p2_health = (p2_health + 15 > heroInfo[p2_hero].max_hp) ? heroInfo[p2_hero].max_hp : p2_health + 15;
                playTone(HEALTH_PICKUP_FREQ, PICKUP_BEEP_MS);
            } else {
                p2_charge = (p2_charge + 5 > SKILL_MAX) ? SKILL_MAX : p2_charge + 5;
                playTone(SKILL_PICKUP_FREQ, PICKUP_BEEP_MS);
            }
            items[i].active = 0;
        }
    }

    // Recording orientation
    if (d1.direction != CENTRE && d1.angle >= 0.0f) p1_facing_angle = d1.angle;
    if (d2.direction != CENTRE && d2.angle >= 0.0f) p2_facing_angle = d2.angle;

    // Movement + Collision Prevention
    uint32_t movement_time = HAL_GetTick();
    uint8_t p1_frozen = (movement_time < p1_freeze_end);
    uint8_t p2_frozen = (movement_time < p2_freeze_end);
    uint8_t p1_speed = heroInfo[p1_hero].move_speed;
    uint8_t p2_speed = heroInfo[p2_hero].move_speed;

    // Calculate the new position of P1
    int16_t p1_new_x = p1_x, p1_new_y = p1_y;
    if (!p1_frozen) {
        if (d1.direction == W || d1.direction == NW || d1.direction == SW) p1_new_x -= p1_speed;
        if (d1.direction == E || d1.direction == NE || d1.direction == SE) p1_new_x += p1_speed;
        if (d1.direction == N || d1.direction == NE || d1.direction == NW) p1_new_y -= p1_speed;
        if (d1.direction == S || d1.direction == SE || d1.direction == SW) p1_new_y += p1_speed;
    }
    
    // Calculate the new position of P2
    int16_t p2_new_x = p2_x, p2_new_y = p2_y;
    if (!p2_frozen) {
        if (d2.direction == W || d2.direction == NW || d2.direction == SW) p2_new_x -= p2_speed;
        if (d2.direction == E || d2.direction == NE || d2.direction == SE) p2_new_x += p2_speed;
        if (d2.direction == N || d2.direction == NE || d2.direction == NW) p2_new_y -= p2_speed;
        if (d2.direction == S || d2.direction == SE || d2.direction == SW) p2_new_y += p2_speed;
    }

    // Boundary, dynamically restricted according to the current hero size
    int16_t p1_max_x = 240 - GetHeroWidth(p1_hero);
    int16_t p1_max_y = 240 - GetHeroHeight(p1_hero);
    int16_t p2_max_x = 240 - GetHeroWidth(p2_hero);
    int16_t p2_max_y = 240 - GetHeroHeight(p2_hero);

    if (p1_new_x < 0) p1_new_x = 0;
    if (p1_new_x > p1_max_x) p1_new_x = p1_max_x;
    if (p1_new_y < 0) p1_new_y = 0;
    if (p1_new_y > p1_max_y) p1_new_y = p1_max_y;

    if (p2_new_x < 0) p2_new_x = 0;
    if (p2_new_x > p2_max_x) p2_new_x = p2_max_x;
    if (p2_new_y < 0) p2_new_y = 0;
    if (p2_new_y > p2_max_y) p2_new_y = p2_max_y;

    // Collision detection: 
    // If the new position would cause a collision, then cancel this movement.
    Hitbox p1_new_body = GetHeroBody(p1_new_x, p1_new_y, p1_hero);
    Hitbox p2_new_body = GetHeroBody(p2_new_x, p2_new_y, p2_hero);
    
    if (!AABBOverlap(p1_new_body, p2_new_body)) {
        // No collision, allow movement
        p1_x = p1_new_x;
        p1_y = p1_new_y;
        p2_x = p2_new_x;
        p2_y = p2_new_y;
    } else {
        // There was a collision. 
        // Check whether moving in each direction would cause a collision.
        Hitbox p1_cur_body = GetHeroBody(p1_x, p1_y, p1_hero);
        Hitbox p2_cur_body = GetHeroBody(p2_x, p2_y, p2_hero);
        
        // Allow P1 to move independently
        Hitbox p1_only = GetHeroBody(p1_new_x, p1_new_y, p1_hero);
        if (!AABBOverlap(p1_only, p2_cur_body)) {
            p1_x = p1_new_x;
            p1_y = p1_new_y;
        }
        
        // Allow P2 to move independently
        Hitbox p2_only = GetHeroBody(p2_new_x, p2_new_y, p2_hero);
        if (!AABBOverlap(p1_cur_body, p2_only)) {
            p2_x = p2_new_x;
            p2_y = p2_new_y;
        }
    }

    // Attack + Collision + Effects
    uint32_t now = HAL_GetTick();

    // Skill activation (each player uses their own joystick button)
    if (current_input.btn3_pressed) {
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        triggerSkill(0);  // P1 uses first joystick button
    }
    if (current_input.btn2_pressed) {
        playTone(JOYSTICK_BEEP_FREQ, BEEP_MS);
        triggerSkill(1);  // P2 uses second joystick button
    }

    // Update darts
    doDartUpdate(p1_darts, MAX_DARTS);
    doDartUpdate(p2_darts, MAX_DARTS);

    // Dart collision detection
    for (int i = 0; i < MAX_DARTS; i++) {
        if (p1_darts[i].active) {
            Hitbox dart_hitbox = {p1_darts[i].x - 2, p1_darts[i].y - 2, 4, 4};
            Hitbox p2_body = GetHeroBody(p2_x, p2_y, p2_hero);
            if (AABBOverlap(dart_hitbox, p2_body)) {
                uint16_t damage = heroInfo[1].damage * 3;  // Assassin dart damage changed to 3x
                if (p2_hero == 0 && now < p2_shield_end) {
                    damage = damage / 2;  // 50% damage reduction
                }
                p2_health = (p2_health > damage) ? p2_health - damage : 0;
                p2_led_flash_end = now + 120;
                playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
                p1_darts[i].active = 0;
            }
        }
        if (p2_darts[i].active) {
            Hitbox dart_hitbox = {p2_darts[i].x - 2, p2_darts[i].y - 2, 4, 4};
            Hitbox p1_body = GetHeroBody(p1_x, p1_y, p1_hero);
            if (AABBOverlap(dart_hitbox, p1_body)) {
                uint16_t damage = heroInfo[1].damage * 3;  // Assassin dart damage changed to 3x
                if (p1_hero == 0 && now < p1_shield_end) {
                    damage = damage / 2;  // 50% damage reduction
                }
                p1_health = (p1_health > damage) ? p1_health - damage : 0;
                p1_led_flash_end = now + 120;
                playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
                p2_darts[i].active = 0;
            }
        }
    }

    // P1 attack (BTN8 - PC4)
    uint8_t p1_attack_pressed = current_input.btn8_pressed;
    uint32_t p1_cooldown = ATTACK_COOLDOWN;
    if (p1_hero == 2 && now < p1_beam_end) {
        p1_cooldown = 100;  // Faster shooting
    }
    if (p1_attack_pressed && (now - p1_last_swing) > p1_cooldown) {
        playTone(ATTACK_BEEP_FREQ, BEEP_MS);
        p1_last_swing = now;
        float p1_attack_angle = (d1.angle >= 0.0f) ? d1.angle : p1_facing_angle;
        uint8_t enhanced = (now < p1_beam_end);
        DrawAttackEffect(p1_x, p1_y, p1_hero, p1_attack_angle, enhanced);
        Hitbox p2_body_local = GetHeroBody(p2_x, p2_y, p2_hero);
        if (AttackHitsBody(p1_x, p1_y, p1_hero, p1_attack_angle, heroInfo[p1_hero].attack_reach, p2_body_local, enhanced)) {
            uint16_t damage = heroInfo[p1_hero].damage;
            // Assassin dart damage
            if (p1_hero == 1 && now < p1_effect_end) {
                damage = (damage * 3) / 2;  // +50%
            }
            // Mage skill increases damage
            if (p1_hero == 2 && now < p1_beam_end) {
                damage = (damage * 3) / 2;  // +50%
            }
            // Warrior damage reduction
            if (p2_hero == 0 && now < p2_shield_end) {
                damage = damage / 2;  // 50% reduction
            }
            p2_health = (p2_health > damage) ? p2_health - damage : 0;
            p2_led_flash_end = now + 120;
            playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
            p1_charge = (p1_charge + 2 > SKILL_MAX) ? SKILL_MAX : p1_charge + 2;
            p2_charge = (p2_charge + 4 > SKILL_MAX) ? SKILL_MAX : p2_charge + 4;
        }
    }

    // P2 attack (BTN9 - PC5)
    uint8_t p2_attack_pressed = current_input.btn9_pressed;
    uint32_t p2_cooldown = ATTACK_COOLDOWN;
    if (p2_hero == 2 && now < p2_beam_end) {
        p2_cooldown = 100;  // Faster shooting
    }
    if (p2_attack_pressed && (now - p2_last_swing) > p2_cooldown) {
        playTone(ATTACK_BEEP_FREQ, BEEP_MS);
        p2_last_swing = now;
        float p2_attack_angle = (d2.angle >= 0.0f) ? d2.angle : p2_facing_angle;
        uint8_t enhanced = (now < p2_beam_end);
        DrawAttackEffect(p2_x, p2_y, p2_hero, p2_attack_angle, enhanced);
        Hitbox p1_body_local = GetHeroBody(p1_x, p1_y, p1_hero);
        if (AttackHitsBody(p2_x, p2_y, p2_hero, p2_attack_angle, heroInfo[p2_hero].attack_reach, p1_body_local, enhanced)) {
            uint16_t damage = heroInfo[p2_hero].damage;
            // Assassin dart damage
            if (p2_hero == 1 && now < p2_effect_end) {
                damage = (damage * 3) / 2;  // +50%
            }
            // Mage skill increases damage
            if (p2_hero == 2 && now < p2_beam_end) {
                damage = (damage * 3) / 2;  // +50%
            }
            // Warrior damage reduction
            if (p1_hero == 0 && now < p1_shield_end) {
                damage = damage / 2;  // 50% reduction
            }
            p1_health = (p1_health > damage) ? p1_health - damage : 0;
            p1_led_flash_end = now + 120;
            playTone(HURT_BEEP_FREQ, HURT_BEEP_MS);
            p2_charge = (p2_charge + 2 > SKILL_MAX) ? SKILL_MAX : p2_charge + 2;
            p1_charge = (p1_charge + 4 > SKILL_MAX) ? SKILL_MAX : p1_charge + 4;
        }
    }

    // Win/lose determination
    if (p1_health == 0 || p2_health == 0) {
        uint32_t death_notes[] = {1000, 800, 600, 400, 200, 100};
        for (int i = 0; i < 6; i++) {
            playTone(death_notes[i], DEATH_NOTE_MS);
            HAL_Delay(DEATH_NOTE_MS + 30);  // pause between notes
        }
        
        PWM_SetDuty(&p1_led_cfg, 0);
        PWM_SetDuty(&pwm_cfg, 0);
        battle_live = 0;
        innerState = GAME_RESULT;
        return;
    }

    updateSkillLights();
    LCD_Refresh(&cfg0);

    // BTN6 backs out to the menu
    if (current_input.btn6_pressed) {
        playTone(JOYSTICK_BEEP_FREQ, EXIT_BEEP_MS);
        HAL_Delay(300);
        PWM_SetDuty(&p1_led_cfg, 0);
        PWM_SetDuty(&pwm_cfg, 0);
        battle_live = 0;
        realBattleNeedsReset = 1;  // Mark that next battle entry needs reset
        needResetNow = 1;  // Mark that next battle entry needs reset
        innerState = GAME_MENU;
    }
}


