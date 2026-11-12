# mekanism 2.0.0

A small parallel build tool for C/C++ (and anything else that can be run from a shell).
Linux, macOS, FreeBSD, NetBSD. Needs a C++17 compiler, no other dependencies.

## Features

- **Caching is just object files.** Every source is compiled to its own `.o` by the compiler,
  like in `make`. A source is recompiled only when its `.o` is missing, older than the source or
  one of its headers, or was built with a different command. Nothing else is stored.
- **Header tracking** through the compiler's own dependency files (`-MMD -MF`), no header scanning.
- **Parallel**: every source file is a separate job, jobs run as soon as their dependencies are done.
- **Output**: one line per command with the reason it runs, compiler messages unchanged, a one-line summary.
- **BUILD.mek**: variables, functions (command templates), `switch`, `force` loops, `console()`.
- **Autodetected toolchain**: `{var:cxx.detect}`, `{var:cc.detect}`, `{var:go.detect}`, `{var:java.detect}`, `{var:lua.detect}`.
- Failed commands never leave a half-written output that looks up to date.

## Quick start

First build (no mekanism yet):

```sh
mkdir -p build
c++ -std=c++17 -O2 -I. -D_XOPEN_SOURCE=700 mekanism.cxx sources/*.cxx -pthread -o build/mekanism
```

After that mekanism builds itself:

```sh
./build/mekanism
```

## Command line

```
mekanism [options] [target...]

  --workspace <path>  directory with BUILD.mek (default: current directory)
  --jobs <N>          number of parallel commands (default: number of CPU cores)
  --keep-going        after a failure, keep building what does not depend on it
  --force             run every command, ignore up-to-date objects
  --dry-run           show what would run, run nothing
  --clean             delete .mekanism/ and the declared outputs of all targets
  --list-targets      list targets with their dependencies and outputs
  --check-deps        check that dependencies exist and have no cycles
  --git-info          show git branch, commit and dirty state of the workspace
  --log-file <path>   also append all messages to this file
  --verbose           show commands and timings
  --version           show version
```

Without target names mekanism builds `default_target` from BUILD.mek, or if there is none,
every target that no other target depends on. Exit code: 0 success, 1 build or BUILD.mek error, 2 bad command line.

## Output

```
mekanism 2.0.0, platform linux, workspace /home/me/project
targets: mekanism
plan: 10 commands, 8 worker(s)
[ 1/10] compile mekanism  mekanism.cxx  (output missing: .mekanism/obj/mekanism/mekanism.cxx.o)
[ 2/10] skip    mekanism  sources/log.cxx  (up to date)
[ 3/10] compile mekanism  sources/plan.cxx  (input is newer: headers/plan.hxx)
[10/10] build   mekanism  build/mekanism  (a dependency was rebuilt)
done: 2 built, 8 up to date, 0 failed, 0 not run, 1.84s
```

Format: `[n/total] action target what (reason)`. Actions: `compile` (one source), `build` (the target's
own command, e.g. link), `clone` (repo), `skip`. Output of a command is printed right after its line.
On failure the command line and all its output are printed under `error:`. `not run` counts commands
that were skipped because something they depend on failed. Everything goes to stdout.

## BUILD.mek

Paths are relative to the workspace. Lines starting with `#` are comments. A value in quotes may span
several lines (lines starting with `#` inside are ignored). Paths cannot contain spaces.

```
var cxx: {var:cxx.detect}
var flags: -O2 -std=c++17

func compile: {var:cxx} {var:flags} -MMD -MF {depfile} -c {source} -o {object}
func link:    {var:cxx} {objects} -o {output0} -pthread

default_target: app

target app:
  sources: "
    main.cxx
    util.cxx
  "
  compile: compile
  use: link
  outputs: build/app
```

### Statements

