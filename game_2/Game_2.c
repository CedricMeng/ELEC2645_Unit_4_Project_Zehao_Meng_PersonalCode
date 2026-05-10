#include "main.h"
#include "adc.h"
#include "stm32l4xx_hal_adc.h"
#include "usart.h"
#include "gpio.h"
#include "adc.h"
#include "rng.h"

#include "Joystick.h"
#include "LCD.h"
#include "Menu.h"
#include "InputHandler.h"
#include "PWM.h"
#include "Buzzer.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ===== 引用main.c中的全局变量 =====
extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
extern InputState current_input;
extern PWM_cfg_t pwm_cfg;
extern Buzzer_cfg_t buzzer_cfg;

// ===== 第二个摇杆配置（用于射击控制） =====
static Joystick_cfg_t joystick2_cfg;
static Joystick_t joystick2_data;

// ===== UTILITY FUNCTIONS =====
static uint16_t Random_U16(uint16_t max);

// ===== 颜色定义 =====
#define COLOR_BG            0
#define COLOR_PLAYER_BODY   4
#define COLOR_PLAYER_BELT   1
#define COLOR_PLAYER_HEAD   5
#define COLOR_PLAYER_BAND   1
#define COLOR_TARGET_YELLOW 6
#define COLOR_WIZARD_ROBE   2
#define COLOR_WIZARD_HAT    6
#define COLOR_MONSTER_VINE  3
#define COLOR_MONSTER_FLOWER 2
#define COLOR_MONSTER_EYE   6
#define COLOR_COVER         7  // 白色掩体
#define COLOR_NINJA_CLOTH   0
#define COLOR_NINJA_BELT    7
#define COLOR_NINJA_SHOE    6
#define COLOR_NINJA_EYE     7
#define COLOR_SAMURAI_ARMOR 2
#define COLOR_SAMURAI_HELM  2
#define COLOR_SAMURAI_ACCENT 6
#define COLOR_SAMURAI_BLADE 7
#define COLOR_ENEMY_PROJECTILE 4  // 蓝色敌人弹丸
#define COLOR_DRAGON_BODY   4
#define COLOR_DRAGON_WING   3
#define COLOR_DRAGON_EYE    6
#define COLOR_DRAGON_HORN   5

// LCD 参数
#define LCD_WIDTH 240
#define LCD_HEIGHT 240
#define PLAY_AREA_Y0 20

// 对象大小
#define PLAYER_WIDTH    12
#define PLAYER_HEIGHT   16
#define PLAYER_COLLISION_RADIUS 7

#define WIZARD_WIDTH    14
#define WIZARD_HEIGHT   18
#define WIZARD_COLLISION_RADIUS 9

#define MONSTER_WIDTH    16
#define MONSTER_HEIGHT   18
#define MONSTER_COLLISION_RADIUS 10

#define NINJA_WIDTH      12
#define NINJA_HEIGHT     18
#define NINJA_COLLISION_RADIUS 9

#define SAMURAI_WIDTH    16
#define SAMURAI_HEIGHT   18
#define SAMURAI_COLLISION_RADIUS 10

#define TARGET_RADIUS 4
#define TARGET_COUNT 10  // 最多支持10个目标槽
#define BOSS_COLLISION_RADIUS 28

#define MOVE_SPEED 3
#define MOVE_DELAY_MS 50
#define PROJECTILE_SPEED 5
#define PROJECTILE_RADIUS 2
#define MONSTER_CHASE_SPEED 1
#define SAMURAI_CHASE_SPEED 2
#define WIZARD_KEEP_DISTANCE_SPEED 1
#define NINJA_RANDOM_SPEED 3
#define ENEMY_MOVE_DELAY_MS 120

#define INITIAL_SPAWN_INTERVAL_MS 3000
#define MIN_SPAWN_INTERVAL_MS 1000
#define SPAWN_ACCELERATION_STEP_MS 250
#define SPAWN_ACCELERATION_PERIOD_MS 10000
#define ENEMY_MAX_LIMIT_START 6
#define ENEMY_MAX_LIMIT_CAP 10
#define ENEMY_MAX_INCREASE_PERIOD_MS 30000

#define PLAYER_MAX_HP 100
#define PLAYER_MAX_HP_CAP 200
#define PLAYER_BASE_SHOT_INTERVAL_MS 200
#define PLAYER_MAX_SPEED_MULTIPLIER 1.6f
#define PLAYER_MAX_SHOT_SPEED_MULTIPLIER 2.0f
#define PLAYER_SPEED_BOOST_STEP 0.04f
#define PLAYER_SHOT_SPEED_BOOST_STEP 0.05f

#define DROP_TYPE_NONE 0
#define DROP_TYPE_GREEN 1
#define DROP_TYPE_YELLOW 2
#define DROP_TYPE_RED 3
#define MAX_DROPS 4
#define DROP_RADIUS 5
#define DROP_SPAWN_CHANCE_PERCENT 15
#define DROP_SPAWN_TYPE_COUNT 3
#define DROP_GREEN_HP_UP 5

#define MONSTER_DAMAGE 5
#define SAMURAI_DAMAGE 15
#define UI_LINE_Y 35  // UI横线位置

#define WIZARD_SHOOT_INTERVAL_MS 1000  // 法师射击间隔1秒
#define NINJA_SHOOT_INTERVAL_MS 3000   // 忍者射击间隔3秒
#define WIZARD_PROJECTILE_SPEED 2      // 法师弹丸速度较慢
#define NINJA_PROJECTILE_SPEED 4       // 忍者弹丸速度
#define TRIANGLE_PROJECTILE_SIZE 3     // 三角形弹丸大小
#define BOSS_RANDOM_SHOOT_INTERVAL_MS 1500  // boss随机5方向射击间隔1.5秒
#define BOSS_CROSS_SHOOT_INTERVAL_MS 5000   // boss十字方向射击间隔5秒
#define BOSS_PROJECTILE_SPEED 3        // boss弹丸速度
#define COLOR_BOSS_PROJECTILE 1        // 红色弹丸（使用红色）

#define BOSS_TYPE_DRAGON 1
#define BOSS_TYPE_KING   2

#define KING_CHASE_SPEED 3
#define KING_CONTACT_DAMAGE 35
#define KING_CONTINUOUS_DAMAGE 15
#define KING_SHOOT_INTERVAL_MS 1000
#define KING_PROJECTILE_SPEED 1
#define KING_PROJECTILE_DAMAGE 3

// 弹丸结构体
typedef struct {
    uint16_t x, y;
    int16_t dx, dy;
    uint8_t active;
    uint8_t is_enemy;      // 1=敌人弹丸, 0=玩家弹丸
    uint8_t is_triangle;   // 1=三角形, 0=圆形
    uint8_t is_boss;       // 1=boss弹丸, 0=普通敌人弹丸
    uint8_t damage;        // 敌人弹丸伤害值
} Projectile_t;

#define MAX_PROJECTILES 8

// 函数原型
uint8_t Circles_Overlap(uint16_t x1, uint16_t y1, uint16_t r1,
                        uint16_t x2, uint16_t y2, uint16_t r2);

void Place_Target(uint8_t index,
                  uint16_t *target_x, uint16_t *target_y,
                  uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai,
                  uint16_t player_x, uint16_t player_y,
                  uint8_t *out_color, uint8_t *out_is_wizard, uint8_t *out_is_monster,
                  uint8_t *out_is_ninja, uint8_t *out_is_samurai);

void LCD_Draw_Player(uint16_t x, uint16_t y, uint8_t erase);
void LCD_Draw_Wizard(uint16_t x, uint16_t y, uint8_t erase);
void LCD_Draw_Monster(uint16_t x, uint16_t y, uint8_t erase);
void LCD_Draw_Ninja(uint16_t x, uint16_t y, uint8_t erase);
void LCD_Draw_Samurai(uint16_t x, uint16_t y, uint8_t erase);
void LCD_Draw_Triangle(uint16_t x, uint16_t y, uint8_t size, uint8_t color, uint8_t erase);

uint8_t Shoot_Projectile(uint16_t player_x, uint16_t player_y, uint8_t direction, Projectile_t *projs, uint32_t shoot_interval_ms);
void Shoot_Enemy_Projectile(uint16_t enemy_x, uint16_t enemy_y, uint16_t player_x, uint16_t player_y, uint8_t is_fan, Projectile_t *projs);
void Update_And_Draw_Projectiles(Projectile_t *projs, uint16_t *target_x, uint16_t *target_y,
                                 uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai,
                                 uint8_t *target_hp, uint16_t player_x, uint16_t player_y, uint16_t *player_hp, uint16_t *player_max_hp, uint16_t *score, uint8_t *active_enemies,
                                 uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type,
                                 uint8_t *boss_active, uint16_t boss_x, uint16_t boss_y, uint16_t *boss_hp, uint8_t *boss_defeated);

static void LCD_Draw_Drop(uint16_t x, uint16_t y, uint8_t type, uint8_t erase);
static void LCD_Draw_Dragon(uint16_t x, uint16_t y, uint8_t erase);
static void LCD_Draw_King(uint16_t x, uint16_t y, uint8_t erase);
static void Spawn_Drop(uint16_t x, uint16_t y, uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type);
static uint8_t Try_Collect_Drops(uint16_t player_x, uint16_t player_y, uint16_t *player_hp, uint16_t *player_max_hp,
                                 float *player_speed_mult, uint8_t *player_move_speed, float *player_shot_speed_mult,
                                 uint32_t *player_shot_interval_ms, uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type);

static void Game2_Display_Intro(void);
static void Game2_Wait_For_Start(void);
static void Game2_Show_Story_Chapter(const char *lines[]);
static void Game2_Wait_For_Story_Advance(void);
static void Game2_Redraw_Game_State(uint16_t player_x, uint16_t player_y,
                                    uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai,
                                    uint16_t *target_x, uint16_t *target_y, uint8_t *target_hp,
                                    Projectile_t *projectiles,
                                    uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type,
                                    uint8_t boss_active, uint8_t boss_type, uint16_t boss_x, uint16_t boss_y,
                                    uint16_t player_hp, uint16_t player_max_hp, uint16_t score);
static uint8_t Area_Overlaps_Player(uint16_t x, uint16_t y, uint8_t radius, uint16_t player_x, uint16_t player_y);
static void Move_Chasing_Enemies(uint16_t player_x, uint16_t player_y,
                                 uint16_t *target_x, uint16_t *target_y,
                                 uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai);

