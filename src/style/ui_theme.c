/*
 * Shared semantic terminal UI theme. See ui_theme.h.
 */

#include "ui_theme.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "design_tokens.h"

#define UI_RGBH(rr, gg, bb, hint)              \
  ((app_ui_color_t){.kind = APP_UI_COLOR_RGB,  \
                    .rgb = {(rr), (gg), (bb)}, \
                    .has_ansi16_hint = true,   \
                    .ansi16_hint = (hint)})

#define UI_ADAPT(d, l) ((app_ui_adaptive_color_t){.dark = (d), .light = (l)})

static app_ui_color_t app_ui_rgbh(app_rgb_t rgb, uint8_t hint) {
  return (app_ui_color_t){.kind = APP_UI_COLOR_RGB,
                          .rgb = rgb,
                          .has_ansi16_hint = true,
                          .ansi16_hint = hint};
}

static app_ui_color_scheme_t APP_UI_DEFAULT_SCHEME;
static bool APP_UI_DEFAULT_SCHEME_READY;

static void app_ui_theme_init_default_scheme(void) {
  if (APP_UI_DEFAULT_SCHEME_READY) {
    return;
  }

  const app_design_palette_t *p = &APP_DESIGN_PALETTE;
  APP_UI_DEFAULT_SCHEME =
      (app_ui_color_scheme_t){
          .roles =
              {
                  [APP_UI_ROLE_TEXT] =
                      UI_ADAPT(app_ui_rgbh(p->fg, 7), UI_RGBH(60, 56, 54, 0)),
                  [APP_UI_ROLE_TITLE] = UI_ADAPT(app_ui_rgbh(p->amber, 11),
                                                 UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_DESCRIPTION] =
                      UI_ADAPT(app_ui_rgbh(p->fg, 7), UI_RGBH(60, 56, 54, 0)),
                  [APP_UI_ROLE_CODE] = UI_ADAPT(app_ui_rgbh(p->amber, 3),
                                                UI_RGBH(110, 78, 20, 3)),
                  [APP_UI_ROLE_PROGRAM] = UI_ADAPT(app_ui_rgbh(p->amber, 11),
                                                   UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_MUTED] = UI_ADAPT(app_ui_rgbh(p->muted, 8),
                                                 UI_RGBH(120, 112, 105, 8)),
                  [APP_UI_ROLE_ACCENT] = UI_ADAPT(app_ui_rgbh(p->amber, 11),
                                                  UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_COMMENT] = UI_ADAPT(app_ui_rgbh(p->muted, 8),
                                                   UI_RGBH(120, 112, 105, 8)),
                  [APP_UI_ROLE_FLAG] = UI_ADAPT(app_ui_rgbh(p->amber, 11),
                                                UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_FLAG_DEFAULT] = UI_ADAPT(
                      app_ui_rgbh(p->muted, 8), UI_RGBH(120, 112, 105, 8)),
                  [APP_UI_ROLE_COMMAND] = UI_ADAPT(app_ui_rgbh(p->amber, 11),
                                                   UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_QUOTED_STRING] = UI_ADAPT(
                      app_ui_rgbh(p->green, 2), UI_RGBH(75, 115, 55, 2)),
                  [APP_UI_ROLE_ARGUMENT] =
                      UI_ADAPT(app_ui_rgbh(p->fg, 7), UI_RGBH(60, 56, 54, 0)),
                  [APP_UI_ROLE_HELP] = UI_ADAPT(app_ui_rgbh(p->amber, 11),
                                                UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_DASH] = UI_ADAPT(app_ui_rgbh(p->muted, 8),
                                                UI_RGBH(120, 112, 105, 8)),
                  [APP_UI_ROLE_ERROR_HEADER_FG] = UI_ADAPT(
                      UI_RGBH(255, 245, 235, 15), UI_RGBH(255, 255, 255, 15)),
                  [APP_UI_ROLE_ERROR_HEADER_BG] =
                      UI_ADAPT(app_ui_rgbh(p->red, 1), UI_RGBH(180, 55, 50, 1)),
                  [APP_UI_ROLE_ERROR_DETAILS] =
                      UI_ADAPT(app_ui_rgbh(p->red, 1), UI_RGBH(145, 40, 35, 1)),
                  [APP_UI_ROLE_SUCCESS] = UI_ADAPT(app_ui_rgbh(p->green, 2),
                                                   UI_RGBH(75, 115, 55, 2)),
                  [APP_UI_ROLE_WARNING] = UI_ADAPT(app_ui_rgbh(p->yellow, 3),
                                                   UI_RGBH(145, 105, 25, 3)),
                  [APP_UI_ROLE_INFO] = UI_ADAPT(app_ui_rgbh(p->blue, 4),
                                                UI_RGBH(60, 110, 145, 4)),
                  [APP_UI_ROLE_BORDER] = UI_ADAPT(app_ui_rgbh(p->muted, 7),
                                                  UI_RGBH(165, 158, 150, 8)),
                  [APP_UI_ROLE_SELECTION_FG] =
                      UI_ADAPT(app_ui_rgbh(p->near_black, 0),
                               UI_RGBH(255, 255, 255, 15)),
                  [APP_UI_ROLE_SELECTION_BG] =
                      UI_ADAPT(app_ui_rgbh(p->amber, 11),
                               UI_RGBH(135, 94, 20, 3)),
                  [APP_UI_ROLE_PANEL] = UI_ADAPT(app_ui_rgbh(p->panel, 0),
                                                 UI_RGBH(250, 248, 244, 15)),
                  [APP_UI_ROLE_PANEL_ALT] =
                      UI_ADAPT(app_ui_rgbh(p->panel_alt, 0),
                               UI_RGBH(242, 238, 232, 7)),
              },
      };

  APP_UI_DEFAULT_SCHEME_READY = true;
}