| Statement | Meaning |
|---|---|
| `var name: value` | variable, used as `{var:name}`, may use other variables |
| `func name: template` | command template, used by `use:` and `compile:` |
| `default_target: a b` | targets built when none are named on the command line |
| `target name:` | starts a target, the field lines below belong to it |
| `console("text")` | prints `console: text` while BUILD.mek is read |
| `switch field expr:` | picks a value by `case` (see below) |
| `force v in a b c:` ... `endforce` | repeats the lines in between for each item (`forse` is accepted as well) |

### Target fields

| Field | Meaning |
|---|---|
| `sources:` | input files, whitespace or one per line |
| `outputs:` | files the target produces. **A target without outputs always runs.** |
| `deps:` | targets that must be built first |
| `compile:` | func run once per source, must contain `{object}`, put `-MMD -MF {depfile}` in it to track headers |
| `use:` | func run once for the target (link, archive, copy, ...) |
| `cmd:` | literal command instead of `use:` (cannot be combined with `use:`), several lines run as a script |
| `build_env:` | `NAME=value`, one per line, exported for every command of the target |
| `repo:` | `<url> <dest> [branch]`: `git clone --depth 1` into `dest` unless it exists |

Unknown fields are reported as warnings.

### Template tokens (in `use:`, `compile:`, `cmd:`)

| Token | Value |
|---|---|
| `{name}` | target name |
| `{sources}`, `{inputs}` | all sources |
| `{input0}`, `{input1}`, ... | one source |
| `{outputs}`, `{output0}`, ... | outputs |
| `{deps}` | dependency targets |
| `{objects}` | the `.o` files of all sources (targets with `compile:`) |
| `{source}`, `{object}`, `{depfile}` | in `compile:` only: current source, its `.o`, its `.d` |
| `{var:name}` | variable |

Anything else in braces (`${HOME}`, awk `{print}`) is passed to the shell untouched.

### switch

```
switch cxxflags {var:platform}:
  case linux: -O3 -Wall
  case macos: -O3 -Wall -D_DARWIN_C_SOURCE
  default:    -O2
```

Before the first `target` it sets a variable, inside a target it sets that target's field.
`case` values are compared with the expanded expression, case-sensitive.

### force

```
force n in one two three:
target gen_{var:n}:
  cmd: ./gen.sh {var:n} > out/{var:n}.txt
  outputs: out/{var:n}.txt
endforce
```

### Predefined variables

| Variable | Value |
|---|---|
| `{var:platform}` | `linux`, `macos`, `freebsd`, `netbsd`, `unknown` |
| `{var:platform.linux}`, `.macos`, `.freebsd`, `.netbsd`, `.bsd` | `1` or `0` |
| `{var:platform.family}` | `unix` (Linux), `bsd`, `unknown` |
| `{var:cpu.cores}` | logical CPU count |
| `{var:cxx.detect}` | first found of `clang++`, `g++`, `c++` |
| `{var:cc.detect}` | `clang`, `gcc`, `cc` |
| `{var:go.detect}`, `{var:java.detect}` | `go`, `javac` |
| `{var:lua.detect}` | `luajit`, `lua5.4`, `lua5.3`, `lua` |
| `{var:git.branch}`, `{var:git.commit}`, `{var:git.dirty}` | state of the workspace repository |

They are computed when first used. Your own `var` with the same name wins.

## How caching works

Files live in `.mekanism/` in the workspace:

```
.mekanism/obj/<target>/<source path>.o     object file
.mekanism/obj/<target>/<source path>.d     headers it includes, written by the compiler
.mekanism/obj/<target>/<source path>.cmd   command that produced the object
.mekanism/stamp/<target>.cmd               command that produced the target's outputs
```

A command runs when any of these is true (the first one found is printed as the reason):

1. the target declares no outputs,
2. an output is missing,
3. the saved command differs from the current one (changed flags, compiler, variables, `build_env`),
4. for a `build` command: a dependency with outputs was rebuilt in this run,
5. an input (source, header from the `.d` file, `.o` for a link) is newer than the oldest output.