static void Game2_Display_Intro(void)
{
  const char *full_text[] = {
    "In the mountain forests on the kingdom's border, rumors of a ferocious dragon spread far and wide, plunging the people into panic and fear. The king summons brave warriors from all lands and bestows upon them a sacred mission: venture deep into the dragon's lair, slay the evil dragon, and restore peace to the realm.",
    "",
    "You are a righteous brave warrior. Armed with your ancestral bow and arrow, you set forth on an unknown journey.",
    "",
    "Use the two joysticks to control movement and firing respectively, and fend off enemies along the way. Enemies have a chance to drop items upon defeat: The green cross permanently increases your maximum health limit; The yellow cross boosts your movement speed; The red cross raises your firing rate."
  };
  const uint8_t text_count = sizeof(full_text) / sizeof(full_text[0]);
  const uint8_t font_scale = 1;
  const uint8_t line_height = 10;
  const int max_chars_per_line = 28; // 减少到28以确保不超出

  // 计算总行数（包括换行后的行）
  uint8_t total_lines = 0;
  for (uint8_t i = 0; i < text_count; i++) {
    const char *line = full_text[i];
    int line_len = strlen(line);
    if (line_len == 0) {
      total_lines += 1;
    } else {
      int lines_needed = (line_len + max_chars_per_line - 1) / max_chars_per_line; // 向上取整
      total_lines += lines_needed;
    }
  }

  int16_t base_y = LCD_HEIGHT - line_height;
  int16_t end_y = -((int16_t)total_lines * line_height);
  while (base_y > end_y) {
    LCD_Fill_Buffer(0);
    uint16_t y = base_y;
    for (uint8_t i = 0; i < text_count; i++) {
      const char *line = full_text[i];
      int line_len = strlen(line);
      
      if (line_len == 0) {
        y += line_height;
        continue;
      }
      
      // 处理换行
      int pos = 0;
      while (pos < line_len) {
        char display_line[50];
        int remaining = line_len - pos;
        int take = (remaining > max_chars_per_line) ? max_chars_per_line : remaining;
        
        // 尝试在单词边界分割
        if (take < remaining) {
          int split_pos = take;
          while (split_pos > 0 && line[pos + split_pos] != ' ') {
            split_pos--;
          }
          if (split_pos > 0) {
            take = split_pos;
          }
        }
        
        strncpy(display_line, line + pos, take);
        display_line[take] = '\0';
        
        // 去掉末尾空格
        int display_len = strlen(display_line);
        while (display_len > 0 && display_line[display_len - 1] == ' ') {
          display_line[--display_len] = '\0';
        }
        
        if (display_len > 0 && y >= 0 && y <= LCD_HEIGHT - line_height) {
          LCD_printString(display_line, 5, y, 1, font_scale);
        }
        y += line_height;
        pos += take;
        
        // 跳过空格
        while (pos < line_len && line[pos] == ' ') {
          pos++;
        }
      }
    }
    LCD_printString("Move both joysticks to start...", 5, LCD_HEIGHT - 20, 1, 1);
    LCD_Refresh(&cfg0);
    HAL_Delay(120);
    base_y -= 2;
  }
}

static void Game2_Wait_For_Start(void)
{
  while (1) {
    Input_Read();
    Joystick_Read(&joystick_cfg, &joystick_data);
    Joystick_Read(&joystick2_cfg, &joystick2_data);
    if (joystick_data.magnitude > 0.8f && joystick2_data.magnitude > 0.8f) {
      break;
    }
    HAL_Delay(50);
  }
}

static void Game2_Show_Story_Chapter(const char *lines[])
{
  LCD_Fill_Buffer(0);
  uint16_t y = 50;
  const int max_chars_per_line = 28;
  
  for (uint8_t i = 0; lines[i] != NULL; i++) {
    const char *line = lines[i];
    int line_len = strlen(line);
    
    // 如果这行是空行，直接显示并跳过
    if (line_len == 0) {
      y += 14;
      continue;
    }
    
    // 处理长行：分割为多个短行
    int pos = 0;
    while (pos < line_len) {
      char display_line[50];
      int remaining = line_len - pos;
      int take = (remaining > max_chars_per_line) ? max_chars_per_line : remaining;
      
      // 如果需要分割，尝试在单词边界处分割
      if (take < remaining && take < line_len) {
        // 从 take 位置向前找空格
        int split_pos = take;
        while (split_pos > 0 && line[pos + split_pos] != ' ') {
          split_pos--;
        }
        if (split_pos > 0) {
          take = split_pos;
          // 跳过空格
          while (pos + take < line_len && line[pos + take] == ' ') {
            take++;
          }
        }
      }
      
      strncpy(display_line, line + pos, take);
      display_line[take] = '\0';
      
      // 去掉末尾空格
      int display_len = strlen(display_line);
      while (display_len > 0 && display_line[display_len - 1] == ' ') {
        display_line[--display_len] = '\0';
      }
      
      if (display_len > 0) {
        LCD_printString(display_line, 5, y, 1, 1);
        y += 14;
      }
      
      pos += take;
      // 跳过多余的空格
      while (pos < line_len && line[pos] == ' ') {
        pos++;
      }
    }
  }
  
  if (y + 20 < LCD_HEIGHT) {
    LCD_printString("Move both joysticks to continue...", 5, LCD_HEIGHT - 20, 1, 1);
  }
  LCD_Refresh(&cfg0);
}

static void Game2_Wait_For_Story_Advance(void)
{
  // 等待先释放摇杆
  while (1) {
    Input_Read();
    Joystick_Read(&joystick_cfg, &joystick_data);
    Joystick_Read(&joystick2_cfg, &joystick2_data);
    if (joystick_data.magnitude < 0.3f && joystick2_data.magnitude < 0.3f) {
      break;
    }
    HAL_Delay(20);
  }
  // 等待双摇杆同时大幅移动
  while (1) {
    Input_Read();
    Joystick_Read(&joystick_cfg, &joystick_data);
    Joystick_Read(&joystick2_cfg, &joystick2_data);
    if (joystick_data.magnitude > 0.8f && joystick2_data.magnitude > 0.8f) {
      break;
    }
    HAL_Delay(20);
  }
}

static uint8_t Game2_Show_Choice(void)
{
  LCD_Fill_Buffer(0);
  // Draw two boxes
  LCD_Draw_Rect(20, 80, 100, 40, 1, 0); // Left box
  LCD_Draw_Rect(140, 80, 100, 40, 1, 0); // Right box
  LCD_printString("kill dragon", 30, 90, 1, 1);
  LCD_printString("for reward", 30, 100, 1, 1);
  LCD_printString("chat with", 150, 90, 1, 1);
  LCD_printString("dragon", 150, 100, 1, 1);
  LCD_Refresh(&cfg0);

  uint8_t choice = 0; // 0=none, 1=kill, 2=chat
  uint8_t selected = 0;

  // Wait for release
  while (1) {
    Input_Read();
    Joystick_Read(&joystick_cfg, &joystick_data);
    Joystick_Read(&joystick2_cfg, &joystick2_data);
    if (joystick_data.magnitude < 0.3f && joystick2_data.magnitude < 0.3f) {
      break;
    }
    HAL_Delay(20);
  }

  // Wait for choice
  while (!selected) {
    Input_Read();
    Joystick_Read(&joystick_cfg, &joystick_data);
    Joystick_Read(&joystick2_cfg, &joystick2_data);
    if (joystick_data.magnitude > 0.8f && choice == 0) {
      choice = 1; // kill
      // Erase other box
      LCD_Draw_Rect(140, 80, 100, 40, 0, 1);
      LCD_printString("chat with", 150, 90, 0, 1);
      LCD_printString("dragon", 150, 100, 0, 1);
      LCD_Refresh(&cfg0);
    } else if (joystick2_data.magnitude > 0.8f && choice == 0) {
      choice = 2; // chat
      // Erase other box
      LCD_Draw_Rect(20, 80, 100, 40, 0, 1);
      LCD_printString("kill dragon", 30, 90, 0, 1);
      LCD_printString("for reward", 30, 100, 0, 1);
      LCD_Refresh(&cfg0);
    }
    if (choice != 0 && joystick_data.magnitude > 0.8f && joystick2_data.magnitude > 0.8f) {
      selected = 1;
      LCD_Fill_Buffer(0);
      LCD_Refresh(&cfg0);
    }
    HAL_Delay(20);
  }
  return choice;
}

static void Game2_Redraw_Game_State(uint16_t player_x, uint16_t player_y,
                                    uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai,
                                    uint16_t *target_x, uint16_t *target_y, uint8_t *target_hp,
                                    Projectile_t *projectiles,
                                    uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type,
                                    uint8_t boss_active, uint8_t boss_type, uint16_t boss_x, uint16_t boss_y,
                                    uint16_t player_hp, uint16_t player_max_hp, uint16_t score)
{
  LCD_Fill_Buffer(0);
  LCD_Draw_Line(0, UI_LINE_Y, LCD_WIDTH - 1, UI_LINE_Y, 1);
  char score_str[20];
  char hp_str[16];
  sprintf(score_str, "Score: %d", score);
  sprintf(hp_str, "HP: %d", player_hp);
  LCD_printString(score_str, 10, 10, 1, 2);
  LCD_printString(hp_str, 160, 10, 1, 2);

  for (uint8_t i = 0; i < TARGET_COUNT; i++) {
    if (is_wizard[i]) LCD_Draw_Wizard(target_x[i], target_y[i], 0);
    else if (is_monster[i]) LCD_Draw_Monster(target_x[i], target_y[i], 0);
    else if (is_ninja[i]) LCD_Draw_Ninja(target_x[i], target_y[i], 0);
    else if (is_samurai[i]) LCD_Draw_Samurai(target_x[i], target_y[i], 0);
  }
  for (uint8_t i = 0; i < MAX_DROPS; i++) {
    if (drop_active[i]) {
      LCD_Draw_Drop(drop_x[i], drop_y[i], drop_type[i], 0);
    }
  }
  if (boss_active) {
    if (boss_type == BOSS_TYPE_KING) {
      LCD_Draw_King(boss_x, boss_y, 0);
    } else {
      LCD_Draw_Dragon(boss_x, boss_y, 0);
    }
  }
  for (int i = 0; i < MAX_PROJECTILES; i++) {
    if (!projectiles[i].active) continue;
    uint8_t proj_color = COLOR_ENEMY_PROJECTILE;
    if (projectiles[i].is_boss) {
      proj_color = COLOR_BOSS_PROJECTILE;  // 红色
    }
    if (projectiles[i].is_triangle) {
      LCD_Draw_Triangle(projectiles[i].x, projectiles[i].y, TRIANGLE_PROJECTILE_SIZE, proj_color, 0);
    } else if (projectiles[i].is_enemy) {
      LCD_Draw_Circle(projectiles[i].x, projectiles[i].y, PROJECTILE_RADIUS, proj_color, 0);
    } else {
      LCD_Draw_Circle(projectiles[i].x, projectiles[i].y, PROJECTILE_RADIUS, COLOR_TARGET_YELLOW, 0);
    }
  }
  LCD_Draw_Player(player_x, player_y, 0);
  LCD_Refresh(&cfg0);
}