const app_ui_color_scheme_t *app_ui_theme_default_scheme(void) {
  app_ui_theme_init_default_scheme();
  return &APP_UI_DEFAULT_SCHEME;
}

app_ui_color_t app_ui_theme_pick(const app_ui_color_scheme_t *scheme,
                                 app_ui_role_id role,
                                 app_ui_theme_mode_id mode) {
  if (!scheme || role < 0 || role >= APP_UI_ROLE_COUNT) {
    return (app_ui_color_t){.kind = APP_UI_COLOR_UNSET};
  }
  const app_ui_adaptive_color_t *adaptive = &scheme->roles[role];
  return mode == APP_UI_THEME_MODE_LIGHT ? adaptive->light : adaptive->dark;
}

app_ui_resolved_color_t app_ui_color_resolve(app_ui_color_t color,
                                             app_cli_color_profile_id profile,
                                             int color_count) {
  switch (color.kind) {
  case APP_UI_COLOR_UNSET:
    return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_NONE};
  case APP_UI_COLOR_DEFAULT:
    return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_DEFAULT};
  default:
    break;
  }

  if (profile == APP_CLI_COLOR_PROFILE_TRUECOLOR) {
    if (color.kind == APP_UI_COLOR_RGB) {
      return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_RGB,
                                       .rgb = color.rgb};
    }
    // An explicit palette index renders fine on a truecolor terminal, so pass
    // it through as INDEXED rather than dropping it. Without this, an
    // all-indexed theme (e.g. "mono", whose every role is an ANSI16 index)
    // would resolve to NONE and render colorless on the common truecolor
    // profile.
    if (color.kind == APP_UI_COLOR_ANSI256) {
      return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_INDEXED,
                                       .index = color.ansi256};
    }
    if (color.kind == APP_UI_COLOR_ANSI16) {
      return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_INDEXED,
                                       .index = color.ansi16};
    }
  }

  if (profile == APP_CLI_COLOR_PROFILE_ANSI256) {
    uint8_t index;
    if (color.kind == APP_UI_COLOR_ANSI256) {
      index = color.ansi256;
    } else if (color.kind == APP_UI_COLOR_ANSI16) {
      index = color.ansi16;
    } else {
      index = app_color_rgb_to_xterm256(color.rgb);
    }
    return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_INDEXED,
                                     .index = index};
  }

  if (profile == APP_CLI_COLOR_PROFILE_ANSI16) {
    uint8_t index;
    if (color.kind == APP_UI_COLOR_ANSI16) {
      index = color.ansi16;
    } else if (color.has_ansi16_hint) {
      index = color.ansi16_hint;
    } else if (color.kind == APP_UI_COLOR_ANSI256) {
      index = color.ansi256 < 16 ? color.ansi256 : 7;
    } else {
      index = app_color_rgb_to_ansi16(color.rgb);
    }
    // Fold bright colors onto the base 8 for true 8-color terminals.
    if (color_count > 0 && color_count < 16 && index >= 8) {
      index = (uint8_t)(index - 8);
    }
    return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_INDEXED,
                                     .index = index};
  }

  return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_NONE};
}

