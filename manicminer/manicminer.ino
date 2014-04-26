#include <EEPROM.h>
#include <SPI.h>
#include <GD2.h>

#define CHEAT_INVINCIBLE    0   // means Willy unaffected by nasties
#define START_LEVEL         4  // level to start on, 0-18
#define CHEAT_OPEN_PORTAL   0   // Portal always open

// Game has three controls: LEFT, RIGHT, JUMP.  You can map them
// to any pins by changing the definitions of PIN_L, PIN_R, PIN_J
// below.

// The basic colors
#define BLACK   RGB(0,   0,   0)
#define RED     RGB(255, 0,   0)
#define GREEN   RGB(0,   255, 0)
#define YELLOW  RGB(255, 255, 0)
#define BLUE    RGB(0,   0,   255)
#define MAGENTA RGB(255, 0,   255)
#define CYAN    RGB(0,   255, 255)
#define WHITE   RGB(255, 255, 255)

// Convert a spectrum attribute byte to a 24-bit RGB
static uint32_t attr(byte a)
{
  // bit 6 indicates bright version
  byte l = (a & 64) ? 0xff : 0xaa;
  return RGB(
    ((a & 2) ? l : 0),
    ((a & 4) ? l : 0),
    ((a & 1) ? l : 0));
}

#define HANDLE_WILLY    0
#define HANDLE_SPECIAL  6

// The map's blocks have fixed meanings:
#define ELEM_AIR      0
#define ELEM_FLOOR    1
#define ELEM_CRUMBLE  2
#define ELEM_WALL     3
#define ELEM_CONVEYOR 4
#define ELEM_NASTY1   5
#define ELEM_NASTY2   6

#include "mmtypes.h"

#include "manicminer.h"

#define MAXRAY 40

struct state_t {
  byte level;
  byte lives;
  byte t;
  uint32_t score;
  uint32_t hiscore;

  byte bidir;
  byte air;
  byte conveyordir;
  byte portalattr;
  byte nitems;
  byte items[5];
  byte wx, wy;    // Willy x,y
  byte wd, wf;    // Willy dir and frame
  byte lastdx;    // Willy last x movement
  byte convey;    // Willy caught on conveyor
  byte jumping;
  signed char wyv;
  byte conveyor[2];
  PROGMEM prog_uchar *guardian;
  uint16_t prevray[MAXRAY];
  byte switch1, switch2;
} state;

static void screen2ii(byte x, byte y, byte handle = 0, byte cell = 0)
{
  GD.Vertex2ii(112 + x, 32 + y, handle, cell);
}

static void bump_score(byte n)
{
  if ((state.score < 10000) && (10000 <= (state.score + n )))
    state.lives++;
  state.score += n;
}