// ===== Game2_Run 函数 =====
MenuState Game2_Run(void)
{
  // 开启背光
  gpio_write(cfg0.BL, 1);

  LCD_Fill_Buffer(0);
  
  // 绘制UI界面：显示分数和血量
  uint16_t score = 0;
  uint8_t story_stage = 0; // 0=none,1=first story shown,2=second story shown
  static const char *story1[] = {
    "(Before Departure · Palace Corridor)",
    NULL
  };
  static const char *story2[] = {
    "Brave: Maid, His Majesty ordered me to slay",
    "the dragon in Misty Mountains. Any tips?",
    NULL
  };
  static const char *story3[] = {
    "Maid: (nervous) Be careful, sir. Things in the",
    "mountains aren't as simple as they look.",
    "Don't trust everything easily.",
    "(hurries away, hiding a dragon-patterned shard)",
    NULL
  };
  static const char *story4[] = {
    "Brave: (frowning) Not as simple?...",
    NULL
  };
  static const char *story5[] = {
    "Old Woodcutter (Border Village)",
    NULL
  };
  static const char *story6[] = {
    "Brave: Elder, where's the dragon's lair?",
    "Does it really kill villagers?",
    NULL
  };
  static const char *story7[] = {
    "Old Woodcutter: Ten years ago, the mountains",
    "were quiet, and our village knew no fear.",
    "But lately, I've seen royal knight tracks near",
    "its lair. Be wary—rumors aren't always the",
    "whole truth.",
    NULL
  };
  static const char *story8[] = {
    "Brave: (startled) Royal knights? Why?",
    NULL
  };
  static const char *story9[] = {
    "Old Woodcutter: (silent, returning to",
    "chopping wood)",
    NULL
  };
  static const char *story10[] = {
    "Priestess (Near Dragon's Lair)",
    NULL
  };
  static const char *story11[] = {
    "Brave: Priestess, guide me to the dragon's lair.",
    "I must slay it.",
    NULL
  };
  static const char *story12[] = {
    "Priestess: The line between good and evil is blurred.",
    "This potion keeps you clear—choose wisely.",
    "The dragon may not be what you think.",
    NULL
  };
  static const char *story13[] = {
    "Brave: (holding the potion, confused) Is the dragon innocent?",
    NULL
  };
  static const char *story14[] = {
    "Priestess: Witness the truth yourself.",
    NULL
  };
  static const char *story15[] = {
    "The Dragon (Dragon's Lair)",
    "(The brave enters; the dragon does not attack.)",
    NULL
  };
  static const char *story16[] = {
    "Brave: Are you the evil dragon? Why not fight me?",
    NULL
  };
  static const char *story17[] = {
    "Dragon: I never harmed the innocent. The King has his",
    "reasons for sending you.",
    NULL
  };
  static const char *story18[] = {
    "The Dragon (Inside the Lair)",
    NULL
  };
  static const char *story19[] = {
    "Brave: (drinks the elixir, gripping his blade) All call",
    "you a fiend. Why not attack?",
    NULL
  };
  static const char *story20[] = {
    "Dragon: (rumbling, sorrowful) I am the Land's Guardian.",
    "The King wants my heart for eternal life—at the land's cost.",
    NULL
  };
  static const char *story21[] = {
    "Brave: (shocked) He used me… I'll return to the capital",
    "and end his tyranny.",
    NULL
  };
  static const char *story22[] = {
    "Return to the Capital & Final Confrontation",
    NULL
  };
  static const char *story23[] = {
    "*(The warrior strides into the throne hall, resolve firm.)*",
    NULL
  };
  static const char *story24[] = {
    "King: (feigning calm) Have you slain the dragon?",
    NULL
  };
  static const char *story25[] = {
    "Brave: I know the truth—you want its heart to live forever.",
    "The dragon is the Guardian.",
    NULL
  };
  static const char *story26[] = {
    "*(Dark energy coils around the King; he mutates into a",
    "horned monster.)*",
    NULL
  };
  static const char *story27[] = {
    "Monster King: (roaring) You'll die for this! Face your",
    "end, fool!",
    NULL
  };
  static const char *story28[] = {
    "*(Final battle begins)*",
    NULL
  };
  const char **story_segments[] = {story1, story2, story3, story4};
  const uint8_t story_count = sizeof(story_segments) / sizeof(story_segments[0]);
  const char **story2_segments[] = {story5, story6, story7, story8, story9};
  const uint8_t story2_count = sizeof(story2_segments) / sizeof(story2_segments[0]);
  const char **story3_segments[] = {story10, story11, story12, story13, story14};
  const uint8_t story3_count = sizeof(story3_segments) / sizeof(story3_segments[0]);
  const char **story4_segments[] = {story15, story16, story17};
  const uint8_t story4_count = sizeof(story4_segments) / sizeof(story4_segments[0]);
  static const char *story_end[] = {
    "You have won!",
    "The giant dragon has been defeated.",
    "The king rewards you with a great fortune",
    "of treasure. Years later, you hear that a severe",
    "drought has fallen upon the kingdom's border,",
    "along with ominous rumors of impending doom.",
    "You pay little heed to these tales and set off",
    "on a new journey once again.",
    "The game will now enter Endless Mode.",
    NULL
  };
  const char **story5_segments[] = {story18, story19, story20, story21, story22, story23, story24, story25, story26, story27, story28};
  const uint8_t story5_count = sizeof(story5_segments) / sizeof(story5_segments[0]);
  const char **story6_segments[] = {story_end};
  const uint8_t story6_count = sizeof(story6_segments) / sizeof(story6_segments[0]);

  LCD_printString("Score: 0", 10, 10, 1, 2);
  LCD_printString("HP: 100", 160, 10, 1, 2);
  LCD_Draw_Line(0, UI_LINE_Y, LCD_WIDTH-1, UI_LINE_Y, 1);  // 白色横线

  // 初始化第二个摇杆用于射击控制
  joystick2_cfg = (Joystick_cfg_t){
    .adc = &hadc1,
    .x_channel = ADC_CHANNEL_5,
    .y_channel = ADC_CHANNEL_6,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x = JOYSTICK_DEFAULT_CENTER_X,
    .center_y = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone = JOYSTICK_DEADZONE,
    .setup_done = 0
  };
  Joystick_Init(&joystick2_cfg);
  Joystick_Calibrate(&joystick2_cfg);

  Game2_Display_Intro();
  Game2_Wait_For_Start();
  LCD_Fill_Buffer(0);

  uint16_t player_x = LCD_WIDTH / 2;
  uint16_t player_y = LCD_HEIGHT / 2 + 40;
  uint16_t player_hp = PLAYER_MAX_HP;
  uint16_t player_max_hp = PLAYER_MAX_HP;
  float player_speed_mult = 1.0f;
  uint8_t player_move_speed = MOVE_SPEED;
  float player_shot_speed_mult = 1.0f;
  uint32_t player_shot_interval_ms = PLAYER_BASE_SHOT_INTERVAL_MS;

  // 持续伤害状态
  uint8_t damage_from_monster = 0;  // 是否正在受到触手怪伤害
  uint8_t damage_from_samurai = 0;  // 是否正在受到武士伤害
  uint32_t last_damage_tick = 0;    // 上次受到持续伤害的时间

  uint16_t target_x[TARGET_COUNT] = {0};
  uint16_t target_y[TARGET_COUNT] = {0};
  uint8_t is_wizard[TARGET_COUNT] = {0};
  uint8_t is_monster[TARGET_COUNT] = {0};
  uint8_t is_ninja[TARGET_COUNT] = {0};
  uint8_t is_samurai[TARGET_COUNT] = {0};
  uint8_t target_hp[TARGET_COUNT] = {0};
  uint8_t active_enemies = 0;  // 当前活跃敌人数量

  uint8_t drop_active[MAX_DROPS] = {0};
  uint16_t drop_x[MAX_DROPS] = {0};
  uint16_t drop_y[MAX_DROPS] = {0};
  uint8_t drop_type[MAX_DROPS] = {0};
  uint8_t boss_active = 0;
  uint8_t boss_type = 0;
  uint8_t boss_defeated = 0;
  uint16_t boss_x = LCD_WIDTH / 2;
  uint16_t boss_y = (UI_LINE_Y + LCD_HEIGHT) / 2;
  uint16_t boss_hp = 0;
  uint32_t last_boss_random_shoot_tick = 0;
  uint32_t last_boss_cross_shoot_tick = 0;
  uint32_t last_king_shoot_tick = 0;
  uint8_t king_contact_damage_dealt = 0;
  uint32_t last_king_damage_tick = 0;
  uint32_t last_king_move_tick = 0;

  // 初始化敌人：随机放置 3 到 5 个敌人
  uint8_t initial_enemies = 3 + Random_U16(3);  // 3-5
  for (uint8_t i = 0; i < initial_enemies; i++) {
    uint8_t dummy = 0;
    Place_Target(i, target_x, target_y, is_wizard, is_monster, is_ninja, is_samurai, player_x, player_y,
                 &dummy, &is_wizard[i], &is_monster[i], &is_ninja[i], &is_samurai[i]);
    target_hp[i] = is_wizard[i] ? 2 : is_monster[i] ? 3 : is_ninja[i] ? 2 : is_samurai[i] ? 5 : 1;
    if (is_wizard[i] || is_monster[i] || is_ninja[i] || is_samurai[i]) active_enemies++;
  }

  LCD_Draw_Player(player_x, player_y, 0);
  LCD_Refresh(&cfg0);

  uint32_t last_move_tick = HAL_GetTick();
  uint32_t last_enemy_move_tick = HAL_GetTick();
  uint32_t last_spawn_tick = HAL_GetTick();  // 敌人刷新定时器
  uint32_t last_spawn_accel_tick = HAL_GetTick();  // 刷新速度加速定时器
  uint32_t last_enemy_limit_tick = HAL_GetTick();  // 敌方单位最大上限增长定时器
  uint32_t last_wizard_shoot_tick = HAL_GetTick();
  uint32_t last_ninja_shoot_tick = HAL_GetTick();
  uint32_t spawn_interval_ms = INITIAL_SPAWN_INTERVAL_MS;
  uint8_t enemy_max_limit = ENEMY_MAX_LIMIT_START;
  uint16_t prev_x = player_x, prev_y = player_y;

  Projectile_t projectiles[MAX_PROJECTILES] = {0};
  
  // LED闪烁控制
  static uint32_t muzzle_led_off_tick = 0;

  while (1) {
    // 检查是否按下BT3退出游戏
    Input_Read();
    if (current_input.btn3_pressed) {
      return MENU_STATE_HOME;
    }

    // 检查血量是否为0，游戏结束
    if (player_hp <= 0) {
      // 清除屏幕中所有显示
      LCD_Fill_Buffer(0);
      // 在屏幕中央显示GAME OVER，字体较大
      LCD_printString("GAME OVER", 70, 120, 1, 3);
      LCD_Refresh(&cfg0);
      // 短暂延迟让玩家看到
      HAL_Delay(2000);
      return MENU_STATE_HOME;
    }

    // 保持背光开启
    gpio_write(cfg0.BL, 1);

    // 读取第一个摇杆（移动控制）
    Joystick_Read(&joystick_cfg, &joystick_data);
    
    // 读取第二个摇杆（射击控制）
    Joystick_Read(&joystick2_cfg, &joystick2_data);

    uint32_t now = HAL_GetTick();
    if ((now - last_move_tick) >= MOVE_DELAY_MS && joystick_data.direction != CENTRE) {
      int16_t dx = 0, dy = 0;
      switch (joystick_data.direction) {
        case N:  dy = -player_move_speed; break;
        case NE: dy = -player_move_speed; dx = player_move_speed; break;
        case E:  dx = player_move_speed; break;
        case SE: dy = player_move_speed; dx = player_move_speed; break;
        case S:  dy = player_move_speed; break;
        case SW: dy = player_move_speed; dx = -player_move_speed; break;
        case W:  dx = -player_move_speed; break;
        case NW: dy = -player_move_speed; dx = -player_move_speed; break;
        case CENTRE: break;
      }
      if (dx || dy) {
        int32_t nx = (int32_t)player_x + dx;
        int32_t ny = (int32_t)player_y + dy;
        if (nx < PLAYER_WIDTH/2) nx = PLAYER_WIDTH/2;
        if (nx > LCD_WIDTH - PLAYER_WIDTH/2 - 1) nx = LCD_WIDTH - PLAYER_WIDTH/2 - 1;
        if (ny < UI_LINE_Y + PLAYER_HEIGHT/2) ny = UI_LINE_Y + PLAYER_HEIGHT/2;
        if (ny > LCD_HEIGHT - PLAYER_HEIGHT/2 - 1) ny = LCD_HEIGHT - PLAYER_HEIGHT/2 - 1;

        // 检查与敌方单位的碰撞
        uint8_t can_move = 1;
        for (uint8_t i = 0; i < TARGET_COUNT; i++) {
          if (is_wizard[i] || is_monster[i] || is_ninja[i] || is_samurai[i]) {
            uint8_t r = is_wizard[i] ? WIZARD_COLLISION_RADIUS :
                        is_monster[i] ? MONSTER_COLLISION_RADIUS :
                        is_ninja[i] ? NINJA_COLLISION_RADIUS :
                        SAMURAI_COLLISION_RADIUS;
            if (Circles_Overlap(nx, ny, PLAYER_COLLISION_RADIUS, target_x[i], target_y[i], r)) {
              can_move = 0;
              // 设置持续伤害状态
              if (is_monster[i]) {
                damage_from_monster = 1;
              } else if (is_samurai[i]) {
                damage_from_samurai = 1;
              }
              break;
            }
          }
        }

        if (can_move) {
          player_x = (uint16_t)nx;
          player_y = (uint16_t)ny;
        }
      }
      last_move_tick = now;
    }

    // 检查持续伤害
    if (damage_from_monster || damage_from_samurai) {
      if ((now - last_damage_tick) >= 1000) {  // 每隔1秒
        if (damage_from_monster) {
          if (player_hp >= 5) player_hp -= 5;
          else player_hp = 0;
        } else if (damage_from_samurai) {
          if (player_hp >= 15) player_hp -= 15;
          else player_hp = 0;
        }
        last_damage_tick = now;
      }
    }

    // 重置持续伤害状态（需要每帧检查是否还在碰撞）
    damage_from_monster = 0;
    damage_from_samurai = 0;

    // 检查当前是否还在与敌人碰撞（用于持续伤害）
    for (uint8_t i = 0; i < TARGET_COUNT; i++) {
      if (is_wizard[i] || is_monster[i] || is_ninja[i] || is_samurai[i]) {
        uint8_t r = is_wizard[i] ? WIZARD_COLLISION_RADIUS :
                    is_monster[i] ? MONSTER_COLLISION_RADIUS :
                    is_ninja[i] ? NINJA_COLLISION_RADIUS :
                    SAMURAI_COLLISION_RADIUS;
        if (Circles_Overlap(player_x, player_y, PLAYER_COLLISION_RADIUS, target_x[i], target_y[i], r)) {
          if (is_monster[i]) {
            damage_from_monster = 1;
          } else if (is_samurai[i]) {
            damage_from_samurai = 1;
          }
        }
      }
    }

    if ((now - last_enemy_move_tick) >= ENEMY_MOVE_DELAY_MS) {
      Move_Chasing_Enemies(player_x, player_y, target_x, target_y,
                           is_wizard, is_monster, is_ninja, is_samurai);
      last_enemy_move_tick = now;
    }

    // 敌人射击逻辑
    if ((now - last_wizard_shoot_tick) >= WIZARD_SHOOT_INTERVAL_MS) {
      for (uint8_t i = 0; i < TARGET_COUNT; i++) {
        if (is_wizard[i]) {
          Shoot_Enemy_Projectile(target_x[i], target_y[i], player_x, player_y, 0, projectiles);  // 圆形弹丸
        }
      }
      last_wizard_shoot_tick = now;
    }

    if ((now - last_ninja_shoot_tick) >= NINJA_SHOOT_INTERVAL_MS) {
      for (uint8_t i = 0; i < TARGET_COUNT; i++) {
        if (is_ninja[i]) {
          Shoot_Enemy_Projectile(target_x[i], target_y[i], player_x, player_y, 1, projectiles);  // 三角形扇形弹丸
        }
      }
      last_ninja_shoot_tick = now;
    }

    // 使用第二个摇杆控制射击
    if (joystick2_data.magnitude > 0.3f) {
      uint8_t shot_fired = Shoot_Projectile(player_x, player_y, joystick2_data.direction, projectiles, player_shot_interval_ms);
      
      // LED闪烁：当射击时点亮LED，设置定时器
      if (shot_fired) {
        uint32_t flash_duty = 50u;  // 默认闪烁亮度
        uint32_t flash_ms = 30u;    // 默认闪烁时间
        PWM_SetDuty(&pwm_cfg, flash_duty);
        muzzle_led_off_tick = now + flash_ms;
      }
    }

    // Boss射击逻辑
    if (boss_active && boss_type == BOSS_TYPE_DRAGON) {
      // 每隔1.5秒发射5枚随机方向伤害为10的圆形弹丸
      if ((now - last_boss_random_shoot_tick) >= BOSS_RANDOM_SHOOT_INTERVAL_MS) {
        for (uint8_t i = 0; i < 5; i++) {
          uint8_t random_dir = Random_U16(8);  // 0-7 方向
          int16_t dx = 0, dy = 0;
          switch (random_dir) {
            case N:  dy = -BOSS_PROJECTILE_SPEED; break;
            case S:  dy =  BOSS_PROJECTILE_SPEED; break;
            case E:  dx =  BOSS_PROJECTILE_SPEED; break;
            case W:  dx = -BOSS_PROJECTILE_SPEED; break;
            case NE: dy = -BOSS_PROJECTILE_SPEED; dx = BOSS_PROJECTILE_SPEED; break;
            case NW: dy = -BOSS_PROJECTILE_SPEED; dx = -BOSS_PROJECTILE_SPEED; break;
            case SE: dy =  BOSS_PROJECTILE_SPEED; dx = BOSS_PROJECTILE_SPEED; break;
            case SW: dy =  BOSS_PROJECTILE_SPEED; dx = -BOSS_PROJECTILE_SPEED; break;
          }
          for (int j = 0; j < MAX_PROJECTILES; j++) {
            if (!projectiles[j].active) {
              projectiles[j].x = boss_x;
              projectiles[j].y = boss_y;
              projectiles[j].dx = dx;
              projectiles[j].dy = dy;
              projectiles[j].active = 1;
              projectiles[j].is_enemy = 1;
              projectiles[j].is_triangle = 0;
              projectiles[j].is_boss = 1;
              projectiles[j].damage = 10;
              break;
            }
          }
        }
        last_boss_random_shoot_tick = now;
      }

      // 每隔5秒向十字方向（上下左右）发射连续三枚伤害为5的三角形弹丸
      if ((now - last_boss_cross_shoot_tick) >= BOSS_CROSS_SHOOT_INTERVAL_MS) {
        uint8_t cross_dirs[] = {N, S, E, W};  // 上下左右
        for (uint8_t dir = 0; dir < 4; dir++) {
          for (uint8_t count = 0; count < 3; count++) {
            int16_t dx = 0, dy = 0;
            switch (cross_dirs[dir]) {
              case N:  dy = -BOSS_PROJECTILE_SPEED; break;
              case S:  dy =  BOSS_PROJECTILE_SPEED; break;
              case E:  dx =  BOSS_PROJECTILE_SPEED; break;
              case W:  dx = -BOSS_PROJECTILE_SPEED; break;
            }
            for (int j = 0; j < MAX_PROJECTILES; j++) {
              if (!projectiles[j].active) {
                projectiles[j].x = boss_x;
                projectiles[j].y = boss_y;
                projectiles[j].dx = dx;
                projectiles[j].dy = dy;
                projectiles[j].active = 1;
                projectiles[j].is_enemy = 1;
                projectiles[j].is_triangle = 1;
                projectiles[j].is_boss = 1;
                projectiles[j].damage = 5;
                break;
              }
            }
          }
        }
        last_boss_cross_shoot_tick = now;
      }
    }

    // 国王逻辑
    if (boss_active && boss_type == BOSS_TYPE_KING) {
      // 国王每秒发射三颗弹丸（像忍者一样）
      if ((now - last_king_shoot_tick) >= KING_SHOOT_INTERVAL_MS) {
        int16_t dx = (int16_t)player_x - (int16_t)boss_x;
        int16_t dy = (int16_t)player_y - (int16_t)boss_y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > 0.1f) {
          float ndx = dx / dist;
          float ndy = dy / dist;
          
          // 发射三颗弹丸：左、中、右
          float angles[3] = {-0.3f, 0.0f, 0.3f};
          for (int i = 0; i < 3; i++) {
            float cos_a = cosf(angles[i]);
            float sin_a = sinf(angles[i]);
            float rot_dx = ndx * cos_a - ndy * sin_a;
            float rot_dy = ndx * sin_a + ndy * cos_a;
            
            for (int j = 0; j < MAX_PROJECTILES; j++) {
              if (!projectiles[j].active) {
                projectiles[j].x = boss_x;
                projectiles[j].y = boss_y;
                projectiles[j].dx = (int16_t)roundf(rot_dx * KING_PROJECTILE_SPEED);
                projectiles[j].dy = (int16_t)roundf(rot_dy * KING_PROJECTILE_SPEED);
                if (projectiles[j].dx == 0 && projectiles[j].dy == 0) {
                  projectiles[j].dx = (rot_dx > 0) ? 1 : (rot_dx < 0) ? -1 : 0;
                  projectiles[j].dy = (rot_dy > 0) ? 1 : (rot_dy < 0) ? -1 : 0;
                }
                projectiles[j].active = 1;
                projectiles[j].is_enemy = 1;
                projectiles[j].is_triangle = 1;
                projectiles[j].is_boss = 1;
                projectiles[j].damage = KING_PROJECTILE_DAMAGE;
                break;
              }
            }
          }
        }
        last_king_shoot_tick = now;
      }
    }

    // 每10秒加快刷新速度
    if ((now - last_spawn_accel_tick) >= SPAWN_ACCELERATION_PERIOD_MS) {
      if (spawn_interval_ms > MIN_SPAWN_INTERVAL_MS) {
        spawn_interval_ms -= (spawn_interval_ms - MIN_SPAWN_INTERVAL_MS > SPAWN_ACCELERATION_STEP_MS)
                             ? SPAWN_ACCELERATION_STEP_MS
                             : (spawn_interval_ms - MIN_SPAWN_INTERVAL_MS);
      }
      last_spawn_accel_tick = now;
    }

    // 每30秒提升敌方单位上限，直到上限为10个
    if (!boss_active && (now - last_enemy_limit_tick) >= ENEMY_MAX_INCREASE_PERIOD_MS && enemy_max_limit < ENEMY_MAX_LIMIT_CAP) {
      enemy_max_limit++;
      last_enemy_limit_tick = now;
    }

    // 敌人刷新逻辑：按当前上限和当前刷新间隔生成敌人
    if ((now - last_spawn_tick) >= spawn_interval_ms && active_enemies < enemy_max_limit) {
      uint8_t spawn_attempts = enemy_max_limit - active_enemies;
      for (uint8_t attempt = 0; attempt < spawn_attempts; attempt++) {
        uint8_t placed = 0;
        for (uint8_t i = 0; i < TARGET_COUNT; i++) {
          if (!is_wizard[i] && !is_monster[i] && !is_ninja[i] && !is_samurai[i]) {
            uint8_t dummy = 0;
            Place_Target(i, target_x, target_y, is_wizard, is_monster, is_ninja, is_samurai, player_x, player_y,
                         &dummy, &is_wizard[i], &is_monster[i], &is_ninja[i], &is_samurai[i]);
            if (is_wizard[i] || is_monster[i] || is_ninja[i] || is_samurai[i]) {
              target_hp[i] = is_wizard[i] ? 2 : is_monster[i] ? 3 : is_ninja[i] ? 2 : is_samurai[i] ? 5 : 1;
              active_enemies++;
              placed = 1;
            }
            break;
          }
        }
        if (!placed) break; 
      }
      last_spawn_tick = now;
    }

    Update_And_Draw_Projectiles(projectiles, target_x, target_y, is_wizard, is_monster, is_ninja, is_samurai, target_hp, player_x, player_y, &player_hp, &player_max_hp, &score, &active_enemies,
                                 drop_active, drop_x, drop_y, drop_type,
                                 &boss_active, boss_x, boss_y, &boss_hp, &boss_defeated);

    Try_Collect_Drops(player_x, player_y, &player_hp, &player_max_hp,
                      &player_speed_mult, &player_move_speed, &player_shot_speed_mult,
                      &player_shot_interval_ms, drop_active, drop_x, drop_y, drop_type);

    if (story_stage == 0 && score >= 100) {
      for (uint8_t s = 0; s < story_count; s++) {
        Game2_Show_Story_Chapter(story_segments[s]);
        Game2_Wait_For_Story_Advance();
      }
      LCD_Fill_Buffer(0);
      LCD_Refresh(&cfg0);
      Game2_Wait_For_Story_Advance();
      Game2_Redraw_Game_State(player_x, player_y, is_wizard, is_monster, is_ninja, is_samurai,
                              target_x, target_y, target_hp,
                              projectiles,
                              drop_active, drop_x, drop_y, drop_type,
                              boss_active, boss_type, boss_x, boss_y,
                              player_hp, player_max_hp, score);
      uint32_t resume_tick = HAL_GetTick();
      last_enemy_move_tick = resume_tick;
      last_wizard_shoot_tick = resume_tick;
      last_ninja_shoot_tick = resume_tick;
      last_spawn_tick = resume_tick;
      last_spawn_accel_tick = resume_tick;
      last_enemy_limit_tick = resume_tick;
      prev_x = player_x;
      prev_y = player_y;
      story_stage = 1;
      continue;
    }
    if (story_stage == 1 && score >= 160) {
      for (uint8_t s = 0; s < story2_count; s++) {
        Game2_Show_Story_Chapter(story2_segments[s]);
        Game2_Wait_For_Story_Advance();
      }
      LCD_Fill_Buffer(0);
      LCD_Refresh(&cfg0);
      Game2_Wait_For_Story_Advance();
      Game2_Redraw_Game_State(player_x, player_y, is_wizard, is_monster, is_ninja, is_samurai,
                              target_x, target_y, target_hp,
                              projectiles,
                              drop_active, drop_x, drop_y, drop_type,
                              boss_active, boss_type, boss_x, boss_y,
                              player_hp, player_max_hp, score);
      uint32_t resume_tick = HAL_GetTick();
      last_enemy_move_tick = resume_tick;
      last_wizard_shoot_tick = resume_tick;
      last_ninja_shoot_tick = resume_tick;
      last_spawn_tick = resume_tick;
      last_spawn_accel_tick = resume_tick;
      last_enemy_limit_tick = resume_tick;
      prev_x = player_x;
      prev_y = player_y;
      story_stage = 2;
      continue;
    }
    if (story_stage == 2 && score >= 240) {
      for (uint8_t s = 0; s < story3_count; s++) {
        Game2_Show_Story_Chapter(story3_segments[s]);
        Game2_Wait_For_Story_Advance();
      }
      LCD_Fill_Buffer(0);
      LCD_Refresh(&cfg0);
      Game2_Wait_For_Story_Advance();
      Game2_Redraw_Game_State(player_x, player_y, is_wizard, is_monster, is_ninja, is_samurai,
                              target_x, target_y, target_hp,
                              projectiles,
                              drop_active, drop_x, drop_y, drop_type,
                              boss_active, boss_type, boss_x, boss_y,
                              player_hp, player_max_hp, score);
      uint32_t resume_tick = HAL_GetTick();
      last_enemy_move_tick = resume_tick;
      last_wizard_shoot_tick = resume_tick;
      last_ninja_shoot_tick = resume_tick;
      last_spawn_tick = resume_tick;
      last_spawn_accel_tick = resume_tick;
      last_enemy_limit_tick = resume_tick;
      prev_x = player_x;
      prev_y = player_y;
      story_stage = 3;
      continue;
    }
    if (story_stage == 3 && score >= 300) {
      // Show initial dragon encounter
      for (uint8_t s = 0; s < story4_count; s++) {
        Game2_Show_Story_Chapter(story4_segments[s]);
        Game2_Wait_For_Story_Advance();
      }
      LCD_Fill_Buffer(0);
      LCD_Refresh(&cfg0);
      Game2_Wait_For_Story_Advance();
      // Show choice
      uint8_t choice = Game2_Show_Choice();
      if (choice == 1) { // kill dragon for reward
        // Clear all existing enemies, projectiles, drops and stop further spawning
        for (uint8_t j = 0; j < TARGET_COUNT; j++) {
          is_wizard[j] = 0;
          is_monster[j] = 0;
          is_ninja[j] = 0;
          is_samurai[j] = 0;
          target_hp[j] = 0;
          target_x[j] = 0;
          target_y[j] = 0;
        }
        for (int j = 0; j < MAX_PROJECTILES; j++) {
          projectiles[j].active = 0;
        }
        for (uint8_t j = 0; j < MAX_DROPS; j++) {
          drop_active[j] = 0;
          drop_x[j] = 0;
          drop_y[j] = 0;
          drop_type[j] = 0;
        }
        active_enemies = 0;
        enemy_max_limit = 0;
        boss_active = 1;
        boss_type = BOSS_TYPE_DRAGON;
        boss_hp = 100;
        boss_defeated = 0;
        boss_x = LCD_WIDTH / 2;
        boss_y = (UI_LINE_Y + LCD_HEIGHT) / 2;
      } else if (choice == 2) { // chat
        for (uint8_t j = 0; j < TARGET_COUNT; j++) {
          is_wizard[j] = 0;
          is_monster[j] = 0;
          is_ninja[j] = 0;
          is_samurai[j] = 0;
          target_hp[j] = 0;
          target_x[j] = 0;
          target_y[j] = 0;
        }
        for (int j = 0; j < MAX_PROJECTILES; j++) {
          projectiles[j].active = 0;
        }
        for (uint8_t j = 0; j < MAX_DROPS; j++) {
          drop_active[j] = 0;
          drop_x[j] = 0;
          drop_y[j] = 0;
          drop_type[j] = 0;
        }
        active_enemies = 0;
        enemy_max_limit = 0;
        boss_active = 1;
        boss_type = BOSS_TYPE_KING;
        boss_hp = 100;
        boss_defeated = 0;
        boss_x = LCD_WIDTH / 2;
        boss_y = (UI_LINE_Y + LCD_HEIGHT) / 2;

        for (uint8_t s = 0; s < story5_count; s++) {
          Game2_Show_Story_Chapter(story5_segments[s]);
          Game2_Wait_For_Story_Advance();
        }
        LCD_Fill_Buffer(0);
        LCD_Refresh(&cfg0);
        Game2_Wait_For_Story_Advance();
      }
      Game2_Redraw_Game_State(player_x, player_y, is_wizard, is_monster, is_ninja, is_samurai,
                              target_x, target_y, target_hp,
                              projectiles,
                              drop_active, drop_x, drop_y, drop_type,
                              boss_active, boss_type, boss_x, boss_y,
                              player_hp, player_max_hp, score);
      uint32_t resume_tick = HAL_GetTick();
      last_enemy_move_tick = resume_tick;
      last_wizard_shoot_tick = resume_tick;
      last_ninja_shoot_tick = resume_tick;
      last_spawn_tick = resume_tick;
      last_spawn_accel_tick = resume_tick;
      last_enemy_limit_tick = resume_tick;
      prev_x = player_x;
      prev_y = player_y;
      
      // 如果boss是国王，初始化国王的定时器
      if (boss_type == BOSS_TYPE_KING) {
        last_king_move_tick = 0;
        last_king_shoot_tick = 0;
        last_king_damage_tick = 0;
        king_contact_damage_dealt = 0;
      }
      
      story_stage = 4;
      continue;
    }

    if (story_stage == 4 && boss_defeated) {
      for (uint8_t s = 0; s < story6_count; s++) {
        Game2_Show_Story_Chapter(story6_segments[s]);
        Game2_Wait_For_Story_Advance();
      }
      LCD_Fill_Buffer(0);
      LCD_Refresh(&cfg0);
      Game2_Wait_For_Story_Advance();
      Game2_Redraw_Game_State(player_x, player_y, is_wizard, is_monster, is_ninja, is_samurai,
                              target_x, target_y, target_hp,
                              projectiles,
                              drop_active, drop_x, drop_y, drop_type,
                              boss_active, boss_type, boss_x, boss_y,
                              player_hp, player_max_hp, score);
      uint32_t resume_tick = HAL_GetTick();
      last_enemy_move_tick = resume_tick;
      last_wizard_shoot_tick = resume_tick;
      last_ninja_shoot_tick = resume_tick;
      last_spawn_tick = resume_tick;
      last_spawn_accel_tick = resume_tick;
      last_enemy_limit_tick = resume_tick;
      prev_x = player_x;
      prev_y = player_y;
      enemy_max_limit = ENEMY_MAX_LIMIT_START;
      spawn_interval_ms = INITIAL_SPAWN_INTERVAL_MS;
      boss_defeated = 0;
      story_stage = 5;
      continue;
    }

    for (uint8_t k = 0; k < MAX_DROPS; k++) {
      if (drop_active[k]) {
        LCD_Draw_Drop(drop_x[k], drop_y[k], drop_type[k], 0);
      }
    }

    if (player_x != prev_x || player_y != prev_y) {
      LCD_Draw_Player(prev_x, prev_y, 1);
      LCD_Draw_Player(player_x, player_y, 0);
      prev_x = player_x;
      prev_y = player_y;
    }

    // 更新分数和血量显示
    // 先擦除旧的顶部显示区域
    LCD_Draw_Rect(0, 10, 150, 16, COLOR_BG, 1);  // 擦除左上角分数区域
    LCD_Draw_Rect(160, 10, 80, 16, COLOR_BG, 1);  // 擦除右上角血量区域
    char score_str[20];
    char hp_str[16];
    sprintf(score_str, "Score: %d", score);
    sprintf(hp_str, "HP: %d", player_hp);
    LCD_printString(score_str, 10, 10, 1, 2);
    LCD_printString(hp_str, 160, 10, 1, 2);

    // LED闪烁处理：检查是否需要关闭LED
    if (muzzle_led_off_tick != 0u && now >= muzzle_led_off_tick) {
      PWM_SetDuty(&pwm_cfg, 0);
      muzzle_led_off_tick = 0u;
    }

    LCD_Refresh(&cfg0);
  }
  return MENU_STATE_HOME;
}