app_ui_resolved_color_t app_ui_theme_resolve(
    const app_ui_color_scheme_t *scheme, app_ui_role_id role,
    app_ui_theme_mode_id mode, app_cli_color_profile_id profile,
    int color_count) {
  return app_ui_color_resolve(app_ui_theme_pick(scheme, role, mode), profile,
                              color_count);
}

bool app_ui_color_parse(const char *spec, app_ui_color_t *out) {
  if (!spec || !out || !spec[0]) {
    return false;
  }
  const char *s = (spec[0] == '#') ? spec + 1 : spec;
  size_t len = strlen(s);

  bool all_hex = len == 6;
  for (size_t i = 0; all_hex && i < len; i++) {
    if (!isxdigit((unsigned char)s[i])) {
      all_hex = false;
    }
  }
  if (all_hex) {
    const char *p = s;
    uint8_t ch[3];
    for (int i = 0; i < 3; i++) {
      unsigned value;
      unsigned field_max;
      if (!app_color_read_hex(&p, p + 2, 2, &value, &field_max)) {
        return false;
      }
      ch[i] = app_color_channel_to_u8(value, field_max);
    }
    app_rgb_t rgb = {ch[0], ch[1], ch[2]};
    *out = (app_ui_color_t){.kind = APP_UI_COLOR_RGB,
                            .rgb = rgb,
                            .has_ansi16_hint = true,
                            .ansi16_hint = app_color_rgb_to_ansi16(rgb)};
    return true;
  }

  if (spec[0] != '#' && len > 0) {
    bool all_digits = true;
    for (size_t i = 0; i < len; i++) {
      if (!isdigit((unsigned char)s[i])) {
        all_digits = false;
        break;
      }
    }
    if (all_digits) {
      long v = strtol(s, NULL, 10);
      if (v < 0 || v > 255) {
        return false;
      }
      if (v < 16) {
        *out =
            (app_ui_color_t){.kind = APP_UI_COLOR_ANSI16, .ansi16 = (uint8_t)v};
      } else {
        *out = (app_ui_color_t){.kind = APP_UI_COLOR_ANSI256,
                                .ansi256 = (uint8_t)v};
      }
      return true;
    }
  }

  return false;
}

void app_ui_theme_apply_accent(app_ui_color_scheme_t *scheme,
                               app_ui_color_t accent) {
  if (!scheme) {
    return;
  }
  static const app_ui_role_id accent_roles[] = {
      APP_UI_ROLE_TITLE,  APP_UI_ROLE_PROGRAM,      APP_UI_ROLE_COMMAND,
      APP_UI_ROLE_FLAG,   APP_UI_ROLE_HELP,         APP_UI_ROLE_CODE,
      APP_UI_ROLE_ACCENT, APP_UI_ROLE_SELECTION_BG,
  };
  for (size_t i = 0; i < sizeof(accent_roles) / sizeof(accent_roles[0]); i++) {
    scheme->roles[accent_roles[i]].dark = accent;
    scheme->roles[accent_roles[i]].light = accent;
  }
}

void app_ui_theme_apply_env_overrides(app_ui_color_scheme_t *scheme) {
  if (!scheme) {
    return;
  }
  const char *accent = getenv("APP_CLI_ACCENT");
  if (!accent || !accent[0]) {
    return;
  }
  app_ui_color_t color;
  if (app_ui_color_parse(accent, &color)) {
    app_ui_theme_apply_accent(scheme, color);
  }
}
