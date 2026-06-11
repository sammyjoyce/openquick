#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: .template/replacer.sh [--dry-run] [-v]

  --dry-run    Show the edits that would be made without writing files
  -v           Verbose logging
  -h, --help   Show this message
EOF
}

dry_run=0
verbose=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)
            dry_run=1
            ;;
        -v|--verbose)
            verbose=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown option '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

for dep in jq sd; do
    if ! command -v "$dep" >/dev/null 2>&1; then
        echo "Error: '$dep' is required. Please install it and re-run." >&2
        exit 1
    fi
done

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
vars_json="$script_dir/template-vars.json"

if [[ ! -f "$vars_json" ]]; then
    echo "Error: cannot find template variables at '$vars_json'." >&2
    exit 1
fi

declare -A VAR_VALUES=()
declare -A VAR_OVERRIDDEN=()
declare -A VAR_TRANSFORM=()
declare -A VAR_VALIDATION=()
declare -A VAR_REQUIRED=()
declare -A PLACEHOLDER_MAP=()
declare -a VAR_KEYS=()

die() {
    printf 'Error: %s\n' "$1" >&2
    exit 1
}

detect_repository_url() {
    git -C "$repo_root" config --get remote.origin.url 2>/dev/null || true
}

detect_repository_name() {
    local url name
    url=$(detect_repository_url)
    if [[ -n $url ]]; then
        name=${url##*/}
        name=${name%.git}
        if [[ -n $name ]]; then
            printf '%s\n' "$name"
            return
        fi
    fi
    basename "$repo_root"
}

detect_repository_owner() {
    local url path
    url=$(detect_repository_url)
    case "$url" in
        git@github.com:*)
            path=${url#git@github.com:}
            ;;
        https://github.com/*)
            path=${url#https://github.com/}
            ;;
        http://github.com/*)
            path=${url#http://github.com/}
            ;;
        *)
            return
            ;;
    esac
    path=${path%.git}
    [[ $path == */* ]] || return
    printf '%s\n' "${path%%/*}"
}

detect_repository_web_url() {
    local url path
    url=$(detect_repository_url)
    case "$url" in
        git@github.com:*)
            path=${url#git@github.com:}
            ;;
        https://github.com/*)
            path=${url#https://github.com/}
            ;;
        http://github.com/*)
            path=${url#http://github.com/}
            ;;
        *)
            return
            ;;
    esac
    path=${path%.git}
    [[ $path == */* ]] || return
    printf 'https://github.com/%s\n' "$path"
}

detect_license() {
    if [[ -f "$repo_root/LICENSE" ]] && grep -qi 'MIT License' "$repo_root/LICENSE"; then
        printf 'MIT\n'
    fi
}

detect_source() {
    case "$1" in
        repository_name)
            detect_repository_name
            ;;
        repository_owner)
            detect_repository_owner
            ;;
        repository_url)
            detect_repository_web_url
            ;;
        git_user_name)
            git -C "$repo_root" config user.name 2>/dev/null || true
            ;;
        git_user_email)
            git -C "$repo_root" config user.email 2>/dev/null || true
            ;;
        current_year)
            date +%Y
            ;;
        license)
            detect_license
            ;;
        repository_description|static)
            return
            ;;
    esac
}

detect_from_sources() {
    local entry="$1"
    local source value
    while IFS= read -r source; do
        [[ -z $source || $source == "null" ]] && continue
        value=$(detect_source "$source")
        if [[ -n $value ]]; then
            printf '%s\n' "$value"
            return
        fi
    done < <(jq -r '(.value.sources // [])[]?' <<<"$entry")
}

