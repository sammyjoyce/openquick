#include "tui_screen_host.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/deploy_plan.h"
#include "../core/ops.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_panel.h"

/* Collected host-setup values, preserved across Back so users never retype. */
typedef struct {
  char profile[QUICK_ONBOARDING_NAME_MAX];
  char host[QUICK_ONBOARDING_HOST_MAX];
  char remote_root[QUICK_ONBOARDING_PATH_MAX];
  char domain[QUICK_ONBOARDING_HOST_MAX];
  char iap[QUICK_ONBOARDING_IAP_MAX];
  char team_domain[QUICK_ONBOARDING_HOST_MAX];
  char audience[QUICK_ONBOARDING_HOST_MAX];
} quick_host_form_t;

static void host_copy(char *destination, size_t size, const char *source) {
  if (destination && size > 0) {
    snprintf(destination, size, "%s", source ? source : "");
  }
}

static void host_copy_form_to_model(quick_onboarding_model_t *model,
                                    const quick_host_form_t *form) {
  if (!model || !form) {
    return;
  }
  host_copy(model->values.profile, sizeof(model->values.profile),
            form->profile);
  host_copy(model->values.host, sizeof(model->values.host), form->host);
  host_copy(model->values.remote_root, sizeof(model->values.remote_root),
            form->remote_root);
  host_copy(model->values.domain, sizeof(model->values.domain), form->domain);
  host_copy(model->values.iap_type, sizeof(model->values.iap_type), form->iap);
  host_copy(model->values.iap_mode, sizeof(model->values.iap_mode),
            quick_iap_default_mode(form->iap));
  host_copy(model->values.team_domain, sizeof(model->values.team_domain),
            form->team_domain);
  host_copy(model->values.audience, sizeof(model->values.audience),
            form->audience);
}

static const quick_profile_t *host_find_profile(quick_tui_app_state_t *state,
                                                const char *name) {
  const char *pn =
      name && name[0] ? name : quick_tui_default_profile_name(state);
  return quick_profile_config_find(&state->profiles, pn);
}

static void host_seed_form(quick_tui_app_state_t *state,
                           quick_host_form_t *form) {
  memset(form, 0, sizeof(*form));
  const quick_onboarding_values_t *values = &state->onboarding.values;
  const char *def = quick_tui_default_profile_name(state);
  host_copy(form->profile, sizeof(form->profile),
            values->profile[0] ? values->profile : (def ? def : "lab"));
  const quick_profile_t *profile = host_find_profile(state, form->profile);
  host_copy(form->host, sizeof(form->host),
            values->host[0]
                ? values->host
                : (profile && profile->ssh ? profile->ssh : "quick@host"));
  host_copy(form->remote_root, sizeof(form->remote_root),
            values->remote_root[0]
                ? values->remote_root
                : (profile && profile->remote_root ? profile->remote_root
                                                   : "/srv/quick"));
  host_copy(
      form->domain, sizeof(form->domain),
      values->domain[0]
          ? values->domain
          : (profile && profile->base_domain ? profile->base_domain : ""));
  host_copy(
      form->iap, sizeof(form->iap),
      values->iap_type[0]
          ? values->iap_type
          : (profile && profile->iap.type ? profile->iap.type : "tailscale"));
  host_copy(form->team_domain, sizeof(form->team_domain),
            values->team_domain[0] ? values->team_domain
                                   : (profile && profile->iap.team_domain
                                          ? profile->iap.team_domain
                                          : ""));
  host_copy(
      form->audience, sizeof(form->audience),
      values->audience[0]
          ? values->audience
          : (profile && profile->iap.audience ? profile->iap.audience : ""));
}

/* Prompt seeded with the current value; blank keeps current. */
static bool host_prompt(const char *title, const char *label, char *buf,
                        size_t size) {
  char prompt[320];
  snprintf(prompt, sizeof(prompt), "%s [%s]:", label, buf[0] ? buf : "none");
  char input[320] = {0};
  if (tui_input_dialog(title, prompt, input, sizeof(input)) != APP_SUCCESS) {
    return false;
  }
  if (input[0]) {
    snprintf(buf, size, "%s", input);
  }
  return true;
}

