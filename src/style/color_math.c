/*
 * Shared color math implementation. See color_math.h.
 */

#include "color_math.h"

// Weighted squared distance between two colors (Rec. 601 channel weights).
// Weighting green most and blue least matches human sensitivity, which keeps
// downsampled hues recognizable.
static uint32_t app_color_dist2(app_rgb_t a, app_rgb_t b) {
  int dr = (int)a.r - (int)b.r;
  int dg = (int)a.g - (int)b.g;
  int db = (int)a.b - (int)b.b;
  return (uint32_t)(30 * dr * dr + 59 * dg * dg + 11 * db * db);
}

int app_color_luma8(app_rgb_t rgb) {
  return (54 * (int)rgb.r + 183 * (int)rgb.g + 19 * (int)rgb.b + 128) >> 8;
}

bool app_color_is_light(app_rgb_t rgb) {
  return app_color_luma8(rgb) >= 140;
}

// Map an 8-bit channel onto one of the 6 cube levels {0,95,135,175,215,255}.
static uint8_t app_color_to_6cube(uint8_t v) {
  if (v < 48) {
    return 0;
  }
  if (v < 115) {
    return 1;
  }
  return (uint8_t)((v - 35) / 40);
}

static uint8_t app_color_6cube_value(uint8_t level) {
  static const uint8_t table[6] = {0, 95, 135, 175, 215, 255};
  return table[level > 5 ? 5 : level];
}

uint8_t app_color_rgb_to_xterm256(app_rgb_t rgb) {
  uint8_t r6 = app_color_to_6cube(rgb.r);
  uint8_t g6 = app_color_to_6cube(rgb.g);
  uint8_t b6 = app_color_to_6cube(rgb.b);

  app_rgb_t cube = {app_color_6cube_value(r6), app_color_6cube_value(g6),
                    app_color_6cube_value(b6)};
  uint8_t cube_index = (uint8_t)(16 + 36 * r6 + 6 * g6 + b6);
  uint32_t cube_dist = app_color_dist2(rgb, cube);

  // Grayscale ramp: indices 232..255 map to values 8,18,...,238.
  int y = app_color_luma8(rgb);
  int gray_i = (y <= 8) ? 0 : (y >= 238) ? 23 : ((y - 8 + 5) / 10);
  int gray_v = 8 + 10 * gray_i;
  app_rgb_t gray = {(uint8_t)gray_v, (uint8_t)gray_v, (uint8_t)gray_v};
  uint8_t gray_index = (uint8_t)(232 + gray_i);
  uint32_t gray_dist = app_color_dist2(rgb, gray);

  return gray_dist < cube_dist ? gray_index : cube_index;
}

uint8_t app_color_rgb_to_ansi16(app_rgb_t rgb) {
  static const app_rgb_t ansi16[16] = {
      {0x00, 0x00, 0x00}, {0x80, 0x00, 0x00}, {0x00, 0x80, 0x00},
      {0x80, 0x80, 0x00}, {0x00, 0x00, 0x80}, {0x80, 0x00, 0x80},
      {0x00, 0x80, 0x80}, {0xc0, 0xc0, 0xc0}, {0x80, 0x80, 0x80},
      {0xff, 0x00, 0x00}, {0x00, 0xff, 0x00}, {0xff, 0xff, 0x00},
      {0x00, 0x00, 0xff}, {0xff, 0x00, 0xff}, {0x00, 0xff, 0xff},
      {0xff, 0xff, 0xff},
  };

  uint8_t best = 0;
  uint32_t best_dist = UINT32_MAX;
  for (uint8_t i = 0; i < 16; i++) {
    uint32_t d = app_color_dist2(rgb, ansi16[i]);
    if (d < best_dist) {
      best = i;
      best_dist = d;
    }
  }
  return best;
}

uint8_t app_color_channel_to_u8(unsigned value, unsigned max) {
  if (max == 0) {
    return 0;
  }
  if (value > max) {
    value = max;
  }
  return (uint8_t)((value * 255u + (max / 2u)) / max);
}

bool app_color_read_hex(const char **p, const char *end, int max_digits,
                        unsigned *value, unsigned *field_max) {
  if (!p || !*p || !value || !field_max) {
    return false;
  }
  unsigned v = 0;
  unsigned m = 1;
  int digits = 0;
  while (*p < end && digits < max_digits) {
    char c = **p;
    unsigned d;
    if (c >= '0' && c <= '9') {
      d = (unsigned)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = (unsigned)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      d = (unsigned)(c - 'A' + 10);
    } else {
      break;
    }
    v = v * 16u + d;
    m *= 16u;
    digits++;
    (*p)++;
  }
  if (digits == 0) {
    return false;
  }
  *value = v;
  *field_max = m - 1u;
  return true;
}