// ===== 发射弹丸 =====
uint8_t Shoot_Projectile(uint16_t player_x, uint16_t player_y, uint8_t direction, Projectile_t *projs, uint32_t shoot_interval_ms) {
  static uint32_t last_shot = 0;
  if (HAL_GetTick() - last_shot < shoot_interval_ms) return 0;  // 优化：减少射击间隔
  last_shot = HAL_GetTick();

  int16_t dx = 0, dy = 0;
  switch (direction) {
    case N:  dy = -PROJECTILE_SPEED; break;
    case S:  dy =  PROJECTILE_SPEED; break;
    case E:  dx =  PROJECTILE_SPEED; break;
    case W:  dx = -PROJECTILE_SPEED; break;
    case NE: dy = -PROJECTILE_SPEED; dx = PROJECTILE_SPEED; break;
    case NW: dy = -PROJECTILE_SPEED; dx = -PROJECTILE_SPEED; break;
    case SE: dy =  PROJECTILE_SPEED; dx = PROJECTILE_SPEED; break;
    case SW: dy =  PROJECTILE_SPEED; dx = -PROJECTILE_SPEED; break;
  }

  for (int i = 0; i < MAX_PROJECTILES; i++) {
    if (!projs[i].active) {
      projs[i].x = player_x;
      projs[i].y = player_y;
      projs[i].dx = dx;
      projs[i].dy = dy;
      projs[i].active = 1;
      projs[i].is_enemy = 0;  // 玩家弹丸
      projs[i].is_triangle = 0;  // 圆形
      projs[i].is_boss = 0;
      projs[i].damage = 0;
      // 射击蜂鸣器短鸣
      buzzer_tone(&buzzer_cfg, 400u, 45u);
      HAL_Delay(25u);
      buzzer_off(&buzzer_cfg);
      return 1;  // 成功射击
    }
  }
  return 0;  // 没有可用的弹丸槽
}