/* Guided IAP choice; maps friendly labels to profile iap types. */
static bool host_choose_iap(const char *title, char *iap, size_t size) {
  const int default_index = quick_iap_is_cloudflare(iap)          ? 1
                            : strcmp(iap, "none") == 0            ? 2
                            : strncmp(iap, "tailscale-", 10) == 0 ? 3
                                                                  : 0;
  const tui_menu_item_t items[] = {
      {.label = "&Tailscale",
       .description = "Recommended for a private personal or team host",
       .id = 1},
      {.label = "&Cloudflare Access",
       .description = "Public DNS name protected by an org login",
       .id = 2},
      {.label = "&Local / advanced",
       .description = "Loopback development only (no access protection)",
       .id = 3},
      {.label = "Tailscale &modes (advanced)",
       .description = "Pick serve, localapi, or tsnet explicitly",
       .id = 4},
  };
  tui_menu_result_t r = tui_show_menu(
      NULL, &(tui_menu_config_t){
                .title = title,
                .subtitle = "Access protection sits in front of your sites",
                .items = items,
                .item_count = (int)(sizeof(items) / sizeof(items[0])),
                .default_index = default_index,
                .frame_height = 14,
                .frame_width = 74,
                .show_numeric_keys = true});
  if (r.status != TUI_MENU_OK) {
    return false;
  }
  if (r.selected_id == 1) {
    snprintf(iap, size, "%s", "tailscale");
    return true;
  }
  if (r.selected_id == 2) {
    snprintf(iap, size, "%s", "cloudflare");
    return true;
  }
  if (r.selected_id == 3) {
    snprintf(iap, size, "%s", "none");
    return true;
  }
  const int mode_default_index = strcmp(iap, "tailscale-localapi") == 0 ? 1
                                 : strcmp(iap, "tailscale-tsnet") == 0  ? 2
                                                                        : 0;
  const tui_menu_item_t modes[] = {
      {.label = "tailscale-&serve",
       .description = "Simplest; path fallback URL https://host.ts.net/~/site/",
       .id = 1},
      {.label = "tailscale-&localapi",
       .description = "True per-site subdomains via a wildcard domain + Caddy",
       .id = 2},
      {.label = "tailscale-&tsnet",
       .description = "quickd joins the tailnet itself",
       .id = 3},
  };
  tui_menu_result_t m = tui_show_menu(
      NULL, &(tui_menu_config_t){
                .title = "Tailscale mode",
                .subtitle = "Pure *.ts.net names use a path fallback per site",
                .items = modes,
                .item_count = (int)(sizeof(modes) / sizeof(modes[0])),
                .default_index = mode_default_index,
                .frame_height = 13,
                .frame_width = 74,
                .show_numeric_keys = true});
  if (m.status != TUI_MENU_OK) {
    return false;
  }
  const char *v = m.selected_id == 2   ? "tailscale-localapi"
                  : m.selected_id == 3 ? "tailscale-tsnet"
                                       : "tailscale-serve";
  snprintf(iap, size, "%s", v);
  return true;
}

static bool host_validate(const quick_host_form_t *form, char *err,
                          size_t errsize) {
  if (!quick_profile_name_is_safe(form->profile)) {
    snprintf(err, errsize,
             "Nickname may use letters, digits, dot, underscore, and dash.");
    return false;
  }
  if (!quick_ssh_target_is_safe(form->host)) {
    snprintf(err, errsize,
             "SSH target looks unsafe. Use something like quick@host.");
    return false;
  }
  if (!quick_remote_path_is_safe(form->remote_root)) {
    snprintf(err, errsize,
             "Storage folder must be an absolute path (e.g. /srv/quick).");
    return false;
  }
  if (form->domain[0] && !quick_domain_is_safe(form->domain)) {
    snprintf(err, errsize, "Website address must be a plain DNS name.");
    return false;
  }
  if (!quick_iap_is_supported(form->iap)) {
    snprintf(err, errsize, "Unknown access-protection type.");
    return false;
  }
  if (quick_iap_is_cloudflare(form->iap) &&
      (!form->team_domain[0] || !form->audience[0])) {
    snprintf(err, errsize,
             "Cloudflare Access needs a team domain and application audience.");
    return false;
  }
  return true;
}

static void host_build_iap(const quick_host_form_t *form,
                           quick_iap_config_t *iap) {
  memset(iap, 0, sizeof(*iap));
  iap->type = (char *)form->iap;
  iap->mode = (char *)quick_iap_default_mode(form->iap);
  if (strcmp(form->iap, "none") == 0) {
    iap->mode = NULL;
  }
  if (quick_iap_is_cloudflare(form->iap)) {
    iap->team_domain = (char *)form->team_domain;
    iap->audience = (char *)form->audience;
  }
}

