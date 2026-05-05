"""
PlatformIO pre-build script for CPR-vCodex versioning.

Version scheme:
  - default/dev builds:  <base>.<release>.dev<dev_counter>
  - gh_release builds:   <base>.<release>

Release builds intentionally do not persist the release counter in this
pre-build script. The post-build packaging script advances the counter only
after firmware.bin exists, so failed release builds cannot burn release numbers.
"""

import configparser
import os
import re
import subprocess
import sys

COUNTER_DIR = "artifacts"
RELEASE_COUNTER_FILE = ".release-counter.txt"
DEV_COUNTER_FILE_TEMPLATE = ".dev-counter-r{release}.txt"
INITIAL_RELEASE_NUMBER = 2


def warn(msg):
    print(f"WARNING [git_branch.py]: {msg}", file=sys.stderr)


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, "platformio.ini")
    if not os.path.isfile(ini_path):
        warn(f"platformio.ini not found at {ini_path}; base version will be \"0.0.0\"")
        return "0.0.0"

    config = configparser.ConfigParser()
    config.read(ini_path)
    if not config.has_option("crosspoint", "version"):
        warn("No [crosspoint] version in platformio.ini; base version will be \"0.0.0\"")
        return "0.0.0"
    return config.get("crosspoint", "version")


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(["git", *args], text=True, stderr=subprocess.PIPE, cwd=project_dir).strip()
        return "".join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
    except subprocess.CalledProcessError as e:
        warn(f"git command failed (exit {e.returncode}): {e.stderr.strip()}; {label} suffix will be \"unknown\"")
    except OSError as e:
        warn(f'OS error reading git {label}: {e}; {label} suffix will be "unknown"')
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(f'Unexpected error reading git {label}: {e}; {label} suffix will be "unknown"')
    return "unknown"


def get_git_short_sha(project_dir):
    return run_git_value(project_dir, ["rev-parse", "--short", "HEAD"], "short SHA")


def _ensure_counter_dir(project_dir):
    counter_dir = os.path.join(project_dir, COUNTER_DIR)
    os.makedirs(counter_dir, exist_ok=True)
    return counter_dir


def _read_counter(counter_path, default_value):
    current_value = default_value
    if os.path.isfile(counter_path):
        try:
            with open(counter_path, "r", encoding="utf-8") as file:
                raw_value = file.read().strip()
            current_value = int(raw_value) if raw_value else default_value
        except ValueError:
            warn(f"Invalid counter in {counter_path}; using {default_value}")
        except OSError as e:
            warn(f"Failed to read counter {counter_path}: {e}; using {default_value}")
    return current_value


def _extract_release_number(base_version, text):
    match = re.search(rf"{re.escape(base_version)}\.(\d+)-cpr-vcodex", text)
    return int(match.group(1)) if match else None


def _latest_tag_release_number(project_dir, base_version):
    try:
        tags = subprocess.check_output(
            ["git", "tag", "--list", f"{base_version}.*-cpr-vcodex"],
            text=True,
            stderr=subprocess.PIPE,
            cwd=project_dir,
        ).splitlines()
    except FileNotFoundError:
        return None
    except subprocess.CalledProcessError as e:
        warn(f"git tag lookup failed (exit {e.returncode}): {e.stderr.strip()}")
        return None
    except OSError as e:
        warn(f"OS error reading git tags: {e}")
        return None

    release_numbers = []
    for tag in tags:
        release_number = _extract_release_number(base_version, tag)
        if release_number is not None:
            release_numbers.append(release_number)
    return max(release_numbers) if release_numbers else None


def _readme_release_number(project_dir, base_version):
    readme_path = os.path.join(project_dir, "README.md")
    if not os.path.isfile(readme_path):
        return None

    try:
        with open(readme_path, "r", encoding="utf-8") as file:
            return _extract_release_number(base_version, file.read())
    except OSError as e:
        warn(f"Failed to read {readme_path}: {e}")
        return None