static void load_level(void)
{
  const PROGMEM level *l = &levels[state.level];

  // state.conveyor[0] = pgm_read_byte_near(&l->bgchars[8 * 4 + 0]);
  // state.conveyor[1] = pgm_read_byte_near(&l->bgchars[8 * 4 + 3]);

  // the items are 5 sprites, using colors 16 up
  state.nitems = 0;
  for (byte i = 0; i < 5; i++) {
    byte x = pgm_read_byte_near(&l->items[i].x);
    byte y = pgm_read_byte_near(&l->items[i].y);
    byte isitem = (x || y);
    state.items[i] = isitem;
    state.nitems += isitem;
  }

  state.air = pgm_read_byte_near(&l->air);

  // Conveyor direction
  state.conveyordir = pgm_read_byte_near(&l->conveyordir);

  // the hguardians
  state.bidir = pgm_read_byte_near(&l->bidir);
  guardian *pg;
  for (byte i = 0; i < 8; i++) {
    byte a = pgm_read_byte_near(&l->hguard[i].a);
    guards[i].a = a;
    if (a) {
      byte x = pgm_read_byte_near(&l->hguard[i].x);
      byte y = pgm_read_byte_near(&l->hguard[i].y);
      guards[i].x = x;
      guards[i].y = y;
      guards[i].d = pgm_read_byte_near(&l->hguard[i].d);
      guards[i].x0 = pgm_read_byte_near(&l->hguard[i].x0);
      guards[i].x1 = pgm_read_byte_near(&l->hguard[i].x1);
      guards[i].f = 0;
    }
  }

  pg = &guards[4];  // Use slot 4 for special guardians
  switch (state.level) { // Special level handling
  case 4: // Eugene's lair
    pg->a = 0;  // prevent normal guardian logic
    pg->x = 120;
    pg->y = 0;
    pg->d = -1;
    pg->x0 = 0;
    pg->x1 = 88;
    // loadspr16(IMG_GUARD + 4, eugene, 255, 15);
    break;
  case 7:   // Miner Willy meets the Kong Beast
  case 11:  // Return of the Alien Kong Beast
    pg->a = 0;
    pg->x = 120;
    pg->y = 0;
    state.switch1 = 0;
    // loadspr8(IMG_SWITCH1, lightswitch, 0, 6);
    // sprite(IMG_SWITCH1, 48, 0, IMG_SWITCH1);
    state.switch2 = 0;
    // loadspr8(IMG_SWITCH2, lightswitch, 0, 6);
    // sprite(IMG_SWITCH2, 144, 0, IMG_SWITCH2);
    break;
  }

  for (byte i = 0; i < MAXRAY; i++)
    state.prevray[i] = 4095;

  // Willy
  state.wx = pgm_read_byte_near(&l->wx);
  state.wy = pgm_read_byte_near(&l->wy);
  state.wf = pgm_read_byte_near(&l->wf);
  state.wd = pgm_read_byte_near(&l->wd);
  state.jumping = 0;
  state.convey = 0;
  state.wyv = 0;
}

static void draw_level(void)
{
  uint16_t mapsize = 16 * 32 * 4;

  const PROGMEM level *l = &levels[state.level];

  GD.BitmapHandle(2);
  GD.BitmapSource(MANICMINER_ASSET_TILES + 64 * 15 * state.level);
  GD.BitmapHandle(1);
  GD.BitmapSource(MANICMINER_ASSET_GUARDIANS + 8 * 32 * state.level);

  GD.ClearColorRGB(pgm_read_dword(&(l->border)));
  GD.Clear();

  // GD.cmd_clock(240, 50, 30, 0, 10, 0, 33, 100);
  // GD.cmd_number(240, 136, 31U, OPT_CENTER, 47U);
  // GD.cmd_slider(10, 250, 400, 10, 0, 66, 100);
  // GD.cmd_regwrite(0, 0x55aa);

  GD.Begin(BITMAPS);
  GD.cmd_append(MANICMINER_ASSET_MAPS + state.level * mapsize, mapsize);

  // the portal is a sprite
  byte portalx = pgm_read_byte_near(&l->portalx);
  byte portaly = pgm_read_byte_near(&l->portaly);
  // flash it when it is open
  if (CHEAT_OPEN_PORTAL || state.nitems == 0) {
    byte lum = state.t << 5;
    GD.ColorRGB(lum, lum, lum);
  }
  screen2ii(portalx, portaly, 3, state.level);

  // the items are 5 sprites
  uint32_t colors[4] = { MAGENTA, YELLOW, CYAN, GREEN };
  for (byte i = 0; i < 5; i++) {
    byte x = pgm_read_byte_near(&l->items[i].x);
    byte y = pgm_read_byte_near(&l->items[i].y);
    GD.ColorRGB(colors[(i + state.t) & 3]);
    if (x || y)
      screen2ii(x, y, 4, state.level);
  }
}