static app_error host_copy_owned_string(char **slot, const char *value) {
  if (!slot) {
    return APP_ERROR_INVALID_ARG;
  }
  if (!value || value[0] == '\0') {
    return APP_SUCCESS;
  }
  *slot = strdup(value);
  return *slot ? APP_SUCCESS : APP_ERROR_MEMORY;
}

/* Build the exact candidate profile in memory; no config file is touched. */
static app_error host_build_verification_profiles(
    const quick_tui_app_state_t *state, const quick_host_form_t *form,
    const quick_iap_config_t *iap, quick_profile_config_t *profiles) {
  if (!profiles) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_profile_config_init(profiles);
  if (!state || !form) {
    return APP_ERROR_INVALID_ARG;
  }

  profiles->default_profile = strdup(form->profile);
  quick_profile_t *profile =
      quick_profile_config_upsert(profiles, form->profile);
  if (!profiles->default_profile || !profile) {
    return APP_ERROR_MEMORY;
  }

  const quick_profile_t *existing =
      quick_profile_config_find(&state->profiles, form->profile);
  const char *base_domain = form->domain[0]
                                ? form->domain
                                : (existing ? existing->base_domain : NULL);
  const char *iap_type = iap && iap->type ? iap->type : "tailscale";
  app_error err = host_copy_owned_string(&profile->ssh, form->host);
  if (err == APP_SUCCESS) {
    err = host_copy_owned_string(&profile->remote_root, form->remote_root);
  }
  if (err == APP_SUCCESS) {
    err = host_copy_owned_string(&profile->base_domain, base_domain);
  }
  if (err == APP_SUCCESS && existing) {
    err = host_copy_owned_string(&profile->base_url, existing->base_url);
    profile->deploy = existing->deploy;
  }
  if (err == APP_SUCCESS) {
    err = host_copy_owned_string(&profile->iap.type, iap_type);
  }
  if (err == APP_SUCCESS) {
    err = host_copy_owned_string(&profile->iap.mode, iap ? iap->mode : NULL);
  }
  if (err == APP_SUCCESS) {
    err = host_copy_owned_string(
        &profile->iap.team_domain,
        iap && quick_iap_is_cloudflare(iap_type) ? iap->team_domain : NULL);
  }
  if (err == APP_SUCCESS) {
    err = host_copy_owned_string(
        &profile->iap.audience,
        iap && quick_iap_is_cloudflare(iap_type) ? iap->audience : NULL);
  }
  return err;
}

static void host_show_profile_save_failure(const char *title,
                                           const quick_host_form_t *form,
                                           app_error err) {
  char msg[600];
  snprintf(msg, sizeof(msg),
           "Could not save profile \"%s\": %s.\n\nCheck the profile config "
           "directory permissions and available disk space, then try again. "
           "Your entered values are still available.",
           form->profile, app_strerror(err));
  tui_show_message(title, msg);
}

static void host_show_verification_failure(
    app_error err, const quick_doctor_result_t *result) {
  const quick_doctor_check_t *failure = NULL;
  if (result) {
    for (size_t i = 0; i < result->count; i++) {
      if (result->checks[i].status == QUICK_DOCTOR_STATUS_FAIL) {
        failure = &result->checks[i];
        break;
      }
    }
  }

  char msg[2400];
  const char *doctor_error = err == APP_SUCCESS ? NULL : app_strerror(err);
  if (failure) {
    snprintf(
        msg, sizeof(msg),
        "Host verification did not pass.\n\nSSH checks were non-interactive "
        "and bounded to one connection attempt with a 10-second connect "
        "timeout.\n\nFirst failing check\n  group: %s\n  check: %s\n  "
        "detail: %s\n  remediation: %s%s%s",
        failure->group ? failure->group : "",
        failure->name ? failure->name : "",
        failure->detail ? failure->detail : "",
        failure->remediation ? failure->remediation : "",
        doctor_error ? "\n\nDoctor error: " : "",
        doctor_error ? doctor_error : "");
  } else {
    snprintf(msg, sizeof(msg),
             "Host verification could not complete.\n\nSSH checks were "
             "non-interactive and bounded to one connection attempt with a "
             "10-second connect timeout.\n\nDoctor error: %s",
             doctor_error ? doctor_error : "no failing check was returned");
  }
  tui_show_message("Host verification failed", msg);
}