// ===== 发射敌人弹丸 =====
void Shoot_Enemy_Projectile(uint16_t enemy_x, uint16_t enemy_y, uint16_t player_x, uint16_t player_y, uint8_t is_fan, Projectile_t *projs) {
  // 计算方向向量
  int16_t dx = (int16_t)player_x - (int16_t)enemy_x;
  int16_t dy = (int16_t)player_y - (int16_t)enemy_y;

  // 归一化方向向量
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist == 0) return;  // 避免除零

  float ndx = dx / dist;
  float ndy = dy / dist;

  if (is_fan) {
    // 忍者：扇形三枚三角形弹丸
    float angles[3] = {-0.3f, 0.0f, 0.3f};  // 左右各15度
    float spawn_dist = NINJA_COLLISION_RADIUS + TRIANGLE_PROJECTILE_SIZE + 1.0f;
    for (int i = 0; i < 3; i++) {
      float cos_a = cosf(angles[i]);
      float sin_a = sinf(angles[i]);
      float rot_dx = ndx * cos_a - ndy * sin_a;
      float rot_dy = ndx * sin_a + ndy * cos_a;
      float start_x = enemy_x + rot_dx * spawn_dist;
      float start_y = enemy_y + rot_dy * spawn_dist;

      for (int j = 0; j < MAX_PROJECTILES; j++) {
        if (!projs[j].active) {
          projs[j].x = (uint16_t)start_x;
          projs[j].y = (uint16_t)start_y;
          projs[j].dx = (int16_t)roundf(rot_dx * NINJA_PROJECTILE_SPEED);
          projs[j].dy = (int16_t)roundf(rot_dy * NINJA_PROJECTILE_SPEED);
          if (projs[j].dx == 0 && projs[j].dy == 0) {
            projs[j].dx = (rot_dx > 0) ? 1 : (rot_dx < 0) ? -1 : 0;
            projs[j].dy = (rot_dy > 0) ? 1 : (rot_dy < 0) ? -1 : 0;
          } else if (projs[j].dx == 0) {
            projs[j].dx = (rot_dx > 0) ? 1 : (rot_dx < 0) ? -1 : 0;
          } else if (projs[j].dy == 0) {
            projs[j].dy = (rot_dy > 0) ? 1 : (rot_dy < 0) ? -1 : 0;
          }
          projs[j].active = 1;
          projs[j].is_enemy = 1;  // 敌人弹丸
          projs[j].is_triangle = 1;  // 三角形
          projs[j].is_boss = 0;
          projs[j].damage = 4;
          break;
        }
      }
    }
  } else {
    // 法师：单个圆形弹丸
    float spawn_dist = WIZARD_COLLISION_RADIUS + PROJECTILE_RADIUS + 1.0f;
    float start_x = enemy_x + ndx * spawn_dist;
    float start_y = enemy_y + ndy * spawn_dist;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
      if (!projs[i].active) {
        projs[i].x = (uint16_t)start_x;
        projs[i].y = (uint16_t)start_y;
        projs[i].dx = (int16_t)roundf(ndx * WIZARD_PROJECTILE_SPEED);
        projs[i].dy = (int16_t)roundf(ndy * WIZARD_PROJECTILE_SPEED);
        if (projs[i].dx == 0 && projs[i].dy == 0) {
          projs[i].dx = (ndx > 0) ? 1 : (ndx < 0) ? -1 : 0;
          projs[i].dy = (ndy > 0) ? 1 : (ndy < 0) ? -1 : 0;
        }
        projs[i].active = 1;
        projs[i].is_enemy = 1;  // 敌人弹丸
        projs[i].is_triangle = 0;  // 圆形
        projs[i].is_boss = 0;
        projs[i].damage = 10;
        break;
      }
    }
  }
}