Timestamps are compared like `make` does, so a file system with very coarse timestamps can miss an edit made within the same tick.
After a failed command its outputs (regular files only) and its saved command are removed.
A command that exits 0 but does not create a declared output counts as failed.
`--force` skips these checks, `--clean` deletes `.mekanism/` and the declared outputs
(never paths outside the workspace, cloned `repo:` directories stay).

## Extending

| I want to... | Change |
|---|---|
| add a target field | member in `Target` (`headers/config.hxx`), a branch in `set_field` (`sources/config.cxx`), use it in `add_target_jobs` (`sources/plan.cxx`) |
| add a template token | one entry in `target_tokens` (`sources/plan.cxx`), per-source tokens next to `{source}` |
| add a predefined variable or language | a row in `TOOLS` (`sources/builtins.cxx`) or a branch in `compute` |
| add a command line option | enum value, table row and `case` in `mekanism.cxx` |
| change the staleness rules | `stale_reason` (`sources/cache.cxx`) |
| change the output | `say`, `execute` (`sources/run.cxx`), `log_*` (`sources/log.cxx`) |
| support another compiler's dependency files | the `compile` func only: write the right flag for that compiler and keep `{depfile}` in the template (the file must be Makefile-style) |

Examples:

```
# Go: no objects, one command, mekanism checks sources against the output
target server:
  sources: cmd/server/main.go
  cmd: {var:go.detect} build -o {output0} {inputs}
  outputs: build/server

# C static library (comments are only allowed on their own line)
func ar_pack: ar rcs {output0} {objects}

target hash:
  sources: "lib/hash.c lib/alloc.c"
  compile: cc_obj
  use: ar_pack
  outputs: build/libhash.a
```

## Code

```
mekanism.cxx          command line, runs the steps below
headers/ + sources/   one .hxx/.cxx pair per module
  log                 plain thread-safe logging
  util                strings, files, running a shell command
  expand              {token} replacement
  builtins            platform, tool detection, git, CPU count
  config              BUILD.mek parser, Target and Config structs
  plan                Target -> list of jobs, dependency check
  cache               up-to-date check, .d parsing, command stamps
  run                 parallel scheduler, runs and reports jobs
```

Data flow: `load_config` -> `make_plan` -> `run_plan`. No classes, modules are plain structs and functions.
A job (`Job` in `plan.hxx`) is one shell command with its inputs and outputs, and everything else
(scheduling, caching, output) only looks at jobs.

## Changes since 1.5.x

- Removed the content-addressable store, cache index, meta files, stat/hash caches, `Mekanism.art`, `--artifacts`,
  SHA-256 and the VaultX dependency. The cache is the `.o` files.
- Removed the spinner, colors and `--no-color`, and the no-op `--remote-exec`.
- Removed the fields `includes`, `targ`, `toolset`, `priority`, `filelist`, `copies`, `git_cmds`
  and the `{filelist}` token. Use `deps`, `cmd` and `{objects}`.
- New: `compile:`, `{source}`, `{object}`, `{objects}`, `{depfile}`, `default_target:`.
- Fixed: values in `cmd:`/`sources:`/`outputs:` were never expanded (`{var:...}`, `{sources}`),
  `force` loops used the last item for every iteration, docs said `force` but the code only knew `forse`,
  `build_env` was only hashed, never exported, outputs were deleted before every cache check, (absolute chinema *=*)
  unknown target names built everything, a target without outputs (`clear`) ran once and was then cached
  forever, and a root `clear` target ran by default and deleted `build/`,
  git was run once per target, `nproc --all` ignored CPU limits.
- Old cache directories (.mekanism_cas, .mekanism_meta, .mekanism_cache, .mek_filelists, Mekanism.art) as they are no longer used, delete them.

See `LICENSE.txt` / `LICENSERU.txt`.