/* Collect fields into form. Returns false on cancel. */
static bool host_collect(quick_tui_app_state_t *state, const char *title,
                         quick_host_form_t *form) {
  char previous_profile[sizeof(form->profile)];
  snprintf(previous_profile, sizeof(previous_profile), "%s", form->profile);
  if (!host_prompt(title, "Profile nickname", form->profile,
                   sizeof(form->profile))) {
    return false;
  }
  const quick_profile_t *p = host_find_profile(state, form->profile);
  if (p && strcmp(previous_profile, form->profile) != 0) {
    if (p->ssh && !form->host[0]) {
      snprintf(form->host, sizeof(form->host), "%s", p->ssh);
    }
    if (p->remote_root) {
      snprintf(form->remote_root, sizeof(form->remote_root), "%s",
               p->remote_root);
    }
    if (p->base_domain) {
      snprintf(form->domain, sizeof(form->domain), "%s", p->base_domain);
    }
  }
  if (!host_prompt(title, "SSH target (user@host)", form->host,
                   sizeof(form->host))) {
    return false;
  }
  if (!host_prompt(title, "Storage folder on the host", form->remote_root,
                   sizeof(form->remote_root))) {
    return false;
  }
  if (!host_prompt(title, "Website address (domain, optional)", form->domain,
                   sizeof(form->domain))) {
    return false;
  }
  if (!host_choose_iap(title, form->iap, sizeof(form->iap))) {
    return false;
  }
  if (quick_iap_is_cloudflare(form->iap)) {
    if (!host_prompt(title, "Cloudflare team domain URL", form->team_domain,
                     sizeof(form->team_domain))) {
      return false;
    }
    if (!host_prompt(title, "Cloudflare application audience (AUD)",
                     form->audience, sizeof(form->audience))) {
      return false;
    }
  }
  return true;
}

/* Review panel; returns chosen action: 0 back, 1 confirm. */
typedef struct {
  const quick_host_form_t *form;
  const char *title;
  const char *action_hint;
  const char *url_behavior;
  bool unsafe_public_candidate;
  bool confirmed;
} quick_host_review_state_t;