static void draw_status(byte playing)
{
  const level *l = &levels[state.level];

  uint16_t x = 112;
  uint16_t y = 32 + 128;

  GD.Begin(RECTS);
  GD.ColorRGB(BLACK);
  GD.Vertex2ii(x, y);
  GD.Vertex2ii(x + 256, y + 84);

  GD.ColorRGB(0xaaaa00);
  GD.Vertex2ii(x, y);
  GD.Vertex2ii(x + 256, y + 15);
  GD.ColorRGB(0xe00000);
  GD.Vertex2ii(x, y + 16);
  GD.Vertex2ii(x + 64, y + 30);
  GD.ColorRGB(0x00e000);
  GD.Vertex2ii(x + 64, y + 16);
  GD.Vertex2ii(x + 256, y + 30);

  if (playing) {
    GD.ColorRGB(BLACK);
    char nm[33];
    strcpy_P(nm, l->name);
    GD.cmd_text(240, y + 7, 26, OPT_CENTER, nm);

    GD.ColorRGB(WHITE);
    GD.cmd_text(x + 26, y + 23, 26, OPT_CENTERY | OPT_RIGHTX, "AIR");
    GD.LineWidth(32);
    GD.Begin(LINES);
    GD.Vertex2ii(x + 4 * 8, y + 23);
    GD.Vertex2ii(x + 4 * 8 + state.air, y + 23);
  }

  GD.ColorRGB(YELLOW);
  GD.cmd_text(x, y + 48, 26, OPT_CENTERY, "High Score");
  GD.cmd_text(x + 165, y + 48, 26, OPT_CENTERY, "Score");
  GD.cmd_number(x + 75, y + 48, 26, OPT_CENTERY | 6, state.hiscore);
  GD.cmd_number(x + 255, y + 48, 26, OPT_RIGHTX | OPT_CENTERY | 6, state.score);

  if (playing) {
    GD.ColorRGB(CYAN);
    GD.Begin(BITMAPS);
    for (int i = 0; i < (state.lives - 1); i++) {
      screen2ii(2 + i * 16, 190, HANDLE_WILLY, 3 & (state.t >> 2));
    }
  }
}

static int qsin(byte r, byte a)
{
  byte t;
  switch (a & 3) {
  default:
  case 0:
    t = 0;
    break;
  case 1:
  case 3:
    t = (45 * r) >> 6;
    break;
  case 2:
    t = r;
  }
  return (a & 4) ? -t : t;
}

static void polar(int x, int y, byte r, byte a)
{
  GD.Vertex2ii(x + qsin(r, a), y + qsin(r, a + 2));
}

static void draw_button(int x, int y, byte angle)
{
  int r = 30;
  GD.PointSize(42 * 16);
  GD.ColorRGB(0x808080);
  GD.Begin(POINTS);
  GD.Vertex2ii(x, y);
  GD.ColorRGB(WHITE);
  GD.LineWidth(6 * 16);
  GD.Begin(LINES);
  polar(x, y, r, angle + 2);
  polar(x, y, r, angle + 6);
  GD.Begin(LINE_STRIP);
  polar(x, y, r, angle + 0);
  polar(x, y, r, angle + 2);
  polar(x, y, r, angle + 4);
}

static void draw_controls()
{
  draw_button(      45, 272 - 45,  4);
  draw_button(      45, 272 - 140, 3);
  draw_button(480 - 45, 272 - 45,  0);
  draw_button(480 - 45, 272 - 140, 1);
}

static void draw_willy()
{
  byte frame = state.wf ^ (state.wd ? 7 : 0);
  GD.ColorRGB(WHITE);
  GD.Begin(BITMAPS);
  screen2ii(state.wx, state.wy, 0, frame);
}