normalize_key() {
    local key="$1"
    key=${key//[^A-Za-z0-9_]/_}
    key=${key^^}
    key=${key//__/_}
    key=${key#_}
    key=${key%_}
    printf '%s\n' "$key"
}

render_template() {
    local result="$1"
    local k
    for k in "${!VAR_VALUES[@]}"; do
        local placeholder="\${$k}"
        result=${result//"$placeholder"/${VAR_VALUES[$k]}}
    done
    printf '%s\n' "$result"
}

while IFS= read -r entry; do
    raw_key=$(jq -r '.key' <<<"$entry")
    safe_key=$(normalize_key "$raw_key")
    [[ -z $safe_key ]] && continue

    VAR_KEYS+=("$safe_key")
    VAR_TRANSFORM[$safe_key]=$(jq -r '.value.transform // empty' <<<"$entry")
    VAR_VALIDATION[$safe_key]=$(jq -r '.value.validation // empty' <<<"$entry")
    VAR_REQUIRED[$safe_key]=$(jq -r 'if .value.required == false then "0" else "1" end' <<<"$entry")

    default_value=$(jq -r '(.value.value // .value.fallback // empty)' <<<"$entry")
    [[ $default_value == "null" ]] && default_value=""
    if [[ -z $default_value ]]; then
        default_value=$(jq -r '(.value.placeholders[0] // empty)' <<<"$entry")
        [[ $default_value == "null" ]] && default_value=""
    fi

    detected_value=$(detect_from_sources "$entry")

    if [[ -n ${!safe_key+x} ]]; then
        value="${!safe_key}"
        VAR_OVERRIDDEN[$safe_key]=1
    elif [[ -n $detected_value ]]; then
        value="$detected_value"
    else
        value="$default_value"
    fi

    VAR_VALUES[$safe_key]="$value"

    while IFS= read -r placeholder; do
        [[ -z $placeholder || $placeholder == "null" ]] && continue
        PLACEHOLDER_MAP[$placeholder]="$safe_key"
    done < <(jq -r '(.value.placeholders // [])[]?' <<<"$entry")
done < <(jq -c '(.variables // {}) | to_entries[]' "$vars_json")

to_words() {
    local text="$1"
    text=${text//_/ }
    text=$(printf '%s' "$text" | sed -E 's/([a-z0-9])([A-Z])/\1 \2/g')
    text=$(printf '%s' "$text" | tr '[:upper:]' '[:lower:]')
    text=$(printf '%s' "$text" | tr -cs '[:alnum:]' ' ')
    printf '%s' "$text" | xargs
}

to_snake() {
    local words
    words="$(to_words "$1")"
    [[ -z $words ]] && return
    printf '%s\n' "${words// /_}"
}

to_kebab() {
    local words
    words="$(to_words "$1")"
    [[ -z $words ]] && return
    printf '%s\n' "${words// /-}"
}

to_pascal() {
    local words
    words="$(to_words "$1")"
    [[ -z $words ]] && return
    local part result=""
    for part in $words; do
        result+="$(printf '%s' "${part:0:1}" | tr '[:lower:]' '[:upper:]')"
        if [[ ${#part} -gt 1 ]]; then
            result+="$(printf '%s' "${part:1}" | tr '[:upper:]' '[:lower:]')"
        fi
    done
    printf '%s\n' "$result"
}

apply_transform() {
    local transform="$1"
    local value="$2"
    case "$transform" in
        snake_case) to_snake "$value" ;;
        kebab_case) to_kebab "$value" ;;
        pascal_case) to_pascal "$value" ;;
        lower_case) printf '%s\n' "${value,,}" ;;
        upper_case) printf '%s\n' "${value^^}" ;;
        *) printf '%s\n' "$value" ;;
    esac
}

for key in "${VAR_KEYS[@]}"; do
    [[ -n ${VAR_OVERRIDDEN[$key]:-} ]] && continue
    transform=${VAR_TRANSFORM[$key]:-}
    [[ -z $transform ]] && continue
    source="${VAR_VALUES[$key]}"
    if [[ $key == PROJECT_NAME_* && $key != PROJECT_NAME ]]; then
        source="${VAR_VALUES[PROJECT_NAME]:-$source}"
    elif [[ $key == AUTHOR_NAME_* && $key != AUTHOR_NAME ]]; then
        source="${VAR_VALUES[AUTHOR_NAME]:-$source}"
    fi
    VAR_VALUES[$key]="$(apply_transform "$transform" "$source")"
done

validate_value() {
    local key="$1"
    local value="${VAR_VALUES[$key]:-}"
    local regex="${VAR_VALIDATION[$key]:-}"
    local required="${VAR_REQUIRED[$key]:-1}"

    if [[ -z $value ]]; then
        if [[ $required == 1 ]]; then
            die "$key is required but resolved to an empty value."
        fi
        return
    fi

    if [[ -n $regex && ! $value =~ $regex ]]; then
        die "$key='$value' does not match validation regex: $regex"
    fi
}

for key in "${VAR_KEYS[@]}"; do
    validate_value "$key"
done

if [[ ${VAR_VALUES[PROJECT_URL]:-} == "https://github.com/yourusername/yourproject" &&
      -n ${VAR_VALUES[GITHUB_USERNAME]:-} &&
      -n ${VAR_VALUES[PROJECT_NAME_KEBAB]:-} ]]; then
    VAR_VALUES[PROJECT_URL]="https://github.com/${VAR_VALUES[GITHUB_USERNAME]}/${VAR_VALUES[PROJECT_NAME_KEBAB]}"
fi

if (( verbose )); then
    for key in "${VAR_KEYS[@]}"; do
        printf '[replacer] %s=%s\n' "$key" "${VAR_VALUES[$key]:-}" >&2
    done
fi

declare -A SPECIAL_MAP=()

while IFS=$'\t' read -r file_key find_value replace_value; do
    [[ -z $file_key ]] && continue
    file_key=${file_key#./}
    file_key=$(render_template "$file_key")
    find_value=$(render_template "$find_value")
    replace_value=$(render_template "$replace_value")
    SPECIAL_MAP[$file_key]+="$find_value"$'\t'"$replace_value"$'\n'
done < <(jq -r '(.special_replacements // {})
    | to_entries[]
    | .key as $file
    | .value
    | to_entries[]
    | [$file, .key, .value] | @tsv' "$vars_json")

declare -A GENERIC_MAP=()
GENERIC_ORDER=()

add_generic() {
    local pattern="$1"
    local replacement="$2"
    [[ -z $pattern || -z $replacement ]] && return
    [[ $pattern == "$replacement" ]] && return
    if [[ -z ${GENERIC_MAP[$pattern]+x} ]]; then
        GENERIC_ORDER+=("$pattern")
    fi
    GENERIC_MAP[$pattern]="$replacement"
}

for pattern in "${!PLACEHOLDER_MAP[@]}"; do
    key=${PLACEHOLDER_MAP[$pattern]}
    add_generic "$pattern" "${VAR_VALUES[$key]:-}"
done

# Generic replacements come only from placeholders declared in template-vars.json.
# Add new placeholders there rather than growing an ad-hoc broad replacement list.
project_kebab="${VAR_VALUES[PROJECT_NAME_KEBAB]:-}"
# shellcheck disable=SC2088 # Literal documentation/config placeholder.
[[ -n $project_kebab ]] && add_generic "~/.config/myapp" "~/.config/$project_kebab"

sort_generic_order() {
    local sorted=()
    while IFS= read -r pattern; do
        sorted+=("$pattern")
    done < <(
        for pattern in "${GENERIC_ORDER[@]}"; do
            printf '%06d\t%s\n' "${#pattern}" "$pattern"
        done | sort -r -n -k1,1 | cut -f2-
    )
    GENERIC_ORDER=("${sorted[@]}")
}

sort_generic_order

readarray -t FILE_PATTERNS < <(jq -r '(.file_patterns // [])[]?' "$vars_json")
readarray -t EXCLUDE_PATTERNS < <(jq -r '(.exclude_patterns // [])[]?' "$vars_json")

FILES=()

collect_files() {
    local path_list=()
    pushd "$repo_root" >/dev/null

    local find_cmd=(find . -type f)
    if ((${#FILE_PATTERNS[@]})); then
        find_cmd+=('(')
        for i in "${!FILE_PATTERNS[@]}"; do
            [[ $i -gt 0 ]] && find_cmd+=('-o')
            find_cmd+=('-name' "${FILE_PATTERNS[$i]}")
        done
        find_cmd+=(')')
    fi

    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        [[ -z $pattern ]] && continue
        if [[ $pattern == */* ]]; then
            find_cmd+=("-not" "-path" "./$pattern")
        else
            find_cmd+=("-not" "-name" "$pattern")
        fi
    done

    find_cmd+=("-print")

    mapfile -t path_list < <("${find_cmd[@]}")
    popd >/dev/null

    declare -A seen=()
    FILES=()

    local rel
    for rel in "${path_list[@]}"; do
        [[ -z $rel ]] && continue
        rel=${rel#./}
        [[ -f "$repo_root/$rel" ]] || continue
        if [[ -z ${seen[$rel]+x} ]]; then
            seen[$rel]=1
            FILES+=("$rel")
        fi
    done

    for rel in "${!SPECIAL_MAP[@]}"; do
        [[ -z $rel ]] && continue
        if [[ -f "$repo_root/$rel" && -z ${seen[$rel]+x} ]]; then
            seen[$rel]=1
            FILES+=("$rel")
        fi
    done
}

collect_files

apply_replacement() {
    local file="$1"
    local pattern="$2"
    local replacement="$3"

    [[ -z $pattern ]] && return 1
    [[ $pattern == "$replacement" ]] && return 1
    if ! grep -qF -- "$pattern" "$file"; then
        return 1
    fi

    if (( dry_run )); then
        (( verbose )) && printf '[dry-run] %s: "%s" -> "%s"\n' "$file" "$pattern" "$replacement"
        sd --preview -F "$pattern" "$replacement" "$file"
    else
        (( verbose )) && printf '%s: "%s" -> "%s"\n' "$file" "$pattern" "$replacement"
        sd -F "$pattern" "$replacement" "$file"
    fi
    return 0
}

changed=()
unchanged=()

for rel in "${FILES[@]}"; do
    file_path="$repo_root/$rel"
    [[ -f $file_path ]] || continue
    if ! LC_ALL=C grep -Iq . "$file_path" 2>/dev/null; then
        (( verbose )) && printf 'Skipping binary file %s\n' "$rel" >&2
        continue
    fi
    file_changed=0

    if [[ -n ${SPECIAL_MAP[$rel]+x} ]]; then
        while IFS=$'\t' read -r pattern replacement; do
            [[ -z $pattern ]] && continue
            if apply_replacement "$file_path" "$pattern" "$replacement"; then
                file_changed=1
            fi
        done <<<"${SPECIAL_MAP[$rel]}"
    fi

    for pattern in "${GENERIC_ORDER[@]}"; do
        replacement="${GENERIC_MAP[$pattern]}"
        if apply_replacement "$file_path" "$pattern" "$replacement"; then
            file_changed=1
        fi
    done

    if (( file_changed )); then
        changed+=("$rel")
    else
        unchanged+=("$rel")
    fi
done

run_label="Replacement run complete"
(( dry_run )) && run_label+=" (dry run only)"
printf '%s\n' "$run_label"
printf 'Files changed: %d\n' "${#changed[@]}"
if ((${#changed[@]})); then
    printf '  %s\n' "${changed[@]}"
fi
printf 'Files unchanged: %d\n' "${#unchanged[@]}"
(( verbose )) && ((${#unchanged[@]})) && printf '  %s\n' "${unchanged[@]}"

exit 0