static void LCD_Draw_Drop(uint16_t x, uint16_t y, uint8_t type, uint8_t erase)
{
  uint8_t color = COLOR_BG;
  if (!erase) {
    if (type == DROP_TYPE_GREEN) color = COLOR_MONSTER_VINE;
    else if (type == DROP_TYPE_YELLOW) color = COLOR_TARGET_YELLOW;
    else if (type == DROP_TYPE_RED) color = COLOR_PLAYER_BODY;

    LCD_Draw_Rect(x - 1, y - 6, 3, 13, color, 1);
    LCD_Draw_Rect(x - 6, y - 1, 13, 3, color, 1);
    LCD_Draw_Rect(x - 2, y - 2, 5, 5, color, 1);
  } else {
    LCD_Draw_Rect(x - 7, y - 7, 15, 15, COLOR_BG, 1);
  }
}

static void LCD_Draw_Dragon(uint16_t x, uint16_t y, uint8_t erase)
{
    if (erase) {
        LCD_Draw_Rect(x - 32, y - 28, 64, 56, COLOR_BG, 1);
        return;
    }

    // 1. 主躯干（红色厚重身体）
    LCD_Draw_Rect(x - 24, y - 12, 48, 22, COLOR_DRAGON_BODY, 1);     // 主体

    // 2. 头部（更大更凶猛）
    LCD_Draw_Rect(x - 34, y - 22, 18, 16, COLOR_DRAGON_BODY, 1);     // 头部底色

    // 3. 红色头冠/背棘（增强红色巨龙气势）
    LCD_Draw_Rect(x - 32, y - 28, 12, 8, COLOR_SAMURAI_ARMOR, 1);    // 红色头冠
    LCD_Draw_Rect(x - 20, y - 26, 6, 6, COLOR_SAMURAI_ARMOR, 1);

    // 4. 眼睛（凶恶黄色/橙色眼睛）
    LCD_Draw_Rect(x - 29, y - 18, 4, 4, COLOR_DRAGON_EYE, 1);        // 左眼
    LCD_Draw_Rect(x - 19, y - 18, 4, 4, COLOR_DRAGON_EYE, 1);        // 右眼

    // 5. 尖角（黄色/亮色装饰）
    LCD_Draw_Rect(x - 36, y - 26, 3, 8, COLOR_DRAGON_HORN, 1);       // 左大角
    LCD_Draw_Rect(x - 12, y - 27, 3, 7, COLOR_DRAGON_HORN, 1);       // 右角

    // 6. 翅膀（红色偏暗 + 黄色翼膜边缘，增加层次感）
    LCD_Draw_Rect(x - 30, y - 16, 14, 18, COLOR_MONSTER_VINE, 1);    // 左翼主体（较暗）
    LCD_Draw_Rect(x + 16, y - 16, 14, 18, COLOR_MONSTER_VINE, 1);    // 右翼主体
    LCD_Draw_Rect(x - 28, y - 14, 10, 12, COLOR_SAMURAI_ACCENT, 1);  // 左翼亮边
    LCD_Draw_Rect(x + 18, y - 14, 10, 12, COLOR_SAMURAI_ACCENT, 1);  // 右翼亮边

    // 7. 腿部（粗壮有力）
    LCD_Draw_Rect(x - 20, y + 8, 7, 12, COLOR_DRAGON_BODY, 1);       // 左前腿
    LCD_Draw_Rect(x - 6,  y + 8, 7, 12, COLOR_DRAGON_BODY, 1);       // 右前腿
    LCD_Draw_Rect(x + 8,  y + 10, 6, 10, COLOR_DRAGON_BODY, 1);      // 左后腿
    LCD_Draw_Rect(x + 18, y + 10, 6, 10, COLOR_DRAGON_BODY, 1);      // 右后腿

    // 8. 尾巴（粗长，带尖刺）
    LCD_Draw_Rect(x + 22, y - 6, 18, 10, COLOR_DRAGON_BODY, 1);      // 尾巴主体
    LCD_Draw_Rect(x + 36, y - 8, 6, 6, COLOR_DRAGON_BODY, 1);        // 尾巴尖端加粗

    // 9. 尾巴尖刺（增加威慑力）
    LCD_Draw_Rect(x + 34, y - 10, 3, 4, COLOR_SAMURAI_ARMOR, 1);

    // 10. 额外红色细节（腹部亮色或纹路）
    LCD_Draw_Rect(x - 18, y - 8, 36, 6, COLOR_SAMURAI_ACCENT, 1);    // 腹部亮色条纹
}

static void Spawn_Drop(uint16_t x, uint16_t y, uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type)
{
  if (Random_U16(100) >= DROP_SPAWN_CHANCE_PERCENT) return;

  int16_t offset_x = (Random_U16(2) == 0 ? -1 : 1) * (8 + Random_U16(8));
  int16_t offset_y = (Random_U16(2) == 0 ? -1 : 1) * (8 + Random_U16(8));
  int32_t spawn_x = (int32_t)x + offset_x;
  int32_t spawn_y = (int32_t)y + offset_y;
  if (spawn_x < 10) spawn_x = 10;
  if (spawn_x > LCD_WIDTH - 10) spawn_x = LCD_WIDTH - 10;
  if (spawn_y < PLAY_AREA_Y0 + 10) spawn_y = PLAY_AREA_Y0 + 10;
  if (spawn_y > LCD_HEIGHT - 10) spawn_y = LCD_HEIGHT - 10;

  for (uint8_t i = 0; i < MAX_DROPS; i++) {
    if (!drop_active[i]) {
      drop_active[i] = 1;
      drop_x[i] = (uint16_t)spawn_x;
      drop_y[i] = (uint16_t)spawn_y;
      drop_type[i] = (uint8_t)(1 + Random_U16(DROP_SPAWN_TYPE_COUNT));
      LCD_Draw_Drop(drop_x[i], drop_y[i], drop_type[i], 0);
      break;
    }
  }
}

static uint8_t Try_Collect_Drops(uint16_t player_x, uint16_t player_y, uint16_t *player_hp, uint16_t *player_max_hp,
                                 float *player_speed_mult, uint8_t *player_move_speed, float *player_shot_speed_mult,
                                 uint32_t *player_shot_interval_ms, uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type)
{
  for (uint8_t i = 0; i < MAX_DROPS; i++) {
    if (!drop_active[i]) continue;
    if (!Circles_Overlap(player_x, player_y, PLAYER_COLLISION_RADIUS, drop_x[i], drop_y[i], DROP_RADIUS)) continue;

    if (drop_type[i] == DROP_TYPE_GREEN) {
      if (*player_max_hp < PLAYER_MAX_HP_CAP) {
        *player_max_hp += DROP_GREEN_HP_UP;
        if (*player_max_hp > PLAYER_MAX_HP_CAP) *player_max_hp = PLAYER_MAX_HP_CAP;
      }
    } else if (drop_type[i] == DROP_TYPE_YELLOW) {
      if (*player_speed_mult < PLAYER_MAX_SPEED_MULTIPLIER) {
        *player_speed_mult += PLAYER_SPEED_BOOST_STEP;
        if (*player_speed_mult > PLAYER_MAX_SPEED_MULTIPLIER) *player_speed_mult = PLAYER_MAX_SPEED_MULTIPLIER;
        *player_move_speed = (uint8_t)roundf(MOVE_SPEED * *player_speed_mult);
        if (*player_move_speed == 0) *player_move_speed = 1;
      }
    } else if (drop_type[i] == DROP_TYPE_RED) {
      if (*player_shot_speed_mult < PLAYER_MAX_SHOT_SPEED_MULTIPLIER) {
        *player_shot_speed_mult *= (1.0f + PLAYER_SHOT_SPEED_BOOST_STEP);
        if (*player_shot_speed_mult > PLAYER_MAX_SHOT_SPEED_MULTIPLIER) *player_shot_speed_mult = PLAYER_MAX_SHOT_SPEED_MULTIPLIER;
        *player_shot_interval_ms = (uint32_t)(PLAYER_BASE_SHOT_INTERVAL_MS / *player_shot_speed_mult);
        if (*player_shot_interval_ms < PLAYER_BASE_SHOT_INTERVAL_MS / PLAYER_MAX_SHOT_SPEED_MULTIPLIER) {
          *player_shot_interval_ms = PLAYER_BASE_SHOT_INTERVAL_MS / PLAYER_MAX_SHOT_SPEED_MULTIPLIER;
        }
      }
    }

    LCD_Draw_Drop(drop_x[i], drop_y[i], drop_type[i], 1);
    drop_active[i] = 0;
    return 1;
  }
  return 0;
}

static uint8_t Area_Overlaps_Player(uint16_t x, uint16_t y, uint8_t radius,
                                    uint16_t player_x, uint16_t player_y)
{
  return Circles_Overlap(x, y, radius, player_x, player_y, PLAYER_COLLISION_RADIUS);
}