void setup()
{
  GD.begin();

  /*
  int cnt = 0;
  for (int i = 0; i < 1000; i++) {
    GD.ClearColorRGB(0x0000ff);
    GD.Clear();
    GD.cmd_number(19, 19, 31, 3, cnt++);
    GD.swap();
  }
  return;
  */

  GD.Clear();
  GD.cmd_inflate(0);
  GD.copy(manicminer_assets, sizeof(manicminer_assets));

  // Handle     Graphic
  //   0        Miner Willy
  //   1        Guardians
  //   2        Background tiles
  //   3        Portals
  //   4        Items
  //   5        Title screen
  //   6        Specials: Eugene, Plinth and Boot

  GD.BitmapHandle(0);
  GD.BitmapSource(MANICMINER_ASSET_WILLY);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 16, 16);
  GD.BitmapLayout(L1, 2, 16);

  GD.BitmapHandle(1);
  GD.BitmapSource(MANICMINER_ASSET_GUARDIANS);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 16, 16);
  GD.BitmapLayout(L1, 2, 16);

  GD.BitmapHandle(2);
  GD.BitmapSource(MANICMINER_ASSET_TILES);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 8, 8);
  GD.BitmapLayout(RGB332, 8, 8);

  GD.BitmapHandle(3);
  GD.BitmapSource(MANICMINER_ASSET_PORTALS);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 16, 16);
  GD.BitmapLayout(RGB332, 16, 16);

  GD.BitmapHandle(4);
  GD.BitmapSource(MANICMINER_ASSET_ITEMS);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 8, 8);
  GD.BitmapLayout(L1, 1, 8);

  GD.BitmapHandle(5);
  GD.BitmapSource(MANICMINER_ASSET_TITLE);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 256, 192);
  GD.BitmapLayout(RGB332, 256, 192);

  GD.BitmapHandle(6);
  GD.BitmapSource(MANICMINER_ASSET_SPECIALS);
  GD.BitmapSize(NEAREST, BORDER, BORDER, 16, 16);
  GD.BitmapLayout(L1, 2, 16);

  GD.swap();
}

static void draw_guardians(void)
{
  guardian *pg;
  for (byte i = 0; i < 8; i++) {
    pg = &guards[i];
    if (pg->a) {
      // Color is pg->a
      GD.ColorRGB(attr(pg->a));
    // if (state.level == 1) exit(0);
      screen2ii(pg->x, pg->y, 1, pg->f);
    }
  }
  pg = &guards[4];
  switch (state.level) { // Special level handling
  case 4: // Eugene's lair
    GD.ColorRGB(WHITE);
    screen2ii(pg->x, pg->y, 6, 0);
  }
}

static void move_guardians(void)
{
  guardian *pg;
  byte lt;  // local time, for slow hguardians
  for (byte i = 0; i < 8; i++) {
    pg = &guards[i];
    if (pg->a) {
      byte vertical = (i >= 4);
      byte frame;
      switch (state.level) {
      case 13:  // Skylabs
        if (pg->y != pg->x1) {
          pg->f = 0;
          pg->y += pg->d;
        } else {
          pg->f++;
        }
        if (pg->f == 8) {
          pg->f = 0;
          pg->x += 64;
          pg->y = pg->x0;
        }
        // Color is pg->a
        screen2ii(pg->x, pg->y, 1, pg->f);
        break;
      default:
        lt = state.t;
        if (!vertical && (pg->a & 0x80)) {
          if (state.t & 1)
            lt = state.t >> 1;
          else
            break;
        }

        if (!vertical) {
          if ((lt & 3) == 0) {
            if (pg->x == pg->x0 && pg->d)
              pg->d = 0;
            else if (pg->x == pg->x1 && !pg->d)
              pg->d = 1;
            else
              pg->x += pg->d ? -8 : 8;
          }
        } else {
          if (pg->y <= pg->x0 && pg->d < 0)
            pg->d = -pg->d;
          else if (pg->y >= pg->x1 && pg->d > 0)
            pg->d = -pg->d;
          else
            pg->y += pg->d;
        }

        if (state.bidir)
          frame = (lt & 3) ^ (pg->d ? 7 : 0);
        else
          if (vertical)
            frame = lt & 3;
          else
            frame = 4 ^ (lt & 3) ^ (pg->d ? 3 : 0);
        pg->f = frame;
      }
    }
  }
  pg = &guards[4];
  switch (state.level) { // Special level handling
  case 4: // Eugene's lair
    // sprite(IMG_GUARD + 4, pg->x, pg->y, IMG_GUARD + 4);
    if (pg->y == pg->x0 && pg->d < 0)
      pg->d = 1;
    if (pg->y == pg->x1 && pg->d > 0)
      pg->d = -1;
    if (state.nitems == 0) {  // all collected -> descend and camp
      if (pg->d == -1)
        pg->d = 1;
      if (pg->y == pg->x1)
        pg->d = 0;
    }
    pg->y += pg->d;
    break;
  case 7: // Miner Willy meets the Kong Beast
  case 11: //  Return of the Alien Kong Beast
    byte frame, color;
    if (!state.switch2) {
      frame = (state.t >> 3) & 1;
      color = 8 + 4;
    } else {
      frame = 2 + ((state.t >> 1) & 1);
      color = 8 + 6;
      if (pg->y < 104) {
        pg->y += 4;
        bump_score(100);
      }
    }
    // if (pg->y != 104) {
    //   loadspr16(IMG_GUARD + 4, state.guardian + (frame << 5), 255, color);
    //   sprite(IMG_GUARD + 4, pg->x, pg->y, IMG_GUARD + 4);
    // } else {
    //   hide(IMG_GUARD + 4);
    // }
  }
}

