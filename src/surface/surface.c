/*
 * cs_surface — public dispatch + the stream backend. See surface.h.
 *
 * This translation unit is linked into every build that has either front-end.
 * It contains NO curses code; the curses backend lives in surface_curses.c and
 * is reached only through the ops vtable, so a CLI-only or unit-test build
 * links this file without pulling in ncurses.
 */

#include <stdlib.h>
#include <string.h>

#include "surface_internal.h"

cs_surface_t *cs_surface_alloc_(const cs_theme_t *theme) {
  cs_surface_t *s = calloc(1, sizeof *s);
  if (!s) {
    return NULL;
  }
  s->theme = theme ? *theme : cs_theme_default();
  return s;
}

static bool contains_ascii_ci(const char *haystack, const char *needle) {
  if (!haystack || !needle || !needle[0]) {
    return false;
  }
  for (const char *h = haystack; *h; h++) {
    const char *hp = h;
    const char *np = needle;
    while (*hp && *np) {
      char hc = (*hp >= 'A' && *hp <= 'Z') ? (char)(*hp - 'A' + 'a') : *hp;
      char nc = (*np >= 'A' && *np <= 'Z') ? (char)(*np - 'A' + 'a') : *np;
      if (hc != nc) {
        break;
      }
      hp++;
      np++;
    }
    if (!*np) {
      return true;
    }
  }
  return false;
}

bool cs_surface_unicode_enabled_(void) {
  const char *vars[] = {"LC_ALL", "LC_CTYPE", "LANG"};
  for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
    const char *value = getenv(vars[i]);
    if (value && value[0]) {
      return contains_ascii_ci(value, "utf-8") ||
             contains_ascii_ci(value, "utf8");
    }
  }
  return false;
}

// ---- stream backend -------------------------------------------------------

static void stream_set_color(cs_surface_t *s, cs_role_t role, bool bg) {
#ifdef APP_ENABLE_CLI_STYLE
  app_ui_resolved_color_t r =
      cs_theme_resolve(&s->theme, role, s->caps.profile, s->caps.color_count);
  switch (r.kind) {
  case APP_UI_RESOLVED_RGB:
    app_cli_term_emit_truecolor(&s->term, bg, r.rgb);
    break;
  case APP_UI_RESOLVED_INDEXED:
    app_cli_term_emit_indexed(&s->term, bg, r.index);
    break;
  case APP_UI_RESOLVED_DEFAULT:
  case APP_UI_RESOLVED_NONE:
  default:
    break;
  }
#else
  (void)s;
  (void)role;
  (void)bg;
#endif
}

static void stream_set_attr(cs_surface_t *s, cs_attr_t attrs) {
#ifdef APP_ENABLE_CLI_STYLE
  if (attrs & CS_ATTR_BOLD) {
    app_cli_term_emit_attr(&s->term, APP_CLI_ATTR_BOLD);
  }
  if (attrs & CS_ATTR_DIM) {
    app_cli_term_emit_attr(&s->term, APP_CLI_ATTR_DIM);
  }
  if (attrs & CS_ATTR_UNDERLINE) {
    app_cli_term_emit_attr(&s->term, APP_CLI_ATTR_UNDERLINE);
  }
  if (attrs & CS_ATTR_ITALIC) {
    app_cli_term_emit_attr(&s->term, APP_CLI_ATTR_ITALIC);
  }
#else
  (void)s;
  (void)attrs;
#endif
}

static void stream_reset(cs_surface_t *s) {
#ifdef APP_ENABLE_CLI_STYLE
  app_cli_term_emit_reset(&s->term);
#else
  (void)s;
#endif
}

static void stream_write_n(cs_surface_t *s, const char *text, size_t n) {
  if (s->stream_fp && text && n) {
    fwrite(text, 1, n, s->stream_fp);
  }
}

static void stream_newline(cs_surface_t *s) {
  if (s->stream_fp) {
    fputc('\n', s->stream_fp);
  }
}

static void stream_move(cs_surface_t *s, int x, int y) {
  (void)s;
  (void)x;
  (void)y;  // a stream flows top-to-bottom; positioning is a no-op
}

