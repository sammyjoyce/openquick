/*
 * Unit tests for the pure-data TUI menu model.
 */

#include <string.h>
#include <wchar.h>

#include "../src/tui/tui_menu.h"
#include "../src/tui/tui_menu_internal.h"
#include "unit_support.h"

static bool test_menu_state_rejects_zero_items(void) {
  const tui_menu_config_t cfg = {.items = NULL, .item_count = 0};
  tui_menu_state_t *s = NULL;
  return tui_menu_state_create(&cfg, &s) == TUI_MENU_INVALID_ARG && s == NULL;
}

static bool test_menu_state_picks_first_enabled_when_default_negative(void) {
  const tui_menu_item_t items[] = {
      {.label = "first", .id = 1, .disabled = true},
      {.label = "second", .id = 2},
      {.label = "third", .id = 3},
  };
  const tui_menu_config_t cfg = {
      .items = items, .item_count = 3, .default_index = -1};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  bool ok = tui_menu_state_selected_index(s) == 1;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_state_honors_default_index_when_enabled(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2},
      {.label = "c", .id = 3},
  };
  const tui_menu_config_t cfg = {
      .items = items, .item_count = 3, .default_index = 2};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  bool ok = tui_menu_state_selected_index(s) == 2;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_step_skips_separator(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "b", .id = 2},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 3};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  if (tui_menu_state_selected_index(s) != 0) {
    tui_menu_state_destroy(s);
    return false;
  }
  tui_menu_state_step(s, 1);
  bool ok = tui_menu_state_selected_index(s) == 2;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_step_skips_disabled(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2, .disabled = true},
      {.label = "c", .id = 3},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 3};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_step(s, 1);
  bool ok = tui_menu_state_selected_index(s) == 2;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_step_wraps(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 2};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_step(s, -1);
  bool ok = tui_menu_state_selected_index(s) == 1;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_home_end(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2},
      {.label = "c", .id = 3},
      {.label = "d", .id = 4},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 4};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_end(s);
  if (tui_menu_state_selected_index(s) != 3) {
    tui_menu_state_destroy(s);
    return false;
  }
  tui_menu_state_home(s);
  bool ok = tui_menu_state_selected_index(s) == 0;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_page_clamps(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1}, {.label = "b", .id = 2}, {.label = "c", .id = 3},
      {.label = "d", .id = 4}, {.label = "e", .id = 5},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 5};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_page(s, 1, 3);
  if (tui_menu_state_selected_index(s) != 3) {
    tui_menu_state_destroy(s);
    return false;
  }
  tui_menu_state_page(s, 1, 3);
  bool ok = tui_menu_state_selected_index(s) == 4;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_label_strips_ampersand(void) {
  const tui_menu_item_t items[] = {{.label = "&Overview", .id = 1}};
  const tui_menu_config_t cfg = {.items = items, .item_count = 1};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  const wchar_t *w = tui_menu_state_label_wcs(s, 0);
  bool ok =
      wcscmp(w, L"Overview") == 0 && tui_menu_state_mnemonic(s, 0) == L'o';
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_label_literal_ampersand(void) {
  const tui_menu_item_t items[] = {{.label = "AT&&T", .id = 1}};
  const tui_menu_config_t cfg = {.items = items, .item_count = 1};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  const wchar_t *w = tui_menu_state_label_wcs(s, 0);
  bool ok = wcscmp(w, L"AT&T") == 0 && tui_menu_state_mnemonic(s, 0) == 0;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_label_mnemonic_mid_word(void) {
  const tui_menu_item_t items[] = {{.label = "E&xit", .id = 1}};
  const tui_menu_config_t cfg = {.items = items, .item_count = 1};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  const wchar_t *w = tui_menu_state_label_wcs(s, 0);
  bool ok = wcscmp(w, L"Exit") == 0 && tui_menu_state_mnemonic(s, 0) == L'x';
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_mnemonic_unique_returns_index(void) {
  const tui_menu_item_t items[] = {
      {.label = "&Foo", .id = 1},
      {.label = "&Bar", .id = 2},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 2};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  bool beep = false;
  int r = tui_menu_state_mnemonic_jump(s, L'b', &beep);
  bool ok = (r == 1) && !beep;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_mnemonic_duplicate_cycles_no_confirm(void) {
  const tui_menu_item_t items[] = {
      {.label = "F&oo", .id = 1},
      {.label = "B&ar", .id = 2},
      {.label = "B&az", .id = 3},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 3};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  bool beep = false;
  int r1 = tui_menu_state_mnemonic_jump(s, L'a', &beep);
  int idx1 = tui_menu_state_selected_index(s);
  int r2 = tui_menu_state_mnemonic_jump(s, L'a', &beep);
  int idx2 = tui_menu_state_selected_index(s);
  bool ok = r1 < 0 && r2 < 0 && idx1 != idx2 && beep;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_mnemonic_disabled_does_not_consume_key(void) {
  const tui_menu_item_t items[] = {
      {.label = "&Foo", .id = 1, .disabled = true},
      {.label = "&Bar", .id = 2},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 2};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  bool beep = false;
  int r = tui_menu_state_mnemonic_jump(s, L'f', &beep);
  bool ok = r < 0 && !beep;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_search_filters_matches_to_front_of_selection(void) {
  const tui_menu_item_t items[] = {
      {.label = "&Overview", .id = 1},
      {.label = "&System Information", .id = 2},
      {.label = "&Input Dialog", .id = 3},
      {.label = "&Progress Pattern", .id = 4},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 4};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;

  tui_menu_state_search_open(s);
  tui_menu_state_search_append(s, L'p');
  tui_menu_state_search_append(s, L'r');
  tui_menu_state_search_append(s, L'o');

  bool ok =
      tui_menu_state_search_active(s) && tui_menu_state_selected_index(s) == 3;

  tui_menu_state_destroy(s);
  return ok;
}

static bool test_search_case_insensitive(void) {
  const tui_menu_item_t items[] = {
      {.label = "Overview", .id = 1},
      {.label = "System", .id = 2},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 2};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_search_open(s);
  tui_menu_state_search_append(s, L'S');
  bool ok = tui_menu_state_selected_index(s) == 1;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_search_close_clears_query(void) {
  const tui_menu_item_t items[] = {{.label = "Foo", .id = 1}};
  const tui_menu_config_t cfg = {.items = items, .item_count = 1};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_search_open(s);
  tui_menu_state_search_append(s, L'f');
  tui_menu_state_search_close(s);
  bool ok = !tui_menu_state_search_active(s) &&
            wcslen(tui_menu_state_search_query(s)) == 0;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_search_backspace_removes_one_wchar(void) {
  const tui_menu_item_t items[] = {{.label = "Foo", .id = 1}};
  const tui_menu_config_t cfg = {.items = items, .item_count = 1};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_search_open(s);
  tui_menu_state_search_append(s, L'a');
  tui_menu_state_search_append(s, L'b');
  tui_menu_state_search_backspace(s);
  bool ok = wcscmp(tui_menu_state_search_query(s), L"a") == 0;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_select_visible_rejects_disabled(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2, .disabled = true},
      {.label = "c", .id = 3},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 3};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  const bool rejected = !tui_menu_state_select_visible(s, 1);
  const bool selected = tui_menu_state_select_visible(s, 2);
  bool ok = rejected && selected && tui_menu_state_selected_index(s) == 2;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_ensure_selection_visible_updates_top(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2},
      {.label = "c", .id = 3},
      {.label = "d", .id = 4},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 4};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  (void)tui_menu_state_select_visible(s, 3);
  tui_menu_state_ensure_selection_visible(s, 2);
  bool ok = tui_menu_state_top_visible(s) == 2;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_numeric_jump(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.label = "b", .id = 2},
      {.label = "c", .id = 3, .disabled = true},
      {.label = "d", .id = 4},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 4};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  tui_menu_state_numeric_jump(s, 1);
  if (tui_menu_state_selected_index(s) != 1) {
    tui_menu_state_destroy(s);
    return false;
  }
  tui_menu_state_numeric_jump(s, 2);
  bool ok = tui_menu_state_selected_index(s) == 1;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_numbering_skips_separators(void) {
  const tui_menu_item_t items[] = {
      {.label = "a", .id = 1},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "b", .id = 2},
      {.label = "c", .id = 3},
  };
  const tui_menu_config_t cfg = {.items = items, .item_count = 4};
  tui_menu_state_t *s = NULL;
  if (tui_menu_state_create(&cfg, &s) != TUI_MENU_OK)
    return false;
  /* Visible rows: 0=a, 1=sep, 2=b, 3=c. Labels count non-separator rows. */
  bool ok = tui_menu_state_number_for_row(s, 0) == 1 &&
            tui_menu_state_number_for_row(s, 1) == 0 && /* separator */
            tui_menu_state_number_for_row(s, 2) == 2 &&
            tui_menu_state_number_for_row(s, 3) == 3;
  /* row_for_number is the inverse and skips the separator. */
  ok = ok && tui_menu_state_row_for_number(s, 1) == 0 &&
       tui_menu_state_row_for_number(s, 2) == 2 &&
       tui_menu_state_row_for_number(s, 3) == 3 &&
       tui_menu_state_row_for_number(s, 4) == -1 &&
       tui_menu_state_row_for_number(s, 0) == -1;
  tui_menu_state_destroy(s);
  return ok;
}

static bool test_menu_version_macro_is_monotonic(void) {
  // The encode helper must order versions so consumers can feature-detect with
  // a single >= comparison, and the published TUI_MENU_VERSION must match its
  // component macros.
  return TUI_MENU_VERSION == TUI_MENU_VERSION_ENCODE(TUI_MENU_VERSION_MAJOR,
                                                     TUI_MENU_VERSION_MINOR,
                                                     TUI_MENU_VERSION_PATCH) &&
         TUI_MENU_VERSION_ENCODE(1, 0, 0) > TUI_MENU_VERSION_ENCODE(0, 9, 9) &&
         TUI_MENU_VERSION_ENCODE(1, 1, 0) > TUI_MENU_VERSION_ENCODE(1, 0, 9) &&
         TUI_MENU_VERSION >= TUI_MENU_VERSION_ENCODE(1, 0, 0);
}

void run_tui_menu_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_menu_version_macro_is_monotonic(),
              "TUI_MENU_VERSION encodes monotonically and matches components");
  {
    const tui_menu_item_t item = {
        .label = "&Foo",
        .description = "demo",
        .id = 42,
    };
    bool ok =
        !item.disabled && item.kind == TUI_MENU_ITEM_NORMAL && item.id == 42;
    unit_record(stats, ok,
                "tui_menu_item_t zero-init defaults are enabled+normal");
  }
  {
    const tui_menu_result_t r = {.status = TUI_MENU_OK, .selected_id = 7};
    unit_record(stats, r.status == TUI_MENU_OK && r.selected_id == 7,
                "tui_menu_result_t designated-init works");
  }
  {
    const tui_menu_result_t r = {.status = TUI_MENU_CANCELLED};
    unit_record(stats, r.selected_id == TUI_MENU_ID_NONE,
                "tui_menu_result_t defaults selected_id to none");
  }
  {
    tui_menu_state_t *s = NULL;
    unit_record(stats, s == NULL,
                "tui_menu_state_t is forward-declared opaque");
  }

  unit_record(stats, test_menu_state_rejects_zero_items(),
              "tui_menu_state_create rejects zero items");
  unit_record(stats,
              test_menu_state_picks_first_enabled_when_default_negative(),
              "tui_menu_state_create skips disabled default");
  unit_record(stats, test_menu_state_honors_default_index_when_enabled(),
              "tui_menu_state_create honors enabled default_index");
  unit_record(stats, test_menu_step_skips_separator(),
              "tui_menu_state_step skips separators");
  unit_record(stats, test_menu_step_skips_disabled(),
              "tui_menu_state_step skips disabled");
  unit_record(stats, test_menu_step_wraps(),
              "tui_menu_state_step wraps at ends");
  unit_record(stats, test_menu_home_end(),
              "tui_menu home/end land on first/last");
  unit_record(stats, test_menu_page_clamps(), "tui_menu page clamps at ends");
  unit_record(stats, test_menu_label_strips_ampersand(),
              "tui_menu label strips '&' and records mnemonic");
  unit_record(stats, test_menu_label_literal_ampersand(),
              "tui_menu label '&&' renders as literal '&'");
  unit_record(stats, test_menu_label_mnemonic_mid_word(),
              "tui_menu mnemonic can be mid-word");
  unit_record(stats, test_mnemonic_unique_returns_index(),
              "tui_menu unique mnemonic returns the items[] index");
  unit_record(stats, test_mnemonic_duplicate_cycles_no_confirm(),
              "tui_menu duplicate mnemonic cycles selection");
  unit_record(stats, test_mnemonic_disabled_does_not_consume_key(),
              "tui_menu disabled mnemonic does not consume key");
  unit_record(stats, test_search_filters_matches_to_front_of_selection(),
              "tui_menu search snaps selection to first match");
  unit_record(stats, test_search_case_insensitive(),
              "tui_menu search is case-insensitive");
  unit_record(stats, test_search_close_clears_query(),
              "tui_menu search_close clears the query");
  unit_record(stats, test_search_backspace_removes_one_wchar(),
              "tui_menu search backspace pops one wchar");
  unit_record(stats, test_select_visible_rejects_disabled(),
              "tui_menu select_visible rejects disabled rows");
  unit_record(stats, test_ensure_selection_visible_updates_top(),
              "tui_menu keeps selected row in viewport");
  unit_record(stats, test_numeric_jump(),
              "tui_menu numeric jump targets visible row, skips disabled");
  unit_record(stats, test_numbering_skips_separators(),
              "tui_menu numeric labels stay contiguous across separators");
}