static void host_review_redraw(tui_window_t *window, void *userdata) {
  quick_host_review_state_t *st = userdata;
  const quick_host_form_t *f = st->form;
  const char *security =
      st->unsafe_public_candidate
          ? "PUBLIC WITHOUT AUTH — explicit override required"
          : (strcmp(f->iap, "none") == 0 ? "loopback only — no authentication"
                                         : "authentication configured");
  tui_draw_border(window);
  tui_set_color(window->win, TUI_COLOR_TITLE);
  tui_print_centered(window->win, 1, st->title);
  tui_unset_color(window->win, TUI_COLOR_TITLE);
  int y = 3;
  char line[320];
  const struct {
    const char *k;
    const char *v;
  } rows[] = {
      {"profile", f->profile},
      {"resolved host", f->host},
      {"storage folder", f->remote_root},
      {"website", f->domain[0] ? f->domain : "(path fallback)"},
      {"access", f->iap},
      {"security", security},
      {"url behavior", st->url_behavior},
  };
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    snprintf(line, sizeof(line), "%-15s %s", rows[i].k, rows[i].v);
    mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  }
  y++;
  snprintf(line, sizeof(line),
           "CLI: quick serve install --profile %s --host %s --remote-root %s",
           f->profile, f->host, f->remote_root);
  tui_set_color(window->win, TUI_COLOR_DIM);
  mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  snprintf(line, sizeof(line), "     --domain %s --iap %s%s",
           f->domain[0] ? f->domain : "(none)", f->iap,
           st->action_hint ? st->action_hint : "");
  mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  tui_unset_color(window->win, TUI_COLOR_DIM);
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     "Enter: confirm  Esc: back (nothing changed yet)");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t host_review_key(tui_window_t *window, int ch,
                                            void *userdata) {
  (void)window;
  quick_host_review_state_t *st = userdata;
  if (ch == '\n' || ch == KEY_ENTER) {
    st->confirmed = true;
    return TUI_MODAL_DONE;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q') {
    st->confirmed = false;
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static const char *host_url_behavior(const quick_host_form_t *form) {
  if (strcmp(form->iap, "tailscale-serve") == 0 ||
      strcmp(form->iap, "tailscale-tsnet") == 0) {
    return "path fallback: https://host.ts.net/~/<site>/";
  }
  if (quick_iap_is_cloudflare(form->iap) || form->domain[0]) {
    return "per-site subdomain: https://<site>.<domain>";
  }
  if (strcmp(form->iap, "none") == 0) {
    return "loopback only (no public URL)";
  }
  return "per-site subdomain once a wildcard domain is configured";
}

/* ---- install progress ---- */
typedef struct {
  tui_progress_t *progress;
  quick_install_phase_t phase;
  volatile sig_atomic_t cancel;
} quick_host_progress_state_t;

static int host_phase_percent(quick_install_phase_t phase) {
  return (int)phase * 100 / (int)QUICK_INSTALL_PHASE_DONE;
}

static void host_progress_cb(quick_install_phase_t phase,
                             quick_stream_kind_t stream, const char *line,
                             void *userdata) {
  (void)stream;
  quick_host_progress_state_t *st = userdata;
  if (!st) {
    return;
  }
  if (tui_interrupted()) {
    st->cancel = 1;
  }
  if (phase != QUICK_INSTALL_PHASE_NONE) {
    st->phase = phase;
  }
  if (st->progress) {
    char status[200];
    if (line && line[0]) {
      snprintf(status, sizeof(status), "%s: %.140s",
               quick_install_phase_label(st->phase), line);
    } else {
      snprintf(status, sizeof(status), "%s...",
               quick_install_phase_label(st->phase));
    }
    char *nl = strchr(status, '\n');
    if (nl) {
      *nl = '\0';
    }
    tui_progress_update(st->progress, host_phase_percent(st->phase), status);
  }
}

static void host_show_install_failure(app_error err,
                                      const quick_install_result_t *res) {
  char msg[2400];
  const quick_install_phase_t failure_phase =
      res->failure_phase != QUICK_INSTALL_PHASE_NONE ? res->failure_phase
                                                     : res->last_phase;
  const char *phase = quick_install_phase_label(failure_phase);
  const char *what =
      res->failure_message ? res->failure_message : app_strerror(err);
  const char *rem =
      res->remediation ? res->remediation : "Fix the issue and try again.";
  char changed[600];
  if (!res->mutation_started) {
    snprintf(changed, sizeof(changed), "Nothing on the host was changed.");
  } else if (res->rollback_attempted) {
    snprintf(changed, sizeof(changed), "Changes were started. Rollback %s.%s%s",
             res->rollback_ok ? "restored the previous install"
                              : "did NOT fully restore",
             res->backup_path ? " Backup: " : "",
             res->backup_path ? res->backup_path : "");
  } else {
    snprintf(changed, sizeof(changed),
             "Changes were started but rollback was not attempted.%s%s",
             res->backup_path ? " Backup kept at " : "",
             res->backup_path ? res->backup_path : "");
  }
  char residue[800] = {0};
  if (res->partial_cleanup_remains) {
    snprintf(residue, sizeof(residue), "\n\nCleanup residue remains:\n%s",
             res->cleanup_detail ? res->cleanup_detail
                                 : "No cleanup detail was reported.");
  }
  snprintf(msg, sizeof(msg),
           "Setup failed during: %s\n\n%s\n\n%s%s\n\nWhat to do next:\n%s",
           phase, what, changed, residue, rem);
  tui_show_message("Host setup failed", msg);
}

static void host_show_install_cancellation(const char *title,
                                           const quick_install_result_t *res) {
  if (!res->mutation_started) {
    tui_show_message(
        title,
        "Setup was cancelled before any changes were made. Nothing changed.");
    return;
  }

  const quick_install_phase_t cancellation_phase =
      res->failure_phase != QUICK_INSTALL_PHASE_NONE ? res->failure_phase
                                                     : res->last_phase;
  const char *rollback = !res->rollback_attempted
                             ? "not attempted"
                             : (res->rollback_ok ? "attempted and succeeded"
                                                 : "attempted and FAILED");
  const char *backup =
      res->backup_path ? res->backup_path : "no backup path reported";
  const char *residue =
      res->partial_cleanup_remains
          ? (res->cleanup_detail
                 ? res->cleanup_detail
                 : "cleanup residue was reported without detail")
          : "none reported";
  char msg[2000];
  snprintf(msg, sizeof(msg),
           "Setup was cancelled after host changes began.\n\nPhase: %s\n"
           "Rollback: %s\nBackup: %s\nCleanup residue: %s",
           quick_install_phase_label(cancellation_phase), rollback, backup,
           residue);
  tui_show_message(title, msg);
}

static void host_show_install_success(const quick_host_form_t *form,
                                      const quick_install_result_t *res) {
  char msg[900];
  snprintf(msg, sizeof(msg),
           "OpenQuick is installed on %s.\n\nHost doctor: %s\nProfile \"%s\" "
           "saved and set as default.%s%s\n\nYou can now deploy a project to "
           "this host.",
           form->host, res->doctor_ok ? "healthy" : "see details",
           form->profile, res->backup_path ? "\nBackup kept: " : "",
           res->backup_path ? res->backup_path : "");
  tui_show_message("Host ready", msg);
}

/* Runs the shared install op behind a progress panel. Returns app_error. */
static app_error host_run_install(quick_tui_app_state_t *state,
                                  const quick_host_form_t *form,
                                  bool allow_public_unsafe,
                                  quick_install_result_t *result) {
  quick_iap_config_t iap;
  host_build_iap(form, &iap);
  quick_install_request_t request = {
      .host = form->host,
      .remote_root = form->remote_root,
      .domain = form->domain[0] ? form->domain : NULL,
      .iap = &iap,
      .non_interactive = true,
      .cancel_flag = tui_interrupt_flag(),
      .allow_public_unsafe = allow_public_unsafe,
      .connect_timeout_seconds = 10,
  };
  tui_progress_t *progress = tui_progress_create("Setting up host", 100);
  quick_host_progress_state_t pstate = {
      .progress = progress, .phase = QUICK_INSTALL_PHASE_NONE, .cancel = 0};
  if (progress) {
    tui_progress_update(progress, 2, "starting...");
  }
  app_error err =
      quick_op_serve_install(&request, host_progress_cb, &pstate, result);
  if (progress) {
    tui_progress_update(progress, 100, err == APP_SUCCESS ? "done" : "stopped");
    tui_progress_destroy(progress);
  }
  (void)state;
  return err;
}

static bool host_retry_is_safe(const quick_onboarding_model_t *model) {
  return !model->mutation.started ||
         (model->mutation.rollback_attempted && model->mutation.rollback_ok &&
          !model->mutation.partial_cleanup_remains);
}

static void host_note_doctor_checks(quick_onboarding_model_t *model,
                                    app_error err,
                                    const quick_doctor_result_t *result) {
  bool ssh_completed = false;
  bool ssh_passed = false;
  bool doctor_completed = false;
  bool doctor_passed = false;
  if (result) {
    for (size_t i = 0; i < result->count; i++) {
      const quick_doctor_check_t *check = &result->checks[i];
      if (check->name && strcmp(check->name, "ssh_present") == 0) {
        ssh_completed = true;
        ssh_passed = check->status == QUICK_DOCTOR_STATUS_OK;
      } else if (check->name && strcmp(check->name, "quickd_doctor") == 0) {
        doctor_completed = true;
        doctor_passed = check->status == QUICK_DOCTOR_STATUS_OK;
      }
    }
  }
  (void)err;
  quick_onboarding_note_check(model, QUICK_ONBOARD_CHECK_SSH,
                              ssh_completed && ssh_passed && doctor_completed);
  quick_onboarding_note_check(model, QUICK_ONBOARD_CHECK_DOCTOR,
                              doctor_completed && doctor_passed);
}

static bool host_prepare_model(quick_tui_app_state_t *state, bool do_install) {
  quick_onboarding_model_t *model = &state->onboarding;
  quick_onboarding_event_t choice = do_install
                                        ? QUICK_ONBOARD_EVENT_CHOOSE_INSTALL
                                        : QUICK_ONBOARD_EVENT_CHOOSE_CONNECT;
  quick_onboarding_flow_t flow = do_install ? QUICK_ONBOARD_FLOW_INSTALL_HOST
                                            : QUICK_ONBOARD_FLOW_CONNECT_HOST;
  if (model->state == QUICK_ONBOARD_STATE_HOST_FIELDS && model->flow == flow) {
    return true;
  }
  if (model->state == QUICK_ONBOARD_STATE_IDLE ||
      model->state == QUICK_ONBOARD_STATE_WELCOME ||
      model->state == QUICK_ONBOARD_STATE_LOCAL_PREVIEW) {
    return quick_onboarding_transition(model, choice);
  }

  quick_onboarding_return_t destination = model->return_destination;
  quick_onboarding_values_t values = model->values;
  quick_onboarding_model_reset(model, destination);
  model->values = values;
  return quick_onboarding_transition(model, choice);
}

/* Shared flow for both "connect" (no install) and "new host" (install). */
static bool host_setup_flow(quick_tui_app_state_t *state, bool do_install) {
  if (!state || !host_prepare_model(state, do_install)) {
    return false;
  }
  (void)quick_tui_reload_profiles(state);
  quick_onboarding_model_t *model = &state->onboarding;
  const char *title = do_install ? "Set up a new host" : "Connect a host";

  if (do_install) {
    tui_show_message(
        title,
        "This installs OpenQuick (quickd) on a Linux server you reach over "
        "SSH.\n\nRequirements:\n- A Linux host with systemd\n- SSH access with "
        "your key already set up\n- sudo that does not prompt for a password "
        "(or run from a terminal)\n\nNothing changes until you confirm the "
        "plan.");
  } else {
    tui_show_message(
        title,
        "A profile is a saved host connection. This verifies how to reach a "
        "host that already runs OpenQuick before saving it.\n\nNothing is "
        "written until you confirm.");
  }

  quick_host_form_t form;
  host_seed_form(state, &form);

  for (;;) {
    if (model->state != QUICK_ONBOARD_STATE_HOST_FIELDS) {
      return model->state == QUICK_ONBOARD_STATE_COMPLETE;
    }
    if (!host_collect(state, title, &form)) {
      host_copy_form_to_model(model, &form);
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_CANCEL);
      return false;
    }
    host_copy_form_to_model(model, &form);

    char validation_message[200];
    if (!host_validate(&form, validation_message, sizeof(validation_message))) {
      quick_onboarding_set_validation(model, APP_ERROR_VALIDATION,
                                      validation_message);
      tui_show_message(title, validation_message);
      continue;
    }
    quick_onboarding_clear_validation(model);
    if (!quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_NEXT)) {
      return false;
    }

    quick_host_review_state_t review = {
        .form = &form,
        .title = do_install ? "Review install plan" : "Review host connection",
        .action_hint = do_install ? " --execute" : "",
        .url_behavior = host_url_behavior(&form),
        .unsafe_public_candidate = strcmp(form.iap, "none") == 0 &&
                                   form.domain[0] != '\0' &&
                                   !quick_domain_is_loopback(form.domain),
        .confirmed = false};
    (void)tui_modal_run(18, 78, title, host_review_redraw, host_review_key,
                        &review);
    if (!review.confirmed) {
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_BACK);
      continue;
    }
    if (review.unsafe_public_candidate &&
        !tui_confirm("Unsafe public host",
                     "No authentication protects this public domain. "
                     "Continue anyway?")) {
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_BACK);
      continue;
    }
    const bool allow_public_unsafe = review.unsafe_public_candidate;
    if (!quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_CONFIRM)) {
      return false;
    }

    quick_iap_config_t iap;
    host_build_iap(&form, &iap);

    if (do_install) {
      (void)quick_onboarding_transition(model,
                                        QUICK_ONBOARD_EVENT_BEGIN_MUTATION);
      quick_install_result_t result;
      quick_install_result_init(&result);
      app_error err =
          host_run_install(state, &form, allow_public_unsafe, &result);
      quick_onboarding_note_install_result(model, &result);
      if (err == APP_SUCCESS && result.completed) {
        for (;;) {
          app_error write_err = quick_op_serve_write_profile(
              form.profile, form.host, form.remote_root,
              form.domain[0] ? form.domain : NULL, &iap);
          if (write_err == APP_SUCCESS) {
            (void)quick_tui_reload_profiles(state);
            (void)quick_onboarding_transition(model,
                                              QUICK_ONBOARD_EVENT_COMPLETE);
            host_show_install_success(&form, &result);
            quick_install_result_destroy(&result);
            return true;
          }
          quick_onboarding_set_validation(
              model, write_err,
              "The host is installed, but its local profile could not be "
              "saved. Check config permissions and retry saving.");
          host_show_profile_save_failure(title, &form, write_err);
          if (!tui_confirm(title, "Retry saving this profile?")) {
            (void)quick_onboarding_transition(
                model, QUICK_ONBOARD_EVENT_VERIFY_FAILED);
            quick_install_result_destroy(&result);
            return false;
          }
        }
      }

      if (err == APP_ERROR_INTERRUPTED || result.cancelled) {
        host_show_install_cancellation(title, &result);
      } else {
        host_show_install_failure(err, &result);
      }
      bool safe_retry = model->state == QUICK_ONBOARD_STATE_FAILED &&
                        host_retry_is_safe(model);
      bool retry =
          safe_retry && tui_confirm(title, "Edit the settings and try again?");
      quick_install_result_destroy(&result);
      if (retry &&
          quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_RETRY)) {
        continue;
      }
      return false;
    }

    /* Connect (no install): verify an in-memory candidate before persisting. */
    tui_progress_t *progress = tui_progress_create("Checking host", 100);
    if (progress) {
      tui_progress_update(progress, 20, "connecting over SSH...");
    }

    quick_doctor_result_t doctor_result;
    quick_doctor_result_init(&doctor_result);
    quick_profile_config_t verification_profiles;
    app_error doctor_err = host_build_verification_profiles(
        state, &form, &iap, &verification_profiles);
    if (doctor_err == APP_SUCCESS) {
      quick_doctor_request_t doctor_request = {
          .profiles = &verification_profiles,
          .profile = form.profile,
          .site = NULL,
          .remote = true,
          .deep = false,
          .non_interactive = true,
          .connect_timeout_seconds = 10,
          .cancel_flag = tui_interrupt_flag(),
      };
      doctor_err = quick_op_doctor(&doctor_request, &doctor_result);
    }
    quick_profile_config_destroy(&verification_profiles);
    host_note_doctor_checks(model, doctor_err, &doctor_result);

    if (progress) {
      tui_progress_update(progress, 100,
                          doctor_err == APP_SUCCESS ? "done" : "stopped");
      tui_progress_destroy(progress);
    }
    if (doctor_err == APP_ERROR_INTERRUPTED || tui_interrupted()) {
      quick_doctor_result_destroy(&doctor_result);
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_CANCEL);
      tui_show_message(title,
                       "Host verification was cancelled. Nothing was saved.");
      return false;
    }

    bool healthy = doctor_err == APP_SUCCESS && doctor_result.ok;
    if (healthy) {
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_VERIFY_OK);
    } else {
      host_show_verification_failure(doctor_err, &doctor_result);
      (void)quick_onboarding_transition(model,
                                        QUICK_ONBOARD_EVENT_VERIFY_FAILED);
    }
    quick_doctor_result_destroy(&doctor_result);

    if (!healthy) {
      if (!tui_confirm(title, "Advanced override: Save anyway?")) {
        tui_show_message(
            title, "Nothing was saved. Your entered values are unchanged.");
        if (quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_RETRY)) {
          continue;
        }
        return false;
      }
      if (!quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_VERIFY_OK)) {
        return false;
      }
    }

    app_error write_err =
        quick_op_serve_write_profile(form.profile, form.host, form.remote_root,
                                     form.domain[0] ? form.domain : NULL, &iap);
    if (write_err != APP_SUCCESS) {
      quick_onboarding_set_validation(model, write_err,
                                      "The profile could not be saved.");
      (void)quick_onboarding_transition(model,
                                        QUICK_ONBOARD_EVENT_VERIFY_FAILED);
      host_show_profile_save_failure(title, &form, write_err);
      if (tui_confirm(title, "Edit the settings and try again?") &&
          quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_RETRY)) {
        continue;
      }
      return false;
    }

    (void)quick_tui_reload_profiles(state);
    (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_COMPLETE);
    char message[500];
    if (healthy) {
      snprintf(message, sizeof(message),
               "Saved profile \"%s\" and verified the host.\nYou can deploy to "
               "it now.",
               form.profile);
    } else {
      snprintf(message, sizeof(message),
               "Saved unverified profile \"%s\" by advanced override.\nRun "
               "Doctor again when the host is reachable.",
               form.profile);
    }
    tui_show_message(title, message);
    return true;
  }
}

bool quick_tui_screen_connect_host(quick_tui_app_state_t *state) {
  return host_setup_flow(state, false);
}

bool quick_tui_screen_new_host(quick_tui_app_state_t *state) {
  return host_setup_flow(state, true);
}