static void Move_Chasing_Enemies(uint16_t player_x, uint16_t player_y,
                                 uint16_t *target_x, uint16_t *target_y,
                                 uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai)
{
  for (uint8_t i = 0; i < TARGET_COUNT; i++) {
    if (!is_wizard[i] && !is_monster[i] && !is_ninja[i] && !is_samurai[i]) continue;

    uint16_t oldx = target_x[i];
    uint16_t oldy = target_y[i];
    uint8_t radius = is_wizard[i] ? WIZARD_COLLISION_RADIUS :
                     is_monster[i] ? MONSTER_COLLISION_RADIUS :
                     is_ninja[i] ? NINJA_COLLISION_RADIUS :
                     SAMURAI_COLLISION_RADIUS;

    int16_t dx = 0;
    int16_t dy = 0;
    uint8_t speed = 0;

    if (is_monster[i] || is_samurai[i]) {
      // 怪物和武士：向主角靠近
      speed = is_monster[i] ? MONSTER_CHASE_SPEED : SAMURAI_CHASE_SPEED;
      if (player_x > oldx) dx = 1;
      else if (player_x < oldx) dx = -1;
      if (player_y > oldy) dy = 1;
      else if (player_y < oldy) dy = -1;
    }
    else if (is_wizard[i]) {
      // 法师：保持距离
      speed = WIZARD_KEEP_DISTANCE_SPEED;
      int16_t dist_x = (int16_t)player_x - (int16_t)oldx;
      int16_t dist_y = (int16_t)player_y - (int16_t)oldy;
      int32_t distance_sq = (int32_t)dist_x * dist_x + (int32_t)dist_y * dist_y;
      uint16_t keep_distance = 60; // 保持60像素距离

      if (distance_sq < keep_distance * keep_distance) {
        // 太近了，向远离主角的方向移动
        if (dist_x > 0) dx = -1;
        else if (dist_x < 0) dx = 1;
        if (dist_y > 0) dy = -1;
        else if (dist_y < 0) dy = 1;
      }
      else if (distance_sq > (keep_distance + 20) * (keep_distance + 20)) {
        // 太远了，向主角靠近
        if (player_x > oldx) dx = 1;
        else if (player_x < oldx) dx = -1;
        if (player_y > oldy) dy = 1;
        else if (player_y < oldy) dy = -1;
      }
      // 距离合适时不移动
    }
    else if (is_ninja[i]) {
      // 忍者：随机移动
      speed = NINJA_RANDOM_SPEED;
      uint8_t rand_dir = Random_U16(8);
      switch (rand_dir) {
        case 0: dx = 1; break;   // 东
        case 1: dx = -1; break;  // 西
        case 2: dy = 1; break;   // 南
        case 3: dy = -1; break;  // 北
        case 4: dx = 1; dy = 1; break;   // 东南
        case 5: dx = 1; dy = -1; break;  // 东北
        case 6: dx = -1; dy = 1; break;  // 西南
        case 7: dx = -1; dy = -1; break; // 西北
      }
    }

    if (dx == 0 && dy == 0) continue;

    int32_t nx = oldx + dx * speed;
    int32_t ny = oldy + dy * speed;

    if (nx < radius) nx = radius;
    if (nx > LCD_WIDTH - radius - 1) nx = LCD_WIDTH - radius - 1;
    if (ny < PLAY_AREA_Y0 + radius) ny = PLAY_AREA_Y0 + radius;
    if (ny > LCD_HEIGHT - radius - 1) ny = LCD_HEIGHT - radius - 1;

    if ((uint16_t)nx == oldx && (uint16_t)ny == oldy) continue;
    if (Area_Overlaps_Player((uint16_t)nx, (uint16_t)ny, radius, player_x, player_y)) continue;

    // 擦除旧位置
    if (is_wizard[i]) LCD_Draw_Wizard(oldx, oldy, 1);
    else if (is_monster[i]) LCD_Draw_Monster(oldx, oldy, 1);
    else if (is_ninja[i]) LCD_Draw_Ninja(oldx, oldy, 1);
    else if (is_samurai[i]) {
      LCD_Draw_Samurai(oldx, oldy, 1);
      // 额外擦除以防止拖影
      LCD_Draw_Rect(oldx - SAMURAI_WIDTH/2 - 1, oldy - SAMURAI_HEIGHT/2 - 1, SAMURAI_WIDTH + 2, SAMURAI_HEIGHT + 2, COLOR_BG, 1);
    }


    target_x[i] = (uint16_t)nx;
    target_y[i] = (uint16_t)ny;

    // 绘制新位置
    if (is_wizard[i]) LCD_Draw_Wizard(target_x[i], target_y[i], 0);
    else if (is_monster[i]) LCD_Draw_Monster(target_x[i], target_y[i], 0);
    else if (is_ninja[i]) LCD_Draw_Ninja(target_x[i], target_y[i], 0);
    else if (is_samurai[i]) LCD_Draw_Samurai(target_x[i], target_y[i], 0);
  }
}

// ===== 更新并绘制弹丸 =====
void Update_And_Draw_Projectiles(Projectile_t *projs, uint16_t *target_x, uint16_t *target_y,
                                 uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai,
                                 uint8_t *target_hp, uint16_t player_x, uint16_t player_y, uint16_t *player_hp, uint16_t *player_max_hp, uint16_t *score, uint8_t *active_enemies,
                                 uint8_t *drop_active, uint16_t *drop_x, uint16_t *drop_y, uint8_t *drop_type,
                                 uint8_t *boss_active, uint16_t boss_x, uint16_t boss_y, uint16_t *boss_hp, uint8_t *boss_defeated) {
  for (int i = 0; i < MAX_PROJECTILES; i++) {
    if (!projs[i].active) continue;

    // 擦除旧弹丸
    if (projs[i].is_triangle) {
      LCD_Draw_Triangle(projs[i].x, projs[i].y, TRIANGLE_PROJECTILE_SIZE, COLOR_BG, 1);
    } else {
      LCD_Draw_Circle(projs[i].x, projs[i].y, PROJECTILE_RADIUS, COLOR_BG, 1);
    }

    projs[i].x += projs[i].dx;
    projs[i].y += projs[i].dy;

    uint8_t proj_radius = projs[i].is_triangle ? TRIANGLE_PROJECTILE_SIZE : PROJECTILE_RADIUS;

    if (projs[i].x < 5 || projs[i].x > LCD_WIDTH - 5 ||
        projs[i].y < 5 || projs[i].y > LCD_HEIGHT - 5 ||
        projs[i].y <= UI_LINE_Y) {
      projs[i].active = 0;
      continue;
    }

    // 检查敌人弹丸与主角的碰撞
    if (projs[i].is_enemy && Circles_Overlap(projs[i].x, projs[i].y, proj_radius, player_x, player_y, PLAYER_COLLISION_RADIUS)) {
      // 擦除弹丸当前位置
      if (projs[i].is_triangle) {
        LCD_Draw_Triangle(projs[i].x, projs[i].y, TRIANGLE_PROJECTILE_SIZE, COLOR_BG, 1);
      } else {
        LCD_Draw_Circle(projs[i].x, projs[i].y, PROJECTILE_RADIUS, COLOR_BG, 1);
      }
      if (*player_hp > projs[i].damage) *player_hp -= projs[i].damage;
      else *player_hp = 0;
      projs[i].active = 0;
      continue;
    }

    // 绘制新弹丸
    uint8_t proj_color = projs[i].is_boss ? COLOR_BOSS_PROJECTILE : COLOR_ENEMY_PROJECTILE;
    if (projs[i].is_triangle) {
      LCD_Draw_Triangle(projs[i].x, projs[i].y, TRIANGLE_PROJECTILE_SIZE, proj_color, 0);
    } else if (projs[i].is_enemy) {
      LCD_Draw_Circle(projs[i].x, projs[i].y, PROJECTILE_RADIUS, proj_color, 0);
    } else {
      LCD_Draw_Circle(projs[i].x, projs[i].y, PROJECTILE_RADIUS, COLOR_TARGET_YELLOW, 0);
    }

    // Check boss collision before ordinary targets
    if (!projs[i].is_enemy && boss_active && *boss_active && Circles_Overlap(projs[i].x, projs[i].y, proj_radius, boss_x, boss_y, BOSS_COLLISION_RADIUS)) {
      if (*boss_hp > 0) *boss_hp -= 1;
      if (*boss_hp == 0) {
        LCD_Draw_Dragon(boss_x, boss_y, 1);
        *boss_active = 0;
        *boss_defeated = 1;
        if (*score <= UINT16_MAX - 10) *score += 10;
      }
      LCD_Draw_Circle(projs[i].x, projs[i].y, PROJECTILE_RADIUS, COLOR_BG, 1);
      projs[i].active = 0;
      continue;
    }

    for (uint8_t j = 0; j < TARGET_COUNT; j++) {
      uint8_t r = is_wizard[j] ? WIZARD_COLLISION_RADIUS :
                  is_monster[j] ? MONSTER_COLLISION_RADIUS :
                  is_ninja[j] ? NINJA_COLLISION_RADIUS :
                  is_samurai[j] ? SAMURAI_COLLISION_RADIUS : TARGET_RADIUS;

      if (Circles_Overlap(projs[i].x, projs[i].y, proj_radius, target_x[j], target_y[j], r)) {
        if (!projs[i].is_enemy) {
          if (target_hp[j] > 0) target_hp[j]--;
          if (target_hp[j] <= 0) {
            if (is_wizard[j]) LCD_Draw_Wizard(target_x[j], target_y[j], 1);
            else if (is_monster[j]) LCD_Draw_Monster(target_x[j], target_y[j], 1);
            else if (is_ninja[j]) LCD_Draw_Ninja(target_x[j], target_y[j], 1);
            else if (is_samurai[j]) LCD_Draw_Samurai(target_x[j], target_y[j], 1);
            else LCD_Draw_Circle(target_x[j], target_y[j], TARGET_RADIUS, COLOR_BG, 1);

            // 击杀得分
            if (is_monster[j]) {
              *score += 1;
            } else if (is_wizard[j]) {
              *score += 2;
            } else if (is_ninja[j] || is_samurai[j]) {
              *score += 3;
            }

            uint16_t kill_x = target_x[j];
            uint16_t kill_y = target_y[j];

            // 清除敌人
            is_wizard[j] = 0;
            is_monster[j] = 0;
            is_ninja[j] = 0;
            is_samurai[j] = 0;
            target_hp[j] = 0;
            target_x[j] = 0;
            target_y[j] = 0;
            (*active_enemies)--;

            // 可能生成掉落
            Spawn_Drop(kill_x, kill_y, drop_active, drop_x, drop_y, drop_type);

            // 击杀奖励：主角血量+10，按照当前血量上限恢复
            if (*player_hp < *player_max_hp) {
              *player_hp += 10;
              if (*player_hp > *player_max_hp) *player_hp = *player_max_hp;
            }
          }
          // 擦除弹丸当前位置
          LCD_Draw_Circle(projs[i].x, projs[i].y, PROJECTILE_RADIUS, COLOR_BG, 1);
        }
        projs[i].active = 0;
        break;
      }
    }
  }
}

// ===== 碰撞检测 =====
uint8_t Circles_Overlap(uint16_t x1, uint16_t y1, uint16_t r1,
                        uint16_t x2, uint16_t y2, uint16_t r2) {
  int32_t dx = (int32_t)x2 - (int32_t)x1;
  int32_t dy = (int32_t)y2 - (int32_t)y1;
  int32_t dist = dx*dx + dy*dy;
  int32_t sumr = (int32_t)r1 + r2;
  return dist <= sumr * sumr;
}

