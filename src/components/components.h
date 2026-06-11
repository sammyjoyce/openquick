/*
 * components.h — the Curspan component catalog in one include.
 *
 * Pull in the whole catalog, or include individual cs_<name>.h headers for a
 * leaner build. Every component renders through a cs_surface and styles itself
 * through theme roles, so the same call works on a CLI stream or a TUI window.
 */

#pragma once

#include "cs_badge.h"
#include "cs_heading.h"
#include "cs_keyvalue.h"
#include "cs_list.h"
#include "cs_note.h"
#include "cs_progress.h"
#include "cs_rule.h"
#include "cs_spinner.h"
#include "cs_table.h"