static void stream_destroy(cs_surface_t *s) {
#ifdef APP_ENABLE_CLI_STYLE
  if (s->have_term) {
    app_cli_term_deinit(&s->term);
  }
#else
  (void)s;
#endif
}

static const cs_surface_ops_t cs_stream_ops = {
    .set_color = stream_set_color,
    .set_attr = stream_set_attr,
    .reset = stream_reset,
    .write_n = stream_write_n,
    .newline = stream_newline,
    .move = stream_move,
    .destroy = stream_destroy,
};

cs_surface_t *cs_surface_stream_new(FILE *stream, const app_config_t *config,
                                    const cs_theme_t *theme) {
  cs_surface_t *s = cs_surface_alloc_(theme);
  if (!s) {
    return NULL;
  }
  s->ops = &cs_stream_ops;
  s->stream_fp = stream;
#ifdef APP_ENABLE_CLI_STYLE
  app_cli_term_opts_t opts = {0};
  bool styled = app_cli_term_init(&s->term, stream, config, &opts);
  s->have_term = true;
  s->caps = (cs_caps_t){
      .tty = s->term.is_tty,
      .color = styled,
      .unicode = cs_surface_unicode_enabled_(),
      .interactive = s->term.is_tty,
      .profile = s->term.profile,
      .color_count = s->term.color_count,
      .width = s->term.width ? s->term.width : 80,
  };
#else
  (void)config;
  s->caps = (cs_caps_t){.unicode = cs_surface_unicode_enabled_(),
                        .profile = APP_CLI_COLOR_PROFILE_NONE,
                        .width = 80};
#endif
  return s;
}

// ---- public dispatch ------------------------------------------------------

void cs_surface_free(cs_surface_t *s) {
  if (!s) {
    return;
  }
  if (s->ops && s->ops->destroy) {
    s->ops->destroy(s);
  }
  free(s);
}

cs_caps_t cs_surface_caps(const cs_surface_t *s) {
  return s ? s->caps : (cs_caps_t){0};
}

size_t cs_surface_width(const cs_surface_t *s) {
  return s ? s->caps.width : 0;
}

const cs_theme_t *cs_surface_theme(const cs_surface_t *s) {
  return s ? &s->theme : NULL;
}

void cs_surface_set_role(cs_surface_t *s, cs_role_t role) {
  if (s && s->ops) {
    s->ops->set_color(s, role, false);
  }
}

void cs_surface_set_role_bg(cs_surface_t *s, cs_role_t role) {
  if (s && s->ops) {
    s->ops->set_color(s, role, true);
  }
}

void cs_surface_set_attr(cs_surface_t *s, cs_attr_t attrs) {
  if (s && s->ops) {
    s->ops->set_attr(s, attrs);
  }
}

void cs_surface_reset(cs_surface_t *s) {
  if (s && s->ops) {
    s->ops->reset(s);
  }
}

void cs_surface_write_n(cs_surface_t *s, const char *text, size_t n) {
  if (s && s->ops) {
    s->ops->write_n(s, text, n);
  }
}

void cs_surface_write(cs_surface_t *s, const char *utf8) {
  if (utf8) {
    cs_surface_write_n(s, utf8, strlen(utf8));
  }
}

void cs_surface_repeat(cs_surface_t *s, const char *glyph, size_t count) {
  if (!s || !glyph || !glyph[0]) {
    return;
  }
  size_t len = strlen(glyph);
  for (size_t i = 0; i < count; i++) {
    cs_surface_write_n(s, glyph, len);
  }
}

void cs_surface_newline(cs_surface_t *s) {
  if (s && s->ops) {
    s->ops->newline(s);
  }
}

void cs_surface_move(cs_surface_t *s, int x, int y) {
  if (s && s->ops) {
    s->ops->move(s, x, y);
  }
}

void cs_surface_styled(cs_surface_t *s, cs_role_t role, cs_attr_t attrs,
                       const char *text) {
  if (!s || !text) {
    return;
  }
  cs_surface_set_role(s, role);
  if (attrs) {
    cs_surface_set_attr(s, attrs);
  }
  cs_surface_write(s, text);
  cs_surface_reset(s);
}