static void move_all(void)
{
  state.t++;
  move_guardians();
}

static void draw_all(void)
{
  byte midi = pgm_read_byte_near(manicminer_tune + ((state.t >> 1) & 63));
  GD.cmd_regwrite(REG_SOUND, 0x01 | (midi << 8));
  GD.cmd_regwrite(REG_PLAY, 1);
  for (int j = 0; j < 5; j++) {
    draw_level();
    draw_willy();
    draw_guardians();
    draw_status(1);
    draw_controls();
    GD.swap();
    GD.cmd_regwrite(REG_SOUND, 0);
    GD.cmd_regwrite(REG_PLAY, 1);
  }
}

static void game_over()
{
  for (byte i = 0; i <= 96; i++) {
    if (i & 1) {
      GD.cmd_regwrite(REG_SOUND, 0x01 | ((40 + (i / 2)) << 8));
    } else {
      GD.cmd_regwrite(REG_SOUND, 0);
    }
    GD.cmd_regwrite(REG_PLAY, 1);

    GD.Clear();
    GD.ClearColorRGB(attr(9 + GD.random(7)));
    GD.ScissorSize(256, 128);
    GD.ScissorXY(112, 32);
    GD.Clear();

    if (i == 96)
      GD.cmd_text(112 + 122, 32 + 64, 29, OPT_CENTER, "GAME    OVER");

    GD.LineWidth(16 * 4);                           // Leg
    GD.Begin(LINES);
    screen2ii(125, 0);
    screen2ii(125, i);

    GD.Begin(BITMAPS);
    screen2ii(120, 128 - 16, HANDLE_SPECIAL, 1);    // Plinth

    screen2ii(120, i, HANDLE_SPECIAL, 2);           // Boot

    // Scissor so that boot covers up Willy
    GD.ScissorSize(480, 511);
    GD.ScissorXY(0, i + 16 + 32);
    screen2ii(124, 128 - 32, HANDLE_WILLY, 0);      // Willy
    GD.RestoreContext();

    draw_status(1);
    GD.swap();
  }
}

static void title_screen(void)
{
  for (uint16_t i = 0; i < 1500 /*60*/; i++) {
    GD.ClearColorRGB(attr(2));
    GD.Clear();
    GD.Begin(BITMAPS);
    screen2ii(0, 0, 5, 0);
    draw_status(0);

    // Text crawl across bottom
    GD.ScissorSize(256, 272);
    GD.ScissorXY(112, 0);
    GD.ColorRGB(WHITE);
    static const char message[] = "MANIC MINER . . (C) BUG-BYTE ltd. 1983 . . By Matthew Smith . . . Gameduino2 conversion by James Bowman .  .  . Guide Miner Willy through 19 lethal caverns";
    GD.cmd_text(112 + 256 - i, 222, 27, 0, message);

    GD.swap();
  }

  for (state.level = 0; state.level < 19; state.level++) {
    load_level();
    for (byte t = 0; t < 30; t++) {
      draw_all();
      move_all();
    }
  }
}

void loop()
{
  title_screen();
  state.level = 18;
  game_over();
  return;

  state.level = START_LEVEL;
  state.score = 0;
  load_level();

  for (state.lives = 3; state.lives; state.lives--) {
    for (int i = 0; i < 160; i++) {
      draw_all();
      move_all();
    }
    game_over();
    return;
  }
}
