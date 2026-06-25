/*
 * Command registration and dispatch.
 */

#include "commands.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../io/output.h"
#include "option_meta.h"

// Forward declarations for handlers defined in their own translation units.
app_error app_cmd_init(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_templates(const app_config_t *config, int argc,
                            char *const argv[]);
app_error app_cmd_deploy(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_serve(const app_config_t *config, int argc,
                        char *const argv[]);
app_error app_cmd_open(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_list(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_config_show(const app_config_t *config, int argc,
                              char *const argv[]);
app_error app_cmd_info(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_doctor(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_delete(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_restore(const app_config_t *config, int argc,
                          char *const argv[]);
app_error app_cmd_rollback(const app_config_t *config, int argc,
                           char *const argv[]);
app_error app_cmd_public(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_domain(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_menu(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_opencli(const app_config_t *config, int argc,
                          char *const argv[]);

static const app_command_arg_t optional_path_args[] = {
    {.name = "path",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = APP_ARG_ARITY_UNBOUNDED,
     .description = "Path and command-specific option values"},
};

static const app_command_arg_t optional_site_args[] = {
    {.name = "site",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = APP_ARG_ARITY_UNBOUNDED,
     .description = "Site name and command-specific option values"},
};

static const app_command_arg_t delete_args[] = {
    {.name = "site",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site slug to delete"},
};

static const app_command_arg_t restore_args[] = {
    {.name = "site",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site slug to restore"},
};

static const app_command_arg_t rollback_args[] = {
    {.name = "site",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site slug to roll back"},
};

static const app_command_arg_t public_args[] = {
    {.name = "site",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site slug"},
    {.name = "state",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = 1,
     .description = "on or off"},
};

static const app_command_arg_t domain_args[] = {
    {.name = "action",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "add, remove, or list"},
    {.name = "domain",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = 1,
     .description = "Custom domain for add/remove"},
};

static const app_command_arg_t config_args[] = {
    {.name = "action",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "show"},
};

static const app_command_arg_t serve_args[] = {
    {.name = "mode",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = APP_ARG_ARITY_UNBOUNDED,
     .description = "--dev or install with command-specific option values"},
};

static const app_command_arg_t config_option_args[] = {
    {.name = "path",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Path to configuration file"},
};

static const app_command_arg_t site_option_args[] = {
    {.name = "site",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site slug"},
};

static const app_command_arg_t subdomain_option_args[] = {
    {.name = "subdomain",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "URL subdomain"},
};

static const app_command_arg_t profile_option_args[] = {
    {.name = "profile",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Deployment profile"},
};

static const app_command_arg_t path_option_args[] = {
    {.name = "path",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Filesystem path"},
};

static const app_command_arg_t release_option_args[] = {
    {.name = "release",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Release id"},
};

static const app_command_arg_t template_option_args[] = {
    {.name = "template",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Scaffold template"},
};

static const app_command_arg_t name_option_args[] = {
    {.name = "name",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site name"},
};

static const app_command_arg_t host_option_args[] = {
    {.name = "host",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "SSH host"},
};

static const app_command_arg_t filter_option_args[] = {
    {.name = "query",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Text to match against site rows"},
};

static const app_command_arg_t sort_option_args[] = {
    {.name = "name|updated|source",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Site list sort key"},
};

static const app_command_arg_t domain_option_args[] = {
    {.name = "domain",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Public base domain"},
};

static const app_command_arg_t iap_option_args[] = {
    {.name = "iap",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "IAP adapter"},
};

static const app_command_arg_t port_option_args[] = {
    {.name = "port",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "TCP port"},
};

static const app_command_arg_t identity_option_args[] = {
    {.name = "email",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Synthetic identity email"},
};

static const app_command_option_t init_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "template",
     .arguments = template_option_args,
     .argument_count = APP_COUNTOF(template_option_args),
     .description = "Scaffold template: blank or realtime"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "name",
     .arguments = name_option_args,
     .argument_count = APP_COUNTOF(name_option_args),
     .description = "Site name to write into quick.json"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile to record in quick.json"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "adopt",
     .description = "Adopt an existing folder by writing only missing OpenQuick metadata"},
};

static const app_command_option_t deploy_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "site",
     .arguments = site_option_args,
     .argument_count = APP_COUNTOF(site_option_args),
     .description = "Override the site slug"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "subdomain",
     .arguments = subdomain_option_args,
     .argument_count = APP_COUNTOF(subdomain_option_args),
     .description = "Override the URL subdomain"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "dry-run",
     .description = "Print the resolved plan without remote operations"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "no-build",
     .description = "Skip the quick.json build command"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "no-delete",
     .description = "Do not mirror deletions into staging"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "open",
     .description = "Open the deployed URL after activation"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "bootstrap",
     .description = "Allow bootstrap flow when quickd is missing"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "allow-unpublished",
     .description = "Allow deploy when IAP/domain publication is incomplete"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "checksum",
     .description = "Pass --checksum to rsync"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "yes",
     .description = "Skip typed overwrite confirmation"},
};

static const app_command_option_t serve_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "dev",
     .description = "Run quickd in local dev mode"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "port",
     .arguments = port_option_args,
     .argument_count = APP_COUNTOF(port_option_args),
     .description = "Local dev port"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "identity",
     .arguments = identity_option_args,
     .argument_count = APP_COUNTOF(identity_option_args),
     .description = "Synthetic dev identity email"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "remote-api",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Remote API profile for local dev proxy"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Profile name for install/dev"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "host",
     .arguments = host_option_args,
     .argument_count = APP_COUNTOF(host_option_args),
     .description = "SSH host for install"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "remote-root",
     .arguments = path_option_args,
     .argument_count = APP_COUNTOF(path_option_args),
     .description = "Remote root directory"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "domain",
     .arguments = domain_option_args,
     .argument_count = APP_COUNTOF(domain_option_args),
     .description = "Public base domain"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "iap",
     .arguments = iap_option_args,
     .argument_count = APP_COUNTOF(iap_option_args),
     .description = "IAP adapter: tailscale, tailscale-tsnet, cloudflare, or none"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "execute",
     .description = "Run installer probes over SSH"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "allow-public-unsafe",
     .description = "Allow iap=none for non-loopback installs"},
};

static const app_command_option_t open_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Profile used for URL resolution"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "copy",
     .description = "Copy the URL when clipboard support is available"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "plain",
     .description = "Print the URL instead of opening a browser"},
};

static const app_command_option_t list_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Profile to query"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "remote",
     .description = "Query quickd over SSH"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "filter",
     .arguments = filter_option_args,
     .argument_count = APP_COUNTOF(filter_option_args),
     .description = "Only show rows matching text"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "sort",
     .arguments = sort_option_args,
     .argument_count = APP_COUNTOF(sort_option_args),
     .description = "Sort rows by name, updated, or source"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t config_show_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Profile used for resolution"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "site",
     .arguments = site_option_args,
     .argument_count = APP_COUNTOF(site_option_args),
     .description = "Override the site slug"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "subdomain",
     .arguments = subdomain_option_args,
     .argument_count = APP_COUNTOF(subdomain_option_args),
     .description = "Override the URL subdomain"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t delete_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "yes",
     .description = "Skip typed delete confirmation"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t restore_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "from",
     .arguments = path_option_args,
     .argument_count = APP_COUNTOF(path_option_args),
     .description = "Archive path returned by quick delete"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "yes",
     .description = "Skip typed restore confirmation"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t rollback_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "to",
     .arguments = release_option_args,
     .argument_count = APP_COUNTOF(release_option_args),
     .description = "Release id to restore; defaults to previous"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "yes",
     .description = "Skip typed rollback confirmation"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t public_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "yes",
     .description = "Skip typed public-on confirmation"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t domain_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "site",
     .arguments = site_option_args,
     .argument_count = APP_COUNTOF(site_option_args),
     .description = "Site slug for domain add"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Deployment profile"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const app_command_option_t doctor_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .arguments = profile_option_args,
     .argument_count = APP_COUNTOF(profile_option_args),
     .description = "Profile to check"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "remote",
     .description = "Include quickd host checks"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "site",
     .arguments = site_option_args,
     .argument_count = APP_COUNTOF(site_option_args),
     .description = "Site to check"},
    {.id = APP_COMMAND_OPTION_DOCTOR_DEEP,
     .name = "deep",
     .description = "Run deeper end-to-end checks when supported"},
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "json",
     .description = "Print JSON output"},
};

static const char *const init_examples[] = {
    APP_NAME " init lunch-vote",
    APP_NAME " init --template realtime --name lunch-vote --profile lab",
};

static const char *const templates_examples[] = {
    APP_NAME " templates",
    APP_NAME " init --template list",
};

static const char *const deploy_examples[] = {
    APP_NAME " deploy",
    APP_NAME " deploy --dry-run",
    APP_NAME " deploy ./dist --site demo --profile lab",
};

static const char *const serve_examples[] = {
    APP_NAME " serve --dev --port 9366",
    APP_NAME " serve --dev --remote-api lab",
    APP_NAME " serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale",
};

static const char *const open_examples[] = {
    APP_NAME " open",
    APP_NAME " open lunch-vote --plain",
};

static const char *const list_examples[] = {
    APP_NAME " list",
    APP_NAME " list --profile lab --json",
    APP_NAME " list --remote --filter demo --sort updated",
};

static const char *const config_examples[] = {
    APP_NAME " config show",
    APP_NAME " config show --profile lab --json",
};

static const char *const info_examples[] = {
    APP_NAME " info",
    APP_NAME " --json info",
};

static const char *const doctor_examples[] = {
    APP_NAME " doctor",
    APP_NAME " doctor --json",
};

static const char *const delete_examples[] = {
    APP_NAME " delete demo",
    APP_NAME " delete demo --profile lab --yes --json",
};

static const char *const restore_examples[] = {
    APP_NAME " restore demo --from /srv/quick/.trash/sites/demo-20260612T000000.000000000Z",
    APP_NAME " restore demo --from /srv/quick/.trash/sites/demo-20260612T000000.000000000Z --yes --json",
};

static const char *const rollback_examples[] = {
    APP_NAME " rollback demo",
    APP_NAME " rollback demo --to 20260612T000000Z-abcdef --yes --json",
};

static const char *const public_examples[] = {
    APP_NAME " public demo",
    APP_NAME " public demo on --yes",
    APP_NAME " public demo off",
};

static const char *const domain_examples[] = {
    APP_NAME " domain list",
    APP_NAME " domain add app.example.com --site demo",
    APP_NAME " domain remove app.example.com",
};

static const char *const menu_examples[] = {
    APP_NAME " menu",
};

static const char *const opencli_examples[] = {
    APP_NAME " opencli",
};

static const app_builtin_option_t g_app_builtin_options[] = {
    {.id = APP_BUILTIN_OPTION_HELP,
     .name = "help",
     .alias = "h",
     .description = "Show help message and exit"},
    {.id = APP_BUILTIN_OPTION_VERSION,
     .name = "version",
     .alias = NULL,
     .description = "Show version information and exit"},
};

static const app_global_value_option_t g_app_global_value_options[] = {
    {.id = APP_GLOBAL_VALUE_OPTION_CONFIG,
     .name = "config",
     .alias = "c",
     .arguments = config_option_args,
     .argument_count = APP_COUNTOF(config_option_args),
     .description = "Specify configuration file path"},
};

static const app_command_t g_app_commands[] = {
    {.name = "init",
     .summary = "Scaffold a static OpenQuick site.",
     .handler = app_cmd_init,
     .options = init_options,
     .option_count = APP_COUNTOF(init_options),
     .arguments = optional_path_args,
     .argument_count = APP_COUNTOF(optional_path_args),
     .examples = init_examples,
     .example_count = APP_COUNTOF(init_examples),
     .requires_terminal = false},
    {.name = "templates",
     .summary = "List bundled OpenQuick site templates.",
     .handler = app_cmd_templates,
     .examples = templates_examples,
     .example_count = APP_COUNTOF(templates_examples),
     .requires_terminal = false},
    {.name = "deploy",
     .summary = "Build, rsync, and activate a site through quickd.",
     .handler = app_cmd_deploy,
     .options = deploy_options,
     .option_count = APP_COUNTOF(deploy_options),
     .arguments = optional_path_args,
     .argument_count = APP_COUNTOF(optional_path_args),
     .examples = deploy_examples,
     .example_count = APP_COUNTOF(deploy_examples),
     .requires_terminal = false},
    {.name = "serve",
     .summary = "Run local dev quickd or print/install host setup steps.",
     .handler = app_cmd_serve,
     .options = serve_options,
     .option_count = APP_COUNTOF(serve_options),
     .arguments = serve_args,
     .argument_count = APP_COUNTOF(serve_args),
     .examples = serve_examples,
     .example_count = APP_COUNTOF(serve_examples),
     .requires_terminal = false},
    {.name = "open",
     .summary = "Open or print the resolved site URL.",
     .handler = app_cmd_open,
     .options = open_options,
     .option_count = APP_COUNTOF(open_options),
     .arguments = optional_site_args,
     .argument_count = APP_COUNTOF(optional_site_args),
     .examples = open_examples,
     .example_count = APP_COUNTOF(open_examples),
     .requires_terminal = false},
    {.name = "list",
     .summary = "List local and remote OpenQuick deployments.",
     .handler = app_cmd_list,
     .options = list_options,
     .option_count = APP_COUNTOF(list_options),
     .examples = list_examples,
     .example_count = APP_COUNTOF(list_examples),
     .requires_terminal = false},
    {.name = "config",
     .summary = "Show resolved OpenQuick config, profile, and target details.",
     .handler = app_cmd_config_show,
     .options = config_show_options,
     .option_count = APP_COUNTOF(config_show_options),
     .arguments = config_args,
     .argument_count = APP_COUNTOF(config_args),
     .examples = config_examples,
     .example_count = APP_COUNTOF(config_examples),
     .requires_terminal = false},
    {.name = "info",
     .summary = "Display application metadata.",
     .handler = app_cmd_info,
     .examples = info_examples,
     .example_count = APP_COUNTOF(info_examples),
     .requires_terminal = false},
    {.name = "doctor",
     .summary = "Run local, remote, and edge OpenQuick diagnostics.",
     .handler = app_cmd_doctor,
     .options = doctor_options,
     .option_count = APP_COUNTOF(doctor_options),
     .arguments = optional_site_args,
     .argument_count = APP_COUNTOF(optional_site_args),
     .examples = doctor_examples,
     .example_count = APP_COUNTOF(doctor_examples),
     .requires_terminal = false},
    {.name = "delete",
     .summary = "Delete a remote OpenQuick site after confirmation.",
     .handler = app_cmd_delete,
     .options = delete_options,
     .option_count = APP_COUNTOF(delete_options),
     .arguments = delete_args,
     .argument_count = APP_COUNTOF(delete_args),
     .examples = delete_examples,
     .example_count = APP_COUNTOF(delete_examples),
     .requires_terminal = false},
    {.name = "restore",
     .summary = "Restore a recently deleted OpenQuick site archive.",
     .handler = app_cmd_restore,
     .options = restore_options,
     .option_count = APP_COUNTOF(restore_options),
     .arguments = restore_args,
     .argument_count = APP_COUNTOF(restore_args),
     .examples = restore_examples,
     .example_count = APP_COUNTOF(restore_examples),
     .requires_terminal = false},
    {.name = "rollback",
     .summary = "Roll a remote OpenQuick site back to a previous release.",
     .handler = app_cmd_rollback,
     .options = rollback_options,
     .option_count = APP_COUNTOF(rollback_options),
     .arguments = rollback_args,
     .argument_count = APP_COUNTOF(rollback_args),
     .examples = rollback_examples,
     .example_count = APP_COUNTOF(rollback_examples),
     .requires_terminal = false},
    {.name = "public",
     .summary = "Show or change a site's public-static flag.",
     .handler = app_cmd_public,
     .options = public_options,
     .option_count = APP_COUNTOF(public_options),
     .arguments = public_args,
     .argument_count = APP_COUNTOF(public_args),
     .examples = public_examples,
     .example_count = APP_COUNTOF(public_examples),
     .requires_terminal = false},
    {.name = "domain",
     .summary = "Manage custom domains for remote sites.",
     .handler = app_cmd_domain,
     .options = domain_options,
     .option_count = APP_COUNTOF(domain_options),
     .arguments = domain_args,
     .argument_count = APP_COUNTOF(domain_args),
     .examples = domain_examples,
     .example_count = APP_COUNTOF(domain_examples),
     .requires_terminal = false},
    {.name = "menu",
     .summary = "Launch the interactive TUI main menu.",
     .handler = app_cmd_menu,
     .examples = menu_examples,
     .example_count = APP_COUNTOF(menu_examples),
     .requires_terminal = true,
     .hidden_from_help = true},
    {.name = "opencli",
     .summary = "Print the OpenCLI contract as JSON.",
     .handler = app_cmd_opencli,
     .examples = opencli_examples,
     .example_count = APP_COUNTOF(opencli_examples),
     .requires_terminal = false},
};

#define G_APP_COMMANDS_COUNT APP_COUNTOF(g_app_commands)
#define G_APP_BUILTIN_OPTIONS_COUNT APP_COUNTOF(g_app_builtin_options)
#define G_APP_GLOBAL_VALUE_OPTIONS_COUNT APP_COUNTOF(g_app_global_value_options)

// The dispatch and help paths assume at least one command and at least one
// built-in option exist; an empty table would make the template silently
// broken rather than fail to build.
static_assert(APP_COUNTOF(g_app_commands) > 0,
              "command table must not be empty");
static_assert(APP_COUNTOF(g_app_builtin_options) > 0,
              "built-in option table must not be empty");
static_assert(APP_COUNTOF(g_app_global_value_options) > 0,
              "global value option table must not be empty");

const app_command_t *app_commands(size_t *count) {
  if (count) {
    *count = G_APP_COMMANDS_COUNT;
  }
  return g_app_commands;
}

const app_builtin_option_t *app_builtin_options(size_t *count) {
  if (count) {
    *count = G_APP_BUILTIN_OPTIONS_COUNT;
  }
  return g_app_builtin_options;
}

const app_builtin_option_t *app_builtin_option_find(const char *arg) {
  if (!arg) {
    return NULL;
  }

  for (size_t i = 0; i < G_APP_BUILTIN_OPTIONS_COUNT; i++) {
    const app_builtin_option_t *option = &g_app_builtin_options[i];
    if (app_option_token_matches(arg, option->name, option->alias)) {
      return option;
    }
  }
  return NULL;
}

const app_global_value_option_t *app_global_value_options(size_t *count) {
  if (count) {
    *count = G_APP_GLOBAL_VALUE_OPTIONS_COUNT;
  }
  return g_app_global_value_options;
}

const app_global_value_option_t *app_global_value_option_find(const char *arg) {
  if (!arg) {
    return NULL;
  }

  for (size_t i = 0; i < G_APP_GLOBAL_VALUE_OPTIONS_COUNT; i++) {
    const app_global_value_option_t *option = &g_app_global_value_options[i];
    if (app_option_token_matches(arg, option->name, option->alias)) {
      return option;
    }
  }
  return NULL;
}

const app_command_t *app_command_find(const char *name) {
  if (!name) {
    return NULL;
  }
  for (size_t i = 0; i < G_APP_COMMANDS_COUNT; i++) {
    if (strcmp(g_app_commands[i].name, name) == 0) {
      return &g_app_commands[i];
    }
  }
  return NULL;
}

bool app_command_is_visible(const app_command_t *command) {
  // Single definition of "appears in human-facing listings" shared by root
  // --help (plain and styled) and the TUI Commands browser, so those surfaces
  // can never disagree on which commands a person sees. Hidden commands stay
  // dispatchable and remain in the OpenCLI contract (the machine surface lists
  // every command); only human discovery filters on this.
  return command && !command->hidden_from_help;
}

// Levenshtein edit distance between a and b. Bounded to short inputs (a real
// typo is short): anything longer is treated as far away so the suggestion gate
// rejects it. Command names are tiny, so the row buffers are small.
#define APP_SUGGEST_MAX_LEN 64

static size_t app_command_edit_distance(const char *a, const char *b) {
  const size_t la = strlen(a);
  const size_t lb = strlen(b);
  if (la > APP_SUGGEST_MAX_LEN || lb > APP_SUGGEST_MAX_LEN) {
    return SIZE_MAX;
  }

  size_t prev[APP_SUGGEST_MAX_LEN + 1];
  size_t curr[APP_SUGGEST_MAX_LEN + 1];
  for (size_t j = 0; j <= lb; j++) {
    prev[j] = j;
  }
  for (size_t i = 1; i <= la; i++) {
    curr[0] = i;
    for (size_t j = 1; j <= lb; j++) {
      const size_t cost = a[i - 1] == b[j - 1] ? 0U : 1U;
      const size_t deletion = prev[j] + 1U;
      const size_t insertion = curr[j - 1] + 1U;
      const size_t substitution = prev[j - 1] + cost;
      size_t best = deletion < insertion ? deletion : insertion;
      best = best < substitution ? best : substitution;
      curr[j] = best;
    }
    for (size_t j = 0; j <= lb; j++) {
      prev[j] = curr[j];
    }
  }
  return prev[lb];
}

const char *app_command_suggest(const char *unknown) {
  if (!unknown || unknown[0] == '\0') {
    return NULL;
  }
  const size_t unknown_len = strlen(unknown);

  const char *best = NULL;
  size_t best_distance = SIZE_MAX;
  for (size_t i = 0; i < G_APP_COMMANDS_COUNT; i++) {
    const app_command_t *command = &g_app_commands[i];
    if (command->hidden_from_help) {
      continue;  // never steer users toward an internal command
    }
    const size_t distance = app_command_edit_distance(unknown, command->name);
    if (distance < best_distance) {
      best_distance = distance;
      best = command->name;
    }
  }

  // Conservative gate: only a clearly-close match (within two edits AND under
  // half the typed length) suggests. Short, ambiguous tokens and far-off words
  // produce no hint rather than a misleading one.
  if (best && best_distance <= 2U && best_distance * 2U < unknown_len) {
    return best;
  }
  return NULL;
}

const app_command_option_t *app_command_option_find(
    const app_command_t *command, const char *arg) {
  if (!command || !arg || strncmp(arg, "--", 2) != 0 || arg[2] == '\0') {
    return NULL;
  }

  for (size_t i = 0; i < command->option_count; i++) {
    const app_command_option_t *option = &command->options[i];
    if (app_option_token_matches(arg, option->name, NULL)) {
      return option;
    }
  }
  return NULL;
}

static int app_command_min_positionals(const app_command_t *command) {
  int minimum = 0;
  if (!command) {
    return minimum;
  }

  for (size_t i = 0; i < command->argument_count; i++) {
    minimum += command->arguments[i].arity_minimum;
  }
  return minimum;
}

static int app_command_max_positionals(const app_command_t *command) {
  int maximum = 0;
  if (!command) {
    return maximum;
  }

  for (size_t i = 0; i < command->argument_count; i++) {
    if (command->arguments[i].arity_maximum == APP_ARG_ARITY_UNBOUNDED) {
      return APP_ARG_ARITY_UNBOUNDED;
    }
    maximum += command->arguments[i].arity_maximum;
  }
  return maximum;
}

static void app_command_report_validation_error(const app_config_t *config,
                                                const char *message,
                                                const char *hint) {
  const bool have_hint = hint && *hint;
  if (config && app_config_is_json_output(config)) {
    // Emit a single JSON object so stderr stays one parseable document, even
    // when a message has an accompanying hint. Other error paths emit one
    // object per error; folding the hint in keeps the headless/--json contract
    // consistent for consumers that parse stderr as a single document.
    if (have_hint) {
      char combined[544];
      snprintf(combined, sizeof(combined), "%s. %s", message, hint);
      app_output(combined, config, true);
    } else {
      app_output(message, config, true);
    }
  } else {
    fprintf(stderr, "%s\n", message);
    if (have_hint) {
      fprintf(stderr, "%s\n", hint);
    }
  }
}

app_error app_command_validate_invocation(const app_command_t *command,
                                          int argc, char *const argv[],
                                          const app_config_t *config,
                                          const char *program_name) {
  if (!command || argc < 0 || (argc > 0 && !argv)) {
    return APP_ERROR_INVALID_ARG;
  }

  char message[256];
  char hint[256];

  bool end_of_options = false;
  int positional_count = 0;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) {
      return APP_ERROR_INVALID_ARG;
    }

    if (!end_of_options && strcmp(arg, "--") == 0) {
      end_of_options = true;
      continue;
    }

    if (!end_of_options && strncmp(arg, "--", 2) == 0 && arg[2] != '\0') {
      const app_command_option_t *option = app_command_option_find(command, arg);
      if (option) {
        for (size_t j = 0; j < option->argument_count; j++) {
          if (i + 1 >= argc) {
            snprintf(message, sizeof(message),
                     "Error: Option '%s' for command '%s' expects a value",
                     arg, command->name);
            app_command_report_validation_error(config, message, NULL);
            return APP_ERROR_MISSING_ARG;
          }
          i++;
        }
        continue;
      }
      snprintf(message, sizeof(message),
               "Error: Unknown option '%s' for command '%s'", arg,
               command->name);
      snprintf(hint, sizeof(hint), "Run '%s --help' for usage information",
               program_name ? program_name : APP_NAME);
      app_command_report_validation_error(config, message, hint);
      return APP_ERROR_UNKNOWN_OPTION;
    }

    positional_count++;
  }

  const int minimum = app_command_min_positionals(command);
  const int maximum = app_command_max_positionals(command);
  if (positional_count < minimum) {
    snprintf(message, sizeof(message),
             "Error: Command '%s' expects at least %d argument%s",
             command->name, minimum, minimum == 1 ? "" : "s");
    app_command_report_validation_error(config, message, NULL);
    return APP_ERROR_MISSING_ARG;
  }
  if (maximum != APP_ARG_ARITY_UNBOUNDED && positional_count > maximum) {
    snprintf(message, sizeof(message),
             "Error: Command '%s' expects at most %d argument%s", command->name,
             maximum, maximum == 1 ? "" : "s");
    app_command_report_validation_error(config, message, NULL);
    return APP_ERROR_INVALID_ARG;
  }

  return APP_SUCCESS;
}