def _write_counter(counter_path, value):
    temp_path = counter_path + ".tmp"
    try:
        with open(temp_path, "w", encoding="utf-8") as file:
            file.write(f"{value}\n")
        os.replace(temp_path, counter_path)
    except OSError as e:
        warn(f"Failed to persist counter {counter_path}: {e}")


def get_current_release_number(project_dir, base_version):
    counter_dir = _ensure_counter_dir(project_dir)
    counter_path = os.path.join(counter_dir, RELEASE_COUNTER_FILE)
    counter_value = _read_counter(counter_path, INITIAL_RELEASE_NUMBER)
    published_values = [
        value
        for value in (
            _latest_tag_release_number(project_dir, base_version),
            _readme_release_number(project_dir, base_version),
        )
        if value is not None
    ]
    release_number = max([counter_value, *published_values])
    source = counter_path
    if release_number != counter_value:
        source = f"{counter_path} (raised by published release metadata)"
    return release_number, source


def preview_next_release_number(project_dir, base_version):
    current_release, counter_path = get_current_release_number(project_dir, base_version)
    next_release = current_release + 1
    return next_release, counter_path


def release_number_from_tag(base_version):
    tag = os.environ.get("VCODEX_RELEASE_TAG") or os.environ.get("GITHUB_REF_NAME")
    if not tag:
        return None

    match = re.fullmatch(rf"{re.escape(base_version)}\.(\d+)-cpr-vcodex", tag)
    if not match:
        raise ValueError(f"Release tag {tag!r} does not match expected pattern {base_version}.<release>-cpr-vcodex")

    return int(match.group(1)), tag


def next_dev_counter(project_dir, release_number):
    counter_dir = _ensure_counter_dir(project_dir)
    counter_path = os.path.join(counter_dir, DEV_COUNTER_FILE_TEMPLATE.format(release=release_number))
    current_value = _read_counter(counter_path, 0)
    next_value = current_value + 1
    _write_counter(counter_path, next_value)
    return next_value, counter_path


def inject_version(env):
    env_name = env["PIOENV"]
    if env_name not in ("default", "gh_release"):
        return

    project_dir = env["PROJECT_DIR"]
    base_version = get_base_version(project_dir)

    if env_name == "default":
        release_number, release_counter_path = get_current_release_number(project_dir, base_version)
        build_counter, counter_path = next_dev_counter(project_dir, release_number)
        short_sha = get_git_short_sha(project_dir)
        version_string = f"{base_version}.{release_number}.dev{build_counter}-{short_sha}"
        build_kind = "dev"
        print(f"CPR-vCodex release line: {release_number} ({release_counter_path})")
    else:
        tagged_release = release_number_from_tag(base_version)
        if tagged_release:
            release_number, tag = tagged_release
            counter_path = f"release tag {tag}"
        else:
            release_number, counter_path = preview_next_release_number(project_dir, base_version)
        build_counter = release_number
        version_string = f"{base_version}.{release_number}"
        build_kind = "release"

    env.Append(
        CPPDEFINES=[
            ("CROSSPOINT_VERSION", f'\\"{version_string}\\"'),
            ("VCODEX_BUILD_SEQ", build_counter),
            ("VCODEX_RELEASE_SEQ", release_number),
            ("VCODEX_BUILD_KIND", f'\\"{build_kind}\\"'),
        ]
    )
    print(f"CPR-vCodex build version: {version_string}")
    print(f"CPR-vCodex {build_kind} counter: {build_counter} ({counter_path})")


try:
    Import("env")  # noqa: F821  # type: ignore[name-defined]
    inject_version(env)  # noqa: F821  # type: ignore[name-defined]
except ValueError as e:
    print(f"ERROR [git_branch.py]: {e}", file=sys.stderr)
    sys.exit(1)
except NameError:
    class _Env(dict):
        def Append(self, **_):
            pass

    _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    inject_version(_Env({"PIOENV": "default", "PROJECT_DIR": _project_dir}))