// ===== 绘制函数 =====
// 玩家像素小人绘制（蓝色主体 + 白色腰带 + 白色头带 + 肤色头手）
void LCD_Draw_Player(uint16_t x, uint16_t y, uint8_t erase) {
  int16_t bx = x - PLAYER_WIDTH/2;
  int16_t by = y - PLAYER_HEIGHT/2;
  if (erase) { LCD_Draw_Rect(bx, by, PLAYER_WIDTH, PLAYER_HEIGHT, COLOR_BG, 1); return; }
  LCD_Draw_Rect(bx+4, by, 4,4, COLOR_PLAYER_HEAD,1);
  LCD_Draw_Rect(bx+3, by+1,6,1, COLOR_PLAYER_BAND,1);
  LCD_Draw_Rect(bx+3, by+4,6,4, COLOR_PLAYER_BODY,1);
  LCD_Draw_Rect(bx+3, by+8,6,2, COLOR_PLAYER_BELT,1);
  LCD_Draw_Rect(bx+3, by+10,6,4, COLOR_PLAYER_BODY,1);
  LCD_Draw_Rect(bx+1, by+6,2,2, COLOR_PLAYER_HEAD,1);
  LCD_Draw_Rect(bx+9, by+6,2,2, COLOR_PLAYER_HEAD,1);
}

// 法师绘制（红色长袍 + 绿色排扣 + 黄色帽子 + 权杖）
void LCD_Draw_Wizard(uint16_t x, uint16_t y, uint8_t erase) {
  int16_t bx = x - WIZARD_WIDTH/2;
  int16_t by = y - WIZARD_HEIGHT/2;
  if (erase) { LCD_Draw_Rect(bx-2, by-2, WIZARD_WIDTH+4, WIZARD_HEIGHT+4, COLOR_BG, 1); return; }
  LCD_Draw_Rect(bx+4, by, 6,3, COLOR_WIZARD_HAT,1);
  LCD_Draw_Rect(bx+5, by+3,4,4, COLOR_PLAYER_HEAD,1);
  LCD_Draw_Rect(bx+3, by+7,8,8, COLOR_WIZARD_ROBE,1);
  LCD_Draw_Rect(bx+3, by+14,8,2, COLOR_WIZARD_HAT,1);
}

// 绿色触手怪物绘制（绿色藤蔓 + 红色花 + 黄色眼睛）
void LCD_Draw_Monster(uint16_t x, uint16_t y, uint8_t erase) {
  int16_t bx = x - MONSTER_WIDTH/2;
  int16_t by = y - MONSTER_HEIGHT/2;
  if (erase) { LCD_Draw_Rect(bx-2, by-2, MONSTER_WIDTH+4, MONSTER_HEIGHT+4, COLOR_BG, 1); return; }
  LCD_Draw_Rect(bx+5, by+6,6,10, COLOR_MONSTER_VINE,1);
  LCD_Draw_Rect(bx+6, by+2,4,4, COLOR_MONSTER_FLOWER,1);
  LCD_Draw_Rect(bx+4, by+6,2,2, COLOR_MONSTER_EYE,1);
  LCD_Draw_Rect(bx+10,by+6,2,2, COLOR_MONSTER_EYE,1);
}

// 忍者绘制（黑色衣服 + 白色腰带 + 黄色鞋 + 白色眼睛）
void LCD_Draw_Ninja(uint16_t x, uint16_t y, uint8_t erase) {
  int16_t bx = x - NINJA_WIDTH/2;
  int16_t by = y - NINJA_HEIGHT/2;
  if (erase) { LCD_Draw_Rect(bx-2, by-2, NINJA_WIDTH+4, NINJA_HEIGHT+4, COLOR_BG, 1); return; }

  // 黑色头套与身体
  LCD_Draw_Rect(bx+3, by+1, 8, 6, COLOR_NINJA_CLOTH, 1);
  LCD_Draw_Rect(bx+2, by+7, 10, 9, COLOR_NINJA_CLOTH, 1);

  // 白色腰带
  LCD_Draw_Rect(bx+3, by+10, 8, 2, COLOR_NINJA_BELT, 1);

  // 黄色鞋子
  LCD_Draw_Rect(bx+3, by+15, 3, 2, COLOR_NINJA_SHOE, 1);
  LCD_Draw_Rect(bx+8, by+15, 3, 2, COLOR_NINJA_SHOE, 1);

  // 白色眼睛（露出部分）
  LCD_Draw_Rect(bx+5, by+4, 2, 1, COLOR_NINJA_EYE, 1);
  LCD_Draw_Rect(bx+7, by+4, 2, 1, COLOR_NINJA_EYE, 1);
}

// 日本武士绘制（红色黄色盔甲，带角头盔，手持长刀）
void LCD_Draw_Samurai(uint16_t x, uint16_t y, uint8_t erase) {
  int16_t bx = x - SAMURAI_WIDTH/2;
  int16_t by = y - SAMURAI_HEIGHT/2;
  if (erase) { LCD_Draw_Rect(bx-3, by-3, SAMURAI_WIDTH+6, SAMURAI_HEIGHT+6, COLOR_BG, 1); return; }

  // 头盔（带角）
  LCD_Draw_Rect(bx+4, by+1, 8, 5, COLOR_SAMURAI_HELM, 1);
  LCD_Draw_Rect(bx+6, by, 4, 2, COLOR_SAMURAI_ACCENT, 1);

  // 头部
  LCD_Draw_Rect(bx+5, by+5, 6, 4, COLOR_PLAYER_HEAD, 1);

  // 红色与黄色相间盔甲
  LCD_Draw_Rect(bx+3, by+8, 10, 8, COLOR_SAMURAI_ARMOR, 1);
  LCD_Draw_Rect(bx+4, by+9, 2, 6, COLOR_SAMURAI_ACCENT, 1);
  LCD_Draw_Rect(bx+8, by+9, 2, 6, COLOR_SAMURAI_ACCENT, 1);

  // 手臂与刀
  LCD_Draw_Rect(bx+1, by+9, 3, 4, COLOR_SAMURAI_ARMOR, 1);
  LCD_Draw_Rect(bx+12, by+10, 4, 2, COLOR_SAMURAI_BLADE, 1);
  LCD_Draw_Rect(bx+13, by+9, 2, 4, COLOR_SAMURAI_ARMOR, 1);

  // 腿部
  LCD_Draw_Rect(bx+4, by+15, 4, 3, COLOR_SAMURAI_ARMOR, 1);
  LCD_Draw_Rect(bx+8, by+15, 4, 3, COLOR_SAMURAI_ARMOR, 1);
}

void LCD_Draw_King(uint16_t x, uint16_t y, uint8_t erase) {
  int16_t bx = x - 8;
  int16_t by = y - 9;
  if (erase) {
    LCD_Draw_Rect(bx-3, by-3, 22, 24, COLOR_BG, 1);
    return;
  }

  // 黄色王冠
  LCD_Draw_Rect(bx+3, by,   10, 4, COLOR_SAMURAI_ACCENT, 1);
  LCD_Draw_Rect(bx+5, by-2, 6,  3, COLOR_SAMURAI_ACCENT, 1);
  LCD_Draw_Rect(bx+7, by-3, 2,  2, COLOR_TARGET_YELLOW, 1);

  // 头部
  LCD_Draw_Rect(bx+5, by+3, 6, 5, COLOR_PLAYER_HEAD, 1);

  // 红色王袍
  LCD_Draw_Rect(bx+2, by+7, 12, 10, COLOR_SAMURAI_ARMOR, 1);

  // 绿色裤子
  LCD_Draw_Rect(bx+3, by+15, 5, 5, COLOR_MONSTER_VINE, 1);
  LCD_Draw_Rect(bx+8, by+15, 5, 5, COLOR_MONSTER_VINE, 1);

  // 腰带装饰
  LCD_Draw_Rect(bx+3, by+12, 10, 2, COLOR_NINJA_BELT, 1);

  // 手臂
  LCD_Draw_Rect(bx+1,  by+8, 3, 5, COLOR_SAMURAI_ARMOR, 1);
  LCD_Draw_Rect(bx+12, by+8, 3, 5, COLOR_SAMURAI_ARMOR, 1);

  // 权杖
  LCD_Draw_Rect(bx+14, by+7, 2, 8, COLOR_PLAYER_BELT, 1);
  LCD_Draw_Rect(bx+13, by+5, 4, 3, COLOR_SAMURAI_ACCENT, 1);

  // 鞋子
  LCD_Draw_Rect(bx+3,  by+19, 4, 2, COLOR_NINJA_CLOTH, 1);
  LCD_Draw_Rect(bx+9,  by+19, 4, 2, COLOR_NINJA_CLOTH, 1);
}

// 掩体绘制（白色箱子）
// 三角形弹丸绘制（蓝色小三角形）
void LCD_Draw_Triangle(uint16_t x, uint16_t y, uint8_t size, uint8_t color, uint8_t erase) {
  if (erase) color = COLOR_BG;
  // 绘制一个向上的小三角形
  for (int i = 0; i < size; i++) {
    LCD_Draw_Line(x - i, y + i, x + i, y + i, color);
  }
}

// ===== Place_Target 已修复 =====
void Place_Target(uint8_t index,
                  uint16_t *target_x, uint16_t *target_y,
                  uint8_t *is_wizard, uint8_t *is_monster, uint8_t *is_ninja, uint8_t *is_samurai,
                  uint16_t player_x, uint16_t player_y,
                  uint8_t *out_color, uint8_t *out_is_wizard, uint8_t *out_is_monster,
                  uint8_t *out_is_ninja, uint8_t *out_is_samurai)
{
  uint8_t tries = 0;
  while (tries < 200) {
    uint16_t x = Random_U16(LCD_WIDTH - 20) + 10;
    uint16_t y = Random_U16(LCD_HEIGHT - PLAY_AREA_Y0 - 20) + PLAY_AREA_Y0 + 10;

    if (Circles_Overlap(x, y, 12, player_x, player_y, PLAYER_COLLISION_RADIUS)) {
      tries++; continue;
    }

    uint8_t collision = 0;
    for (uint8_t i = 0; i < TARGET_COUNT; i++) {
      if (i == index) continue;
      // 只检查活跃敌人的碰撞
      if (!is_wizard[i] && !is_monster[i] && !is_ninja[i] && !is_samurai[i]) continue;

      uint8_t this_wizard = is_wizard[i];
      uint8_t this_monster = is_monster[i];
      uint8_t this_ninja = is_ninja[i];
      uint8_t this_samurai = is_samurai[i];
      uint8_t r = this_wizard ? WIZARD_COLLISION_RADIUS :
                  this_monster ? MONSTER_COLLISION_RADIUS :
                  this_ninja ? NINJA_COLLISION_RADIUS :
                  this_samurai ? SAMURAI_COLLISION_RADIUS : TARGET_RADIUS;

      if (Circles_Overlap(x, y, 12, target_x[i], target_y[i], r)) {
        collision = 1;
        break;
      }
    }
    if (collision) { tries++; continue; }


    target_x[index] = x;
    target_y[index] = y;

    uint8_t choice = Random_U16(4);
    if (choice == 0) {
      *out_is_wizard = 1; *out_is_monster = 0; *out_is_ninja = 0; *out_is_samurai = 0;
      *out_color = COLOR_WIZARD_ROBE;
      LCD_Draw_Wizard(x, y, 0);
    } else if (choice == 1) {
      *out_is_wizard = 0; *out_is_monster = 1; *out_is_ninja = 0; *out_is_samurai = 0;
      *out_color = COLOR_MONSTER_VINE;
      LCD_Draw_Monster(x, y, 0);
    } else if (choice == 2) {
      *out_is_wizard = 0; *out_is_monster = 0; *out_is_ninja = 1; *out_is_samurai = 0;
      *out_color = COLOR_NINJA_CLOTH;
      LCD_Draw_Ninja(x, y, 0);
    } else {
      *out_is_wizard = 0; *out_is_monster = 0; *out_is_ninja = 0; *out_is_samurai = 1;
      *out_color = COLOR_SAMURAI_ARMOR;
      LCD_Draw_Samurai(x, y, 0);
    }
    return;
  }

  *out_is_wizard = 0; *out_is_monster = 0; *out_is_ninja = 0; *out_is_samurai = 0;
  *out_color = COLOR_BG;
}

static uint16_t Random_U16(uint16_t max) {
  uint32_t rnd = 0;
  HAL_RNG_GenerateRandomNumber(&hrng, &rnd);
  return (uint16_t)(rnd % max);
}
