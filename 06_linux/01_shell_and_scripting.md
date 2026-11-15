# LX 01: The shell and robust scripting

*Part of the [Linux plan](00_plan.md) · Time: about 4 hours · You need: [OS 02](../05_operating_systems/02_processes.md) (processes, `fork`, `exec`) and [OS 01](../05_operating_systems/01_os_structure_and_system_calls.md) (system calls). The terminal and process-group facts used in section 8 are measured in [OS 21](../05_operating_systems/21_os_interview_question_bank.md), section 8.3.*

The shell is the first program you run and the one you debug with, and it is also the language in which most Linux automation, build glue and incident response is written. Most of what goes wrong in shell scripts goes wrong for one reason: **the shell is a macro processor that rewrites a line of text several times before any program runs**, and people write as if it were a normal programming language with typed values. Interviewers know this. The questions they ask (what does `echo $x` do when `x` is empty, why did my loop break on a file with a space in its name, what does `set -e` not protect against, what happens when you press Ctrl-C in a script) are all questions about the rewriting and about the processes underneath it.

This tutorial is built around **experiments**. Every transcript below was produced by a script on the machine described under "Where the numbers come from", and every script printed in full was run. The sequence is: how a line is processed (section 1), quoting (2), file names (3), redirection (4), pipes and subshells (5), exit statuses and `set -e` (6), variables, arrays and arithmetic (7), job control (8), a complete robust script and its failure tests (9), traps (10), races and locking (11), portability (12), start-up files (13), speed (14), ShellCheck and debugging (15), and security hazards (16).

**Four honest limits, stated first.** (1) Everything is measured with **bash 5.2.21 and dash** on one Linux 6.18 Firecracker VM running as root; other bash versions and other `/bin/sh` implementations (BusyBox, zsh in `sh` mode, macOS's bash 3.2) differ in places, and the portability section shows by how much for dash. (2) The **order of expansions** and the **`set -e` rules** were checked against the GNU bash manual pages I fetched (listed under Sources); the **command-lookup order**, the exit codes 126 and 127, and the POSIX/sysexits conventions are from memory, confirmed here by the experiments where I say so. (3) Timings are medians or single runs on a VM and vary by 10 to 30 percent; the ratios and the orders of magnitude are the lesson. (4) I could not test a terminal closing (`SIGHUP` from a hang-up) without a real terminal, so the section on `nohup` shows the **signal mask** that results rather than a hang-up.

By the end you can:

- say in order what the shell does to a line (measured: `{a,b}{1,2}` gave `a1 a2 b1 b2`, but a variable holding `{x,y}` did not brace-expand, because brace expansion runs before parameter expansion);
- predict the number of words an expansion produces, quoted and unquoted (measured: an unquoted empty variable produced 0 words and a quoted one produced 1; unquoted `"$*"` over three parameters gave 1 word and `"$@"` gave 3);
- write loops over file names that survive spaces, newlines and a leading dash (measured: `for f in $(ls)` ran 7 times over 5 files; `for f in *` ran 5);
- explain redirection order (measured: `f >o 2>&1` put both streams in the file and `f 2>&1 >o` left stderr on the terminal side of the pipe, visible in the `dup2` order from `strace`);
- explain which part of a pipeline runs in which process (measured: `echo x | read v` left `v` empty; `shopt -s lastpipe` fixed it; `yes | head -1` finished with statuses `141 0`);
- list the cases in which `set -e` does **not** stop a script (measured on bash and dash: 26 cases in a table, including `f || handler` silently switching off `-e` inside `f`);
- use parameter expansion, arrays, associative arrays and shell arithmetic, and name their traps (measured: `$((010))` is 8, `$((2**63))` wraps to a negative number, `[[ 9 < 10 ]]` is false);
- explain process groups, `setsid`, `nohup`, `disown` and what each changes (measured: a background job in a non-interactive shell starts with `SIGINT` and `SIGQUIT` ignored; `nohup` adds `SIGHUP`);
- write a script with usage text, option parsing, a lock, an atomic output, a cleanup trap and distinct exit codes, and test it against success, failure, `SIGINT`, `SIGTERM` and `SIGKILL` (measured: a `SIGTERM` sent to the script alone was handled **3,518 ms** later, after its child finished; sent to the process group it was handled in 3 ms);
- show a lost-update race and fix it with `flock` (measured: 8 workers incrementing a counter 250 times each ended at 3, 6 and 16 instead of 2,000 without a lock, and at exactly 2,000 with `flock`);
- say what costs what in a script (measured: arithmetic in the shell about 2.6 microseconds, a forked `$(...)` about 370 microseconds even for a builtin, a fork plus exec about 1.6 milliseconds);
- run ShellCheck and fix what it finds, trace with `set -x` and a good `PS4`, and avoid the injection and glob hazards of section 16.

**Where the numbers come from.** Twenty scripts were run with `bash 5.2.21` (`GNU bash, version 5.2.21(1)-release (x86_64-pc-linux-gnu)`), with `dash` as `/bin/sh` (`/usr/bin/dash`), on Linux 6.18.44 in a Firecracker micro-VM with 2 vCPUs and about 8 GiB (7.8 GiB reported) of memory, GCC 13.3 and glibc 2.39 installed, running as root. `strace` was used to show system calls, ShellCheck 0.11.0 (installed from PyPI for this session) for static checks, and `flock`, `jq`, `bc`, `xargs` and GNU coreutils for the rest. Transcripts print `$ command` before each command's output; scripts that print process IDs, temporary-file names or addresses were post-processed so the text shows relations (same group, same session) and not numbers that change on every run. Where a tool appears in an output with `XXXXXX`, that is a masked random suffix.

**Data and timing.** The transcripts are deterministic except where a number follows a timing, a race or a random name. For those the text gives the observed values or a range. Do not memorise a timing: memorise the **relation** (a fork is hundreds of times dearer than arithmetic, an unlocked counter loses almost every update).

---

## 1. How the shell processes a line

### 1.1 The pipeline of transformations

When bash reads a line (from a terminal, a script file or `-c`) it runs these stages, in this order (the expansion order is from the GNU bash manual, fetched; the rest is from memory and visible in the experiments):

1. **Read and tokenise.** The text is cut into **words** and **operators** (`|`, `&&`, `;`, `>` and so on) at unquoted metacharacters, with quoting recognised at this stage. **Aliases** are substituted here, before parsing, which is why an alias cannot be defined and used on the same line.
2. **Parse** into commands: simple commands, pipelines, lists (`;`, `&&`, `||`, `&`) and compound commands (`if`, `for`, `while`, `case`, `{ }`, `( )`, `[[ ]]`, functions).
3. **Expand**, in this order: **brace expansion**; then **tilde, parameter and variable, arithmetic and command substitution, and process substitution**, all in one left-to-right pass; then **word splitting**; then **filename expansion** (globbing); then **quote removal**. The manual's wording is that the second group is "done in a left-to-right fashion", and that only brace expansion, word splitting and filename expansion can change the number of words (with the exceptions `"$@"` and `"${name[@]}"`).
4. **Redirections** are applied, left to right (the redirection targets are themselves expanded).
5. **Find and run the command**: function, builtin or external program (section 1.3), in the current shell or in a forked child.

Four consequences follow from the order, and the first transcript shows each.

```bash
#!/usr/bin/env bash
# e1_expand.sh: the order of expansions, and what the same line does in dash
. "$(dirname "$0")/h.sh"
cd "$(mktemp -d)" && touch 'a b.txt' c.txt
show 'echo {a,b}{1,2}'
show "v='{x,y}'; echo \$v"
show 'n=3; echo {1..$n}'
show "a='1   2    3'; echo \$a; echo \"\$a\""
show "pat='*.txt'; echo \$pat; echo \"\$pat\""
show 'echo ~ "~"'
show 'echo $((2+3)) $(echo hi) `echo old`'
show "x=; set -- \$x; echo \$#; set -- \"\$x\"; echo \$#"
show "p=a:b:c; IFS=:; set -- \$p; echo \$# \$2; unset IFS"
show 'echo *.txt'
show 'printf "<%s>\n" *.txt'
echo
echo '--- the same brace line in dash (the POSIX shell on Debian and Ubuntu)'
printf '$ dash -c %s\n' "'echo {a,b}{1,2}'"
dash -c 'echo {a,b}{1,2}'
```

```text
$ echo {a,b}{1,2}
a1 a2 b1 b2
$ v='{x,y}'; echo $v
{x,y}
$ n=3; echo {1..$n}
{1..3}
$ a='1   2    3'; echo $a; echo "$a"
1 2 3
1   2    3
$ pat='*.txt'; echo $pat; echo "$pat"
a b.txt c.txt
*.txt
$ echo ~ "~"
/root ~
$ echo $((2+3)) $(echo hi) `echo old`
5 hi old
$ x=; set -- $x; echo $#; set -- "$x"; echo $#
0
1
$ p=a:b:c; IFS=:; set -- $p; echo $# $2; unset IFS
3 b
$ echo *.txt
a b.txt c.txt
$ printf "<%s>\n" *.txt
<a b.txt>
<c.txt>

--- the same brace line in dash (the POSIX shell on Debian and Ubuntu)
$ dash -c 'echo {a,b}{1,2}'
{a,b}{1,2}
```

- **Brace expansion comes first, so it never sees variables.** `echo {a,b}{1,2}` gives `a1 a2 b1 b2`, but `v='{x,y}'; echo $v` prints `{x,y}` (the braces arrived after brace expansion had finished), and `{1..$n}` stays literal. Use `seq`, a `for ((;;))` loop, or `eval` (with care, section 16) for a counted range with a variable bound. dash has **no brace expansion at all**: the last transcript line shows it passing `{a,b}{1,2}` through unchanged.
- **Word splitting applies only to the results of unquoted expansions**, using `$IFS` (space, tab and newline by default). `a='1   2    3'; echo $a` gives `1 2 3` (three words, joined by `echo` with single spaces) and `echo "$a"` keeps the spacing. With `IFS=:` the string `a:b:c` splits into 3 words.
- **Globbing also applies to the results of unquoted expansions.** `pat='*.txt'; echo $pat` printed the matching file names, and `echo "$pat"` printed the pattern. That is the root of a large share of shell bugs: a value that is data becomes a pattern.
- **An unquoted empty variable vanishes; a quoted one is one empty word.** `set -- $x` left `$#` at 0, and `set -- "$x"` left it at 1. This is why `[ $x = y ]` fails with a syntax error when `x` is empty (measured in section 16) and why `rm -rf $dir/*` is dangerous when `dir` is empty.
- **Tilde expansion needs an unquoted `~`**: `echo ~ "~"` gave `/root ~`.

### 1.2 What a `[[ ]]`, `$(( ))` and `$( )` are

`$(command)` runs `command` in a **subshell** (a forked copy of the shell, section 5) and substitutes its standard output with trailing newlines removed. The backtick form does the same and nests badly (ShellCheck flags it, section 15). `$((expression))` is **arithmetic expansion**: 64-bit signed integers, C-like operators, no floating point (section 7). `[[ ... ]]` is a **keyword** with its own parsing: no word splitting or globbing inside it, `==` and `=~` pattern and regex matching, `&&` and `||` inside. `[ ... ]` is an ordinary command (a builtin that is also `/usr/bin/[`) whose arguments went through all the expansions above, which is why it needs quoting.

### 1.3 Finding the command

The lookup order (from memory, and visible in the transcript) is: **alias** (if enabled), **keyword** (`if`, `while`, `[[`), **function**, **builtin**, then the **hash table** of remembered paths, then a **`PATH` search**. The `type` builtin shows which one applies.

```bash
#!/usr/bin/env bash
# e1b_lookup.sh: how the shell finds a command: alias, function, builtin, hash, PATH
. "$(dirname "$0")/h.sh"
shopt -s expand_aliases
show 'type -a echo'
show 'type -a ls'
show 'echo() { printf "function echo: %s\n" "$*"; }; echo hi; builtin echo hi; command echo hi; unset -f echo'
show "alias ll='ls -l'; type ll; unalias ll"
show 'type -t cd; type -t if; type -t ls; type -t nosuchthing; echo status $?'
show 'command -v ls; command -v nosuchthing || echo "not found"'
show 'hash -r; ls >/dev/null; hash'
```

```text
$ type -a echo
echo is a shell builtin
echo is /usr/bin/echo
echo is /bin/echo
$ type -a ls
ls is /usr/bin/ls
ls is /bin/ls
$ echo() { printf "function echo: %s\n" "$*"; }; echo hi; builtin echo hi; command echo hi; unset -f echo
function echo: hi
hi
hi
$ alias ll='ls -l'; type ll; unalias ll
ll is aliased to `ls -l'
$ type -t cd; type -t if; type -t ls; type -t nosuchthing; echo status $?
builtin
keyword
file
status 1
$ command -v ls; command -v nosuchthing || echo "not found"
/usr/bin/ls
not found
$ hash -r; ls >/dev/null; hash
hits    command
   1    /usr/bin/ls
```

- `type -a echo` lists **every** match in order of precedence: the builtin first, then the two files (`/usr/bin/echo` and `/bin/echo`, the same file reached through a symlink on this merged-`/usr` system). The builtin wins, which is why `echo -e` behaves differently from `/bin/echo -e` on some systems.
- A **function named `echo`** shadows the builtin; `builtin echo` and `command echo` bypass the function. Aliases are off in non-interactive shells unless `shopt -s expand_aliases` is set (the script sets it so the alias line works).
- `type -t` prints `builtin`, `keyword`, `file`, `alias` or `function`, and prints nothing and returns 1 for a name that does not exist, which makes it the right test in scripts; `command -v` is the portable form.
- `hash` shows the table the shell fills as it finds commands (`1 /usr/bin/ls` after one use) and `hash -r` empties it; that table is why a command you just installed in a new directory is sometimes "not found" until `hash -r` or a new shell.

### 1.4 What the shell does for `echo hi | cat` and for an external command

The `strace` below shows the system calls the shell makes for a two-stage pipeline (process IDs replaced by letters in order of appearance, `A` being the shell).

```bash
#!/usr/bin/env bash
# e5b_strace.sh: the system calls behind "echo hi | cat" and behind running an external command
d=$(mktemp -d) && cd "$d" || exit 1
strace -f -o st.txt -e trace=clone,clone3,execve,pipe2,dup2,wait4 bash -c 'echo hi | cat' >/dev/null
# replace each pid by a letter in order of first appearance
python3 - <<'P'
import re
txt=open('st.txt').read()
names={}
for l in txt.splitlines():
    m=re.search(r'clone.*= (\d+)$',l)
    if m and 'unfinished' not in l: names.setdefault(m.group(1),'BCDEFG'[len(names)])
lines=txt.splitlines()
for l in lines:
    m=re.match(r'(\d+)\s+(.*)',l)
    if not m: continue
    pid,rest=m.groups()
    if re.match(r'(\+\+\+|---)',rest): continue
    who=names.get(pid,'A')
    if rest.startswith('execve'): rest=re.sub(r'\[.*?\],','[...],',rest)
    rest=re.sub(r'0x[0-9a-f]+','ADDR',rest)
    rest=re.sub(r'(clone3?)\(.*?SIGCHLD.*?\)',r'\1(... SIGCHLD)',rest)
    rest=re.sub(r'\b(\d{3,})\b',lambda m:names.get(m.group(1),m.group(1)),rest)
    print(who,re.sub(r' +',' ',rest)[:100])
P
echo
echo '--- running an external command: the PATH search'
PATH=/opt/nowhere:/usr/local/bin:/usr/bin strace -f -o st2.txt -e trace=execve,newfstatat,access,stat bash -c 'lsb_release_missing_cmd; sort /dev/null' >/dev/null 2>&1
grep -E 'lsb_release_missing_cmd|/sort' st2.txt | sed -E 's/^[0-9]+ +//; s/newfstatat\(AT_FDCWD, ("[^"]*"),.*\) += +(-?[0-9]+)( ENOENT)?.*/stat(\1) = \2\3/; s/access\(("[^"]*"), X_OK\) += +0/access(\1, X_OK) = 0/' | grep -v '^execve' | uniq
cd / && rm -rf "$d"
```

```text
A execve("/usr/bin/bash", [...], ADDR /* 242 vars */) = 0
A pipe2([3, 4], 0) = 0
A clone(... SIGCHLD) = B
A clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD <unfinished ...>
B dup2(4, 1) = 1
A <... clone resumed>, child_tidptr=ADDR) = C
A wait4(-1, <unfinished ...>
C dup2(3, 0) = 0
A <... wait4 resumed>[{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = B
A wait4(-1, <unfinished ...>
C execve("/usr/bin/cat", [...], ADDR /* 242 vars */) = 0
A <... wait4 resumed>[{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = C
A wait4(-1, ADDR, WNOHANG, NULL) = -1 ECHILD (No child processes)

--- running an external command: the PATH search
stat("/opt/nowhere/lsb_release_missing_cmd") = -1 ENOENT
stat("/usr/local/bin/lsb_release_missing_cmd") = -1 ENOENT
stat("/usr/bin/lsb_release_missing_cmd") = -1 ENOENT
stat("/opt/nowhere/sort") = -1 ENOENT
stat("/usr/local/bin/sort") = -1 ENOENT
stat("/usr/bin/sort") = 0
access("/usr/bin/sort", X_OK) = 0
stat("/usr/bin/sort") = 0
access("/usr/bin/sort", R_OK)     = 0
```

Read the first block as a recipe: the shell (`A`) creates the pipe with `pipe2` **before** forking, then `clone`s twice (one per stage; the flags `SIGCHLD` mean an ordinary fork), each child `dup2`s the right end of the pipe onto its standard output (`B`, descriptor 1) or standard input (`C`, descriptor 0), and only `C` calls `execve` (for `/usr/bin/cat`). **`B` never calls `execve`: `echo` is a builtin, so the child runs it directly and exits.** The shell then `wait4`s for both children (the two `wait4` return values are `B` and `C`, the children it reaped), and a final `wait4` returns `ECHILD` because none are left.

The second block shows command lookup at the system-call level: for a command that exists nowhere (`lsb_release_missing_cmd`) bash `stat`s the name in **each `PATH` directory in order** (three misses, then the shell reports "command not found" and sets status 127, from memory); for `sort` it misses in the first two directories, finds `/usr/bin/sort` in the third, checks it with `access(..., X_OK)` and then forks and executes it. This is also why a long `PATH` makes every uncached command lookup a little slower, and why the hash table exists.

---

## 2. Quoting

Quoting stops the later stages from touching text. The four tools:

| Form | What it protects | What still happens inside |
| --- | --- | --- |
| `'...'` | everything | nothing; a single quote cannot appear inside |
| `"..."` | word splitting, globbing, brace and tilde expansion | `$var`, `${...}`, `$(...)`, `$((...))`, backquotes, `\` before `$`, `` ` ``, `"`, `\`, newline |
| `\c` | one character | nothing |
| `$'...'` | (a different thing) | ANSI-C escapes (`\n`, `\t`, `\x41`) are decoded |

```bash
#!/usr/bin/env bash
# e2_quote.sh: quoting, "$@" against "$*", and safe quoting for reuse
. "$(dirname "$0")/h.sh"
args() { printf '%d args:' "$#"; printf ' <%s>' "$@"; echo; }
echo "positional parameters for the next five lines: 'a b'  c  ''"
show 'args $*' 'a b' c ''
show 'args "$*"' 'a b' c ''
show 'args $@' 'a b' c ''
show 'args "$@"' 'a b' c ''
show 'args "$@" "extra"' 'a b' c ''
show "echo 'it'\\''s' \"say \\\"hi\\\"\"; printf '%s' \$'tab:\\there' | od -An -c"
show 'x="a  b"; echo $x "$x" '"'"'$x'"'"
show 'printf "%q\n" "a b" "it'"'"'s" $'"'"'new\nline'"'"
show 'echo "a\nb"; echo -e "a\nb"; printf "a\nb\n"'
show 'n=-n; echo $n; printf "%s\n" $n'
```

```text
positional parameters for the next five lines: 'a b'  c  ''
$ args $*
3 args: <a> <b> <c>
$ args "$*"
1 args: <a b c >
$ args $@
3 args: <a> <b> <c>
$ args "$@"
3 args: <a b> <c> <>
$ args "$@" "extra"
4 args: <a b> <c> <> <extra>
$ echo 'it'\''s' "say \"hi\""; printf '%s' $'tab:\there' | od -An -c
it's say "hi"
   t   a   b   :  \t   h   e   r   e
$ x="a  b"; echo $x "$x" '$x'
a b a  b $x
$ printf "%q\n" "a b" "it's" $'new\nline'
a\ b
it\'s
$'new\nline'
$ echo "a\nb"; echo -e "a\nb"; printf "a\nb\n"
a\nb
a
b
a
b
$ n=-n; echo $n; printf "%s\n" $n
-n
```

- **`$*` against `$@`.** With the parameters `a b`, `c` and an empty string: unquoted `$*` and `$@` both produced **3 words** (`a`, `b`, `c`; the empty one vanished and `a b` was split); `"$*"` produced **one** word joining them with the first character of `IFS` (`a b c ` with a trailing space for the empty parameter); `"$@"` produced **three** words exactly as passed (`a b`, `c`, and the empty string). **`"$@"` is the form that forwards arguments faithfully**, and it is the answer to the interview question "how do you pass your arguments to another command".
- **Embedding a single quote** in a single-quoted string is done by closing, escaping and reopening: `'it'\''s'` printed `it's`.
- **`$'...'` decodes escapes**, as the `od` output shows (`\t` is one tab character).
- **`echo` is unreliable** for data: `echo "a\nb"` printed the backslash-n literally in bash and `echo -e` interpreted it, and a variable holding `-n` made `echo $n` print nothing at all (it was parsed as an option). **Use `printf '%s\n' "$x"`** for anything that might begin with a dash or contain a backslash; dash's `echo` interprets backslashes by default (section 12).
- **`printf %q`** produces a form safe to paste back into a shell (`a\ b`, `it\'s`, and `$'new\nline'`), which is how you log or echo a command line faithfully.

---

## 3. File names, globbing and loops that survive them

A Unix file name may contain **any byte except `/` and NUL**: spaces, newlines, a leading `-`, glob characters. Shell code that works on tidy names fails on the rest, and the failures are also security problems (section 16). The experiment creates five awkward names and runs the usual loops.

```bash
#!/usr/bin/env bash
# e3_filenames.sh: hostile file names against the usual loops
. "$(dirname "$0")/h.sh"
d=$(mktemp -d) && cd "$d" || exit 1
touch 'plain.txt' 'two words.txt' 'star*.txt' -- '-c' $'new\nline.txt'
echo "directory holds 5 names: plain.txt, 'two words.txt', 'star*.txt', '-c', and one with a newline inside"
show 'ls | wc -l'
show 'n=0; for f in $(ls); do n=$((n+1)); done; echo "for f in \$(ls) ran $n times"'
show 'n=0; for f in *; do n=$((n+1)); done; echo "for f in * ran $n times"'
show 'printf "<%s>\n" *'
show 'wc -l *; echo "status $?"'
show 'wc -l -- *; echo "status $?"'
show 'find . -type f -print0 | xargs -0 printf "[%s]\n"'
show 'n=0; while IFS= read -r -d "" f; do n=$((n+1)); done < <(find . -type f -print0); echo "read -d loop saw $n"'
show 'find . -type f | while read f; do echo "[$f]"; done'
show 'ls -1 | while IFS= read -r f; do echo "[$f]"; done'
cd / && rm -rf "$d"
```

```text
directory holds 5 names: plain.txt, 'two words.txt', 'star*.txt', '-c', and one with a newline inside
$ ls | wc -l
6
$ n=0; for f in $(ls); do n=$((n+1)); done; echo "for f in \$(ls) ran $n times"
for f in $(ls) ran 7 times
$ n=0; for f in *; do n=$((n+1)); done; echo "for f in * ran $n times"
for f in * ran 5 times
$ printf "<%s>\n" *
<-c>
<new
line.txt>
<plain.txt>
<star*.txt>
<two words.txt>
$ wc -l *; echo "status $?"
0 0 'new'$'\n''line.txt'
0 0 plain.txt
0 0 star*.txt
0 0 two words.txt
0 0 total
status 0
$ wc -l -- *; echo "status $?"
0 -c
0 'new'$'\n''line.txt'
0 plain.txt
0 star*.txt
0 two words.txt
0 total
status 0
$ find . -type f -print0 | xargs -0 printf "[%s]\n"
[./-c]
[./two words.txt]
[./new
line.txt]
[./star*.txt]
[./plain.txt]
$ n=0; while IFS= read -r -d "" f; do n=$((n+1)); done < <(find . -type f -print0); echo "read -d loop saw $n"
read -d loop saw 5
$ find . -type f | while read f; do echo "[$f]"; done
[./-c]
[./two words.txt]
[./new]
[line.txt]
[./star*.txt]
[./plain.txt]
$ ls -1 | while IFS= read -r f; do echo "[$f]"; done
[-c]
[new]
[line.txt]
[plain.txt]
[star*.txt]
[two words.txt]
```

- **`ls | wc -l` counted 6 for 5 files**: the name with a newline occupied two lines.
- **`for f in $(ls)` ran 7 times** (the command substitution was word-split, so `two words.txt` became two items and the newline name two more). **`for f in *` ran 5 times**, because globbing produces one word per match with no splitting. **Never parse `ls`**; ShellCheck rule SC2045 flags this.
- **A name beginning with `-` becomes an option.** `wc -l *` treated the file `-c` as the option `-c` and printed an extra column of byte counts; `wc -l -- *` (the `--` ends option parsing) listed it as a file. The other cure is to prefix globs with `./` (`./*`), which makes every name begin with a dot or a letter. **GNU `wc` also printed the newline name quoted as `'new'$'\n''line.txt'`**; that is display only.
- **`find ... -print0 | xargs -0`** passes names separated by NUL, the only byte that cannot occur in a name, so all five arrived intact, with the newline kept inside `[./new` and `line.txt]`. The shell form is `while IFS= read -r -d '' f; do ...; done < <(find ... -print0)`: the loop **counted 5**.
- **`find ... | while read f`** broke the newline name into two lines (`[./new]` and `[line.txt]`), and without `IFS=` and `-r` `read` also trims whitespace and eats backslashes (section 7 shows both). The same happened with `ls -1 | while IFS= read -r f`: **newline-separated output cannot represent every name**, so choose NUL-separated whenever the names are not under your control.

The rules that follow: **quote every expansion** (`"$f"`), **use `--` or `./` before names**, **iterate with a glob or an array** (not with a command substitution), and **use NUL separators** for lists of names.

---

## 4. Redirection and file descriptors

A redirection changes what a **file descriptor** of the command refers to. Descriptor 0 is standard input, 1 is standard output, 2 is standard error (see [OS 02](../05_operating_systems/02_processes.md) for what a child inherits). The shell applies redirections **left to right**, and `n>&m` means "make `n` a duplicate of whatever `m` is **right now**" (a `dup2(m, n)`). That single rule explains the most-asked redirection question.

```bash
#!/usr/bin/env bash
# e4_redirect.sh: redirection order, here-documents, extra descriptors, and what the shell does (strace)
. "$(dirname "$0")/h.sh"
d=$(mktemp -d) && cd "$d" || exit 1
f() { echo out; echo err >&2; }
show 'f >o 2>&1; cat o'
show 'f 2>&1 >o | cat; echo "--- file o holds:"; cat o'
show 'f &>o; wc -l <o'
show 'f 2>/dev/null'
show 'exec 3>fd3; echo to-three >&3; exec 3>&-; cat fd3'
show 'x=world; cat <<EOF
hello $x
EOF'
show "x=world; cat <<'EOF'
hello \$x
EOF"
show 'cat <<< "here-string: $((6*7))"'
show 'echo old >keep; bash -c "set -C; echo new >keep"; echo "status $?"; bash -c "set -C; echo forced >| keep"; cat keep'
show 'echo data | tee t1 t2 >/dev/null; cat t1 t2'
show 'cat <(echo from-process-substitution)'
echo
echo '--- what bash does for: f >o 2>&1   (strace of dup2 and the open of o)'
strace -f -o st.txt -e trace=openat,dup2,dup3 bash -c 'echo out; echo err >&2' >o 2>&1
strace -o st2.txt -e trace=openat,dup2,dup3 bash -c 'echo out >o 2>&1'
grep -E '"o"|dup2|dup3' st2.txt | sed -E 's/^[0-9]+ +//; s/0x[0-9a-f]+/ADDR/g' | head -8
echo
echo '--- and for: 2>&1 >o   (order reversed)'
strace -o st3.txt -e trace=openat,dup2,dup3 bash -c 'echo out 2>&1 >o'
grep -E '"o"|dup2|dup3' st3.txt | sed -E 's/^[0-9]+ +//' | head -8
cd / && rm -rf "$d"
```

```text
$ f >o 2>&1; cat o
out
err
$ f 2>&1 >o | cat; echo "--- file o holds:"; cat o
err
--- file o holds:
out
$ f &>o; wc -l <o
2
$ f 2>/dev/null
out
$ exec 3>fd3; echo to-three >&3; exec 3>&-; cat fd3
to-three
$ x=world; cat <<EOF
hello $x
EOF
hello world
$ x=world; cat <<'EOF'
hello $x
EOF
hello $x
$ cat <<< "here-string: $((6*7))"
here-string: 42
$ echo old >keep; bash -c "set -C; echo new >keep"; echo "status $?"; bash -c "set -C; echo forced >| keep"; cat keep
bash: line 1: keep: cannot overwrite existing file
status 1
forced
$ echo data | tee t1 t2 >/dev/null; cat t1 t2
data
data
$ cat <(echo from-process-substitution)
from-process-substitution

--- what bash does for: f >o 2>&1   (strace of dup2 and the open of o)
openat(AT_FDCWD, "o", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
dup2(3, 1)                              = 1
dup2(1, 2)                              = 2
dup2(11, 2)                             = 2
dup2(10, 1)                             = 1

--- and for: 2>&1 >o   (order reversed)
dup2(1, 2)                              = 2
openat(AT_FDCWD, "o", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
dup2(3, 1)                              = 1
dup2(11, 1)                             = 1
dup2(10, 2)                             = 2
```

- **`f >o 2>&1`** put both streams in the file `o` (the transcript shows `out` and `err` both coming from `cat o`). **`f 2>&1 >o | cat`** did the opposite: `err` went down the pipe to `cat` and the file held only `out`. The order matters because `2>&1` copied the descriptor-1 object that existed **at that moment**: in the first command that was already the file, in the second it was still the pipe (or terminal).
- The `strace` at the bottom shows the mechanism with real system calls. For `echo out >o 2>&1` the shell does `openat("o") = 3`, `dup2(3, 1)`, `dup2(1, 2)`; for the reversed order it does `dup2(1, 2)` **first** (so descriptor 2 becomes a copy of the original output) and only then opens the file and points 1 at it. The trailing `dup2(11, ...)` and `dup2(10, ...)` lines are the shell **restoring** its own descriptors afterwards: for a builtin the shell runs the command in itself, so it saves the originals (here as 10 and 11) and puts them back.
- **`&>o`** is bash shorthand for `>o 2>&1` and does not exist in POSIX `sh`. **`exec 3>file`** opens a descriptor for the rest of the script (`>&3` writes to it, `3>&-` closes it); this is how the template in section 9 holds its lock.
- **Here-documents** expand variables unless the delimiter is quoted: the unquoted `EOF` printed `hello world` and `'EOF'` printed `hello $x`. A **here-string** `<<<` feeds one string with a trailing newline.
- **`set -C` (noclobber)** makes `>` refuse to overwrite an existing file (`cannot overwrite existing file`, status 1) and `>|` overrides it. Section 11 uses it to stop a write through a planted symlink.
- **`tee`** writes its input to several files and its standard output; **process substitution** `<(cmd)` presents a command's output as a file name (a path under `/dev/fd`), which lets two commands be diffed without temporary files and is how `while read` can read a pipeline's output without a subshell.

---

## 5. Pipes, subshells and what runs where

Every stage of a pipeline is a **separate process**, started with the pipe ends already attached (section 1.4). Two facts follow from that, and they are the source of the best-known shell surprises.

```bash
#!/usr/bin/env bash
# e5_pipes.sh: what runs in which process; subshells; exit statuses of pipelines
. "$(dirname "$0")/h.sh"
d=$(mktemp -d) && cd "$d" || exit 1
show 'echo x | read v; echo "v=[$v]"'
show 'shopt -s lastpipe; set +m; echo x | read v; echo "v=[$v]"'
show 'read v < <(echo x); echo "v=[$v]"'
show 'read -r v <<< "x"; echo "v=[$v]"'
show 'cd /usr; pwd; (cd /tmp; pwd); pwd; { cd /tmp; }; pwd'
show 'p=$$; ( [ "$$" = "$p" ] && echo "in the subshell: \$\$ is still the parent pid"; [ "$BASHPID" != "$p" ] && echo "in the subshell: BASHPID is a new pid" )'
show 'x=1; ( x=2 ); echo "x=$x"; { x=3; }; echo "x=$x"'
show 'v=plain bash -c '"'"'echo "child sees v=[${v-unset}]"'"'"'; export v=exported; bash -c '"'"'echo "child sees v=[$v]"'"'"
show 'false | true; echo "${PIPESTATUS[@]}"'
show 'yes | head -1; echo "${PIPESTATUS[@]}"'
show 'set -o pipefail; false | true; echo "status $? pipes ${PIPESTATUS[@]}"; set +o pipefail'
show 'sh -c "exit 3"; echo $?; ( exit 4 ); echo $?; sh -c "kill -TERM \$\$"; echo "killed by TERM: $?"'
cd / && rm -rf "$d"
```

```text
$ echo x | read v; echo "v=[$v]"
v=[]
$ shopt -s lastpipe; set +m; echo x | read v; echo "v=[$v]"
v=[x]
$ read v < <(echo x); echo "v=[$v]"
v=[x]
$ read -r v <<< "x"; echo "v=[$v]"
v=[x]
$ cd /usr; pwd; (cd /tmp; pwd); pwd; { cd /tmp; }; pwd
/usr
/tmp
/usr
/tmp
$ p=$$; ( [ "$$" = "$p" ] && echo "in the subshell: \$\$ is still the parent pid"; [ "$BASHPID" != "$p" ] && echo "in the subshell: BASHPID is a new pid" )
in the subshell: $$ is still the parent pid
in the subshell: BASHPID is a new pid
$ x=1; ( x=2 ); echo "x=$x"; { x=3; }; echo "x=$x"
x=1
x=3
$ v=plain bash -c 'echo "child sees v=[${v-unset}]"'; export v=exported; bash -c 'echo "child sees v=[$v]"'
child sees v=[plain]
child sees v=[exported]
$ false | true; echo "${PIPESTATUS[@]}"
1 0
$ yes | head -1; echo "${PIPESTATUS[@]}"
y
141 0
$ set -o pipefail; false | true; echo "status $? pipes ${PIPESTATUS[@]}"; set +o pipefail
status 1 pipes 1 0
$ sh -c "exit 3"; echo $?; ( exit 4 ); echo $?; sh -c "kill -TERM \$\$"; echo "killed by TERM: $?"
3
4
Terminated
killed by TERM: 143
```

- **`echo x | read v` leaves `v` empty.** Bash runs `read` in a subshell (a forked child), sets `v` there, and the child exits, taking the assignment with it. Four ways to get the value into the shell: `shopt -s lastpipe` (run the **last** stage in the current shell; it needs job control off, hence `set +m`, and only works in scripts), process substitution (`read v < <(echo x)`), a here-string (`read -r v <<< "x"`), or a temporary file. dash also leaves `v` empty here (measured: `dash -c 'echo x | read v; echo "v=[$v]"'` printed `v=[]`); ksh and zsh run the last stage in the current shell, so a script written for them can fail under bash.
- **A subshell is a forked copy**: its `cd` and its variable assignments vanish when it ends (`(cd /tmp; pwd)` printed `/tmp` and the next `pwd` printed `/usr`; `x=2` inside `( )` left `x=1`). **`{ ...; }` is not a subshell**: `{ x=3; }` changed `x`. `$$` stays the **parent's** PID inside a subshell, and `$BASHPID` is the real current PID, which is how the third transcript line proves the child is a new process.
- **A variable is passed to a child only if it is exported or is a prefix assignment for that one command.** `v=plain bash -c '...'` gave the child `plain` for that command only, and `export v=exported` gave it to every later child.
- **`$?` and `PIPESTATUS`.** A pipeline's status is the status of its **last** command. `false | true` has status 0 and `PIPESTATUS` of `1 0`. **`yes | head -1` ended with `141 0`**: `head` quit after one line, `yes` wrote to a closed pipe and was killed by `SIGPIPE` (signal 13), and the shell reports a death by signal as **128 + signal number** (128 + 13 = 141). With `set -o pipefail` the pipeline's status becomes that of the **rightmost command that failed** (here 1).
- **Exit statuses.** `sh -c "exit 3"` returned 3; `( exit 4 )` returned 4; a child killed by `SIGTERM` showed `Terminated` and status **143** (128 + 15). By convention 0 is success, 1 is a general error, 2 is misuse of a builtin or a usage error, 126 means "found but not executable", 127 means "not found" (both from memory), and values above 128 mean death by a signal.

---

## 6. Exit status and `set -e`: what it does and does not do

`set -e` (also `-o errexit`) means "exit if a simple command fails". The manual's rule has so many exceptions that experienced people avoid depending on it. The experiment runs 26 snippets under bash and dash and records what each did **after `set -e`**: `reached` means the script continued to the final `echo`, `exit N` means it stopped with that status.

```bash
#!/usr/bin/env bash
# e6_errexit.sh: what set -e does and does not do. Each case runs in a fresh shell with set -e.
cases=(
  'false; echo reached'
  'false || true; echo reached'
  'if false; then :; fi; echo reached'
  'false && true; echo reached'
  'true && false; echo reached'
  '! true; echo reached'
  'f(){ false; echo in-f; }; f || echo h; echo reached'
  'f(){ false; echo in-f; }; f; echo reached'
  'x=$(false); echo reached'
  'g(){ local x=$(false); echo in-g; }; g; echo reached'
  'echo "$(false)"; echo reached'
  'false | true; echo reached'
  'set -o pipefail; false | true; echo reached'
  '(false; echo in-subshell); echo reached'
  'x=0; ((x++)); echo reached'
  'x=0; let x++; echo reached'
  'x=0; x=$((x+1)); echo reached'
  'while false; do :; done; echo reached'
  'until true; do :; done; echo reached'
  'false; true; echo reached'
  'cmd=(false); "${cmd[@]}"; echo reached'
  'echo $undefined_var; echo reached'
  'set -u; echo $undefined_var; echo reached'
  'set -u; echo ${undefined_var:-default}; echo reached'
  'set -u; a=(); echo "${a[@]}"; echo reached'
  'set -u; echo "$1"; echo reached'
)
printf '%-4s %-52s %-9s %-9s\n' '#' 'command (run after set -e)' 'bash' 'dash'
n=0
for c in "${cases[@]}"; do
  n=$((n+1))
  r=()
  for sh in bash dash; do
    out=$($sh -c "set -e; $c" 2>&1) ; st=$?
    if [[ $out == *reached* ]]; then r+=("reached"); elif [[ $st == 0 ]]; then r+=("ok-noreach"); else r+=("exit $st"); fi
  done
  printf '%-4s %-52s %-9s %-9s\n' "$n" "${c:0:52}" "${r[0]}" "${r[1]}"
done

echo
echo '--- the function called from ||: set -e is switched off inside it'
bash -c 'set -e; f() { false; echo "  f kept running after false"; }; f || echo "  handler ran"; echo "  reached"'
echo '--- command substitution and inherit_errexit'
bash -c 'set -e; x=$(false; echo hi); echo "  default:          x=[$x]"'
bash -c 'set -e; shopt -s inherit_errexit; x=$(false; echo hi); echo "  inherit_errexit:  x=[$x]"' || echo "  inherit_errexit: the assignment failed, shell exited with $?"
echo '--- an ERR trap with -E reports the line'
bash -c 'set -eE; trap '"'"'echo "  ERR trap: line $LINENO, status $?"'"'"' ERR; true; false; echo never'
```

```text
#    command (run after set -e)                           bash      dash
1    false; echo reached                                  exit 1    exit 1
2    false || true; echo reached                          reached   reached
3    if false; then :; fi; echo reached                   reached   reached
4    false && true; echo reached                          reached   reached
5    true && false; echo reached                          exit 1    exit 1
6    ! true; echo reached                                 reached   reached
7    f(){ false; echo in-f; }; f || echo h; echo reached  reached   reached
8    f(){ false; echo in-f; }; f; echo reached            exit 1    exit 1
9    x=$(false); echo reached                             exit 1    exit 1
10   g(){ local x=$(false); echo in-g; }; g; echo reached reached   reached
11   echo "$(false)"; echo reached                        reached   reached
12   false | true; echo reached                           reached   reached
13   set -o pipefail; false | true; echo reached          exit 1    exit 2
14   (false; echo in-subshell); echo reached              exit 1    exit 1
15   x=0; ((x++)); echo reached                           exit 1    exit 127
16   x=0; let x++; echo reached                           exit 1    exit 127
17   x=0; x=$((x+1)); echo reached                        reached   reached
18   while false; do :; done; echo reached                reached   reached
19   until true; do :; done; echo reached                 reached   reached
20   false; true; echo reached                            exit 1    exit 1
21   cmd=(false); "${cmd[@]}"; echo reached               exit 1    exit 2
22   echo $undefined_var; echo reached                    reached   reached
23   set -u; echo $undefined_var; echo reached            exit 1    exit 2
24   set -u; echo ${undefined_var:-default}; echo reached reached   reached
25   set -u; a=(); echo "${a[@]}"; echo reached           reached   exit 2
26   set -u; echo "$1"; echo reached                      exit 1    exit 2

--- the function called from ||: set -e is switched off inside it
  f kept running after false
  reached
--- command substitution and inherit_errexit
  default:          x=[hi]
  inherit_errexit: the assignment failed, shell exited with 1
--- an ERR trap with -E reports the line
  ERR trap: line 1, status 1
```

Reading the table (the rules come from the GNU manual's description of `set -e`, fetched, and the table confirms each):

- **Does stop:** a bare failing command (case 1); the **last** command of an `&&` list (5: `true && false`); a function that is called plainly (8); a failed assignment from a command substitution (9: `x=$(false)`); a failure in a subshell, which then exits with that status and ends the parent too (14); the arithmetic commands `((x++))` and `let x++` **when the result is 0**, because an arithmetic command's status is 1 when its value is zero (15 and 16; `x=$((x+1))` in case 17 never fails); an unset variable under `set -u` (23, 26).
- **Does not stop:** anything that is the **condition** of `if`, `while` or `until` (3, 18, 19); every command of an `&&` or `||` list except the one after the last operator (2, 4); a `!` negated command (6); a **non-last stage of a pipeline** unless `pipefail` is set (12 reached, 13 with `pipefail` stopped); the **command substitution inside another command** (11: `echo "$(false)"` reached, because the failing command is inside the argument and `echo` itself succeeded); and `local x=$(false)` (10), because the status seen is `local`'s, not the substitution's. The usual fix is two lines: `local x; x=$(cmd)`.
- **The trap in the middle.** Case 7 called `f || echo h`. The `f` is in a context where `-e` is ignored (it is part of an `||` list), **and that exemption reaches inside the function**, so `false` did not stop it and `in-f` would have printed. The supplement printed `f kept running after false`. That matches the manual's rule that a function or compound command run where `-e` is ignored has no `-e` effect inside it. Practical effect: **wrapping a function in `if f; then` or `f || ...` silently turns off `set -e` for the entire function.**
- **Command substitutions** do not inherit `-e` by default: `x=$(false; echo hi)` produced `x=[hi]`. With `shopt -s inherit_errexit` the inner `false` stopped the substitution, the assignment failed, and the shell exited with 1.
- **The `ERR` trap** (with `set -E` so that functions and substitutions inherit it) reported "line 1, status 1": it is the right tool for printing **where** a script failed, and section 9 uses it.
- **Bash against dash.** The two agree on every rule that exists in both; where a feature does not exist in dash the case fails with a **syntax or "not found" error** and a different status (`pipefail` and arrays gave exit 2, `((` and `let` gave 127). Portable scripts avoid those features.

The recommended header for a script of more than a few lines is `set -Eeuo pipefail`, with three honest caveats from the table: `-e` is a **safety net, not error handling**; the pipeline, `local`, `||` and `if` holes above remain; and `-u` makes `${a[@]}` on an empty array safe in bash 5.2 (case 25 reached) but not in dash or old bash. When a failure must be handled, test it explicitly: `if ! cmd; then ...` or `cmd || die "..."`.

---

## 7. Parameter expansion, arrays, arithmetic and `read`

```bash
#!/usr/bin/env bash
# e8_params.sh: parameter expansion, arrays, scoping and arithmetic, each with its result
. "$(dirname "$0")/h.sh"
f=/home/user/archive.tar.gz
show 'f=/home/user/archive.tar.gz; echo "${f##*/} | ${f%/*} | ${f%.*} | ${f%%.*} | ${f#*.} | ${f##*.}"'
show 'f=/home/user/archive.tar.gz; echo "${#f} ${f:6:4} ${f: -7} ${f/user/root} ${f//a/A}"'
show 'w="hello world"; echo "${w^} ${w^^} ${w,,} ${w^^[lo]}"'
show 'unset a b; echo "[${a:-dflt}] [${a-dflt}] [$a]"; b=; echo "[${b:-dflt}] [${b-dflt}] [${b:+set}]"'
show 'unset a; : "${a:=assigned}"; echo "$a"; bash -c '"'"': "${zz:?zz must be set}"; echo not-reached'"'"'; echo "status $?"'
show 'arr=(one "two words" three); echo "${#arr[@]} ${arr[1]} ${arr[@]:1:2}"; arr+=(four); printf "<%s>" "${arr[@]}"; echo'
show 'arr=(a b c d); unset "arr[1]"; echo "${#arr[@]} idx: ${!arr[@]}"'
show 'declare -A age=([bob]=30 [alice]=25); age[carol]=41; for k in "${!age[@]}"; do echo "$k=${age[$k]}"; done | sort; echo "${#age[@]} keys"'
show 'mapfile -t lines < <(printf "a\nb\nc\n"); echo "${#lines[@]} lines, last=${lines[-1]}"'
show 'arr=(a b c d); unset "arr[1]"; declare -p arr; arr=(x y); declare -p arr'
show 'echo $((7/2)) $((-7/2)) $((-7%3)) $((2**10)) $((1<<40)) $((0x1f)) $((010)) $((10#010))'
show 'echo $((2**63)) $((2**63-1)) $((9223372036854775807+1))'
show 'x=5; [[ $x -lt 10 ]] && echo "numeric: 5 -lt 10 is true"; [[ $x < 10 ]] && echo "string: 5 < 10 is true" || echo "string: 5 < 10 is false"; [[ 9 < 10 ]] || echo "string: 9 < 10 is false"'
show 'i=5; ((i > 3)) && echo "arith test"; echo $((i++)) $i $((++i)) $i'
show 'g=global; f1() { local g=local1; f2; }; f2() { echo "f2 sees g=$g"; }; f1; f2'
show 'f() { local x=1; ( x=2 ); echo $x; }; f'
show 'printf "%05.1f|%-6s|%6s|%x|%o|%e\n" 3.14159 ab cd 255 8 12345.678'
show 'printf "%(%Y-%m-%d)T\n" 0; printf "%s\n" "$(printf "%3d%%" 50)"'
show 'IFS=, read -r a b c <<< "1,2,3,4,5"; echo "a=$a b=$b c=$c"'
show 'read -r -a parts <<< "x y  z"; echo "${#parts[@]}"'
show 'echo $RANDOM $RANDOM | awk "{print (\$1>=0 && \$1<32768 && \$2<32768) ? \"in range\" : \"out\"}"; echo $SECONDS | grep -qE "^[0-9]+$" && echo "SECONDS is an integer"'
```

```text
$ f=/home/user/archive.tar.gz; echo "${f##*/} | ${f%/*} | ${f%.*} | ${f%%.*} | ${f#*.} | ${f##*.}"
archive.tar.gz | /home/user | /home/user/archive.tar | /home/user/archive | tar.gz | gz
$ f=/home/user/archive.tar.gz; echo "${#f} ${f:6:4} ${f: -7} ${f/user/root} ${f//a/A}"
25 user .tar.gz /home/root/archive.tar.gz /home/user/Archive.tAr.gz
$ w="hello world"; echo "${w^} ${w^^} ${w,,} ${w^^[lo]}"
Hello world HELLO WORLD hello world heLLO wOrLd
$ unset a b; echo "[${a:-dflt}] [${a-dflt}] [$a]"; b=; echo "[${b:-dflt}] [${b-dflt}] [${b:+set}]"
[dflt] [dflt] []
[dflt] [] []
$ unset a; : "${a:=assigned}"; echo "$a"; bash -c ': "${zz:?zz must be set}"; echo not-reached'; echo "status $?"
assigned
bash: line 1: zz: zz must be set
status 127
$ arr=(one "two words" three); echo "${#arr[@]} ${arr[1]} ${arr[@]:1:2}"; arr+=(four); printf "<%s>" "${arr[@]}"; echo
3 two words two words three
<one><two words><three><four>
$ arr=(a b c d); unset "arr[1]"; echo "${#arr[@]} idx: ${!arr[@]}"
3 idx: 0 2 3
$ declare -A age=([bob]=30 [alice]=25); age[carol]=41; for k in "${!age[@]}"; do echo "$k=${age[$k]}"; done | sort; echo "${#age[@]} keys"
alice=25
bob=30
carol=41
3 keys
$ mapfile -t lines < <(printf "a\nb\nc\n"); echo "${#lines[@]} lines, last=${lines[-1]}"
3 lines, last=c
$ arr=(a b c d); unset "arr[1]"; declare -p arr; arr=(x y); declare -p arr
declare -a arr=([0]="a" [2]="c" [3]="d")
declare -a arr=([0]="x" [1]="y")
$ echo $((7/2)) $((-7/2)) $((-7%3)) $((2**10)) $((1<<40)) $((0x1f)) $((010)) $((10#010))
3 -3 -1 1024 1099511627776 31 8 10
$ echo $((2**63)) $((2**63-1)) $((9223372036854775807+1))
-9223372036854775808 9223372036854775807 -9223372036854775808
$ x=5; [[ $x -lt 10 ]] && echo "numeric: 5 -lt 10 is true"; [[ $x < 10 ]] && echo "string: 5 < 10 is true" || echo "string: 5 < 10 is false"; [[ 9 < 10 ]] || echo "string: 9 < 10 is false"
numeric: 5 -lt 10 is true
string: 5 < 10 is false
string: 9 < 10 is false
$ i=5; ((i > 3)) && echo "arith test"; echo $((i++)) $i $((++i)) $i
arith test
5 6 7 7
$ g=global; f1() { local g=local1; f2; }; f2() { echo "f2 sees g=$g"; }; f1; f2
f2 sees g=local1
f2 sees g=global
$ f() { local x=1; ( x=2 ); echo $x; }; f
1
$ printf "%05.1f|%-6s|%6s|%x|%o|%e\n" 3.14159 ab cd 255 8 12345.678
003.1|ab    |    cd|ff|10|1.234568e+04
$ printf "%(%Y-%m-%d)T\n" 0; printf "%s\n" "$(printf "%3d%%" 50)"
1970-01-01
 50%
$ IFS=, read -r a b c <<< "1,2,3,4,5"; echo "a=$a b=$b c=$c"
a=1 b=2 c=3,4,5
$ read -r -a parts <<< "x y  z"; echo "${#parts[@]}"
3
$ echo $RANDOM $RANDOM | awk "{print (\$1>=0 && \$1<32768 && \$2<32768) ? \"in range\" : \"out\"}"; echo $SECONDS | grep -qE "^[0-9]+$" && echo "SECONDS is an integer"
in range
SECONDS is an integer
```

**Parameter expansion** does the work that people reach for `sed` and `basename` to do, without a process:

| Form | Meaning | Measured example |
| --- | --- | --- |
| `${v#pat}`, `${v##pat}` | remove shortest, longest **prefix** matching `pat` | `${f##*/}` gave `archive.tar.gz`; `${f#*.}` gave `tar.gz` |
| `${v%pat}`, `${v%%pat}` | remove shortest, longest **suffix** | `${f%/*}` gave `/home/user`; `${f%.*}` stripped `.gz`; `${f%%.*}` stripped everything after the first dot |
| `${#v}` | length | 25 |
| `${v:off:len}` | substring (negative offset needs a space or parentheses: `${f: -7}`) | `${f:6:4}` gave `user` |
| `${v/pat/new}`, `${v//pat/new}` | replace first, all | `//a/A` replaced every `a` |
| `${v^}`, `${v^^}`, `${v,,}` | case change (bash 4) | `HELLO WORLD`, `hello world` |
| `${v:-d}` / `${v-d}` | default if **unset or empty** / if **unset** | `${b:-dflt}` gave `dflt`, `${b-dflt}` gave the empty string |
| `${v:=d}`, `${v:+alt}` | assign default; alternate if set and non-empty | `: "${a:=assigned}"` set `a` |
| `${v:?msg}` | **abort** with `msg` if unset or empty | printed `zz must be set` and the script stopped (status 127 in bash 5.2) |

The pair `:-` against `-` is the standard trap: with the colon the test is "unset or empty", without it "unset only". A line such as `${1:?usage: prog FILE}` is the cheapest argument check there is.

**Arrays.** An indexed array holds each element as a separate word: `arr=(one "two words" three)` has 3 elements, `"${arr[@]}"` expands to exactly those 3 words (the other exemption from word splitting), and `arr+=(four)` appends. After `unset "arr[1]"` the array had **3 elements with indices 0, 2 and 3**: indices are **sparse**, so `"${#arr[@]}"` is a count and not "last index plus one". Re-assigning the whole array compacts it. **Associative arrays** (`declare -A`) map strings to strings and iterate in **no defined order** (the transcript sorts the output to make it stable). `mapfile -t lines < <(cmd)` reads lines into an array without the word-splitting traps of `$(...)`. `declare -p name` prints a variable in a form that can be pasted back, which makes it the best debugging aid for arrays.

**Arithmetic** is **64-bit signed**: `$((7/2))` is 3 and `$((-7/2))` is **-3** (truncation toward zero, as in C), `$((-7%3))` is **-1**, `$((1<<40))` is 1099511627776, and `$((2**63))` **wraps to -9223372036854775808**, as does `9223372036854775807+1`: **there is no overflow error**. A leading zero means **octal** (`$((010))` is 8, and `08` is an error); force decimal with `10#` (`$((10#010))` is 10). There is no floating point (use `bc` or `awk`). Inside `[[ ]]`, `<` and `>` compare **strings**, so `[[ 9 < 10 ]]` is false; for numbers use `-lt`, `-gt` or `(( ))`. `echo $((i++)) $i $((++i)) $i` printed `5 6 7 7`, the post-increment and pre-increment of C.

**Scope.** Variables are **global by default**; `local` makes a variable function-scoped, but bash scoping is **dynamic**, not lexical: `f2` printed `g=local1` when called from `f1` (which had its own local `g`) and `g=global` when called from the top. A subshell inside a function does not change the function's local (`f` printed 1 after `( x=2 )`).

**`printf` and `read`.** `printf` reuses its format for extra arguments (`printf "%s " "$n" "$@"` printed all of `1 2 3`), supports `%x`, `%o`, `%e`, `%05.1f` and, in bash, `%(%Y-%m-%d)T` for dates without forking `date`. `IFS=, read -r a b c <<< "1,2,3,4,5"` gave `c=3,4,5`: the **last variable receives the remainder**. `read -r -a parts` splits into an array. Always use **`read -r`** (without it a backslash escapes the next character) and **`IFS=`** when you want the line unchanged (without it leading and trailing whitespace is trimmed).

**Positional parameters and `getopts`.**

```bash
#!/usr/bin/env bash
# e8b_scope.sh: ${@:2}, shift, and positional-parameter handling in functions
. "$(dirname "$0")/h.sh"
show 'f() { echo "$# args, first=$1, rest=${*:2}, last=${!#}"; shift 2; echo "after shift 2: $# left: $*"; }; f a b c d e'
show 'f() { local n=$1; shift; printf "%s " "$n" "$@"; echo; }; f 1 2 3'
show 'set -- -a -b file; while [ $# -gt 0 ]; do case $1 in -a) echo "flag a";; -b) echo "flag b";; --) shift; break;; -*) echo "unknown $1";; *) echo "operand $1";; esac; shift; done'
```

```text
$ f() { echo "$# args, first=$1, rest=${*:2}, last=${!#}"; shift 2; echo "after shift 2: $# left: $*"; }; f a b c d e
5 args, first=a, rest=b c d e, last=e
after shift 2: 3 left: c d e
$ f() { local n=$1; shift; printf "%s " "$n" "$@"; echo; }; f 1 2 3
1 2 3
$ set -- -a -b file; while [ $# -gt 0 ]; do case $1 in -a) echo "flag a";; -b) echo "flag b";; --) shift; break;; -*) echo "unknown $1";; *) echo "operand $1";; esac; shift; done
flag a
flag b
operand file
```

`$#` is the argument count; `${*:2}` is "all but the first"; `${!#}` is the **last** argument (indirect expansion of `$#`); `shift N` drops the first N. The `while ... case ... shift` loop above is the manual way to parse options and is what `getopts` automates for single-letter options (used in the template in section 9). It handles `--` (end of options) and treats `-*` unknown options as errors. For **long options** (`--output=x`) use a `case` loop like this one, or GNU `getopt` (an external program, not a builtin and not on every system).

---

## 8. Jobs, process groups and sessions

A **job** is a pipeline the shell started. In an interactive shell, **job control** puts each job in its own **process group**, so that Ctrl-C (which the terminal driver sends as `SIGINT` to the **foreground process group**) reaches only the foreground job. Scripts have job control **off** by default, which changes what `&` does. See [OS 21](../05_operating_systems/21_os_interview_question_bank.md), section 8.3, for the terminal side of this; here is the measurement on the shell side.

```bash
#!/usr/bin/env bash
# e7_jobs.sh: process groups, sessions and SIGHUP for background work (non-interactive shell, so set -m is used where noted)
rel() { # rel LABEL PID: describe a process by its relation to this shell, not by number
  local label=$1 pid=$2 pg sid
  read -r pg sid < <(ps -o pgid=,sid= -p "$pid")
  local same_pg=no same_sid=no
  [ "$pg" = "$(ps -o pgid= -p $$ | tr -d ' ')" ] && same_pg=yes
  [ "$sid" = "$(ps -o sid= -p $$ | tr -d ' ')" ] && same_sid=yes
  [ "$pg" = "$pid" ] && own=yes || own=no
  printf '%-34s same group as shell: %-3s  same session: %-3s  leads own group: %s\n' "$label" "$same_pg" "$same_sid" "$own"
}
sigign() { awk '/SigIgn/ {print $2}' /proc/$1/status; }
sleep 30 & p1=$!
rel 'sleep 30 &   (no job control)' $p1
set -m
sleep 30 & p2=$!
rel 'sleep 30 &   (after set -m)' $p2
set +m
setsid sleep 30 & p3=$!
sleep 0.2
rel 'setsid sleep 30 &' $p3
nohup sleep 30 >/dev/null 2>&1 & p4=$!
sleep 0.2
echo
echo 'SigIgn mask (bit 0 set means SIGHUP is ignored):'
printf '  %-28s %s\n' 'plain background' "$(sigign $p1)"
printf '  %-28s %s\n' 'set -m background' "$(sigign $p2)"
printf '  %-28s %s\n' 'nohup' "$(sigign $p4)"
printf '  %-28s %s\n' 'setsid' "$(sigign $p3)"
kill $p1 $p2 $p3 $p4 2>/dev/null
wait 2>/dev/null
echo
sleep 0.1 & wait $!; echo "wait \$! returned $? for a normal exit"
sleep 5 & q=$!; kill -TERM $q; wait $q; echo "wait \$! returned $? after SIGTERM (128 + 15)"
( sleep 0.1; exit 7 ) & wait $!; echo "wait \$! returned $? for exit 7"
echo
echo 'jobs in a non-interactive shell (the table exists; -m only adds process groups):'
sleep 5 & jobs; set -m; sleep 5 & jobs; kill %1 %2 2>/dev/null; wait 2>/dev/null
```

```text
sleep 30 &   (no job control)      same group as shell: yes  same session: yes  leads own group: no
sleep 30 &   (after set -m)        same group as shell: no   same session: yes  leads own group: yes
setsid sleep 30 &                  same group as shell: no   same session: no   leads own group: yes

SigIgn mask (bit 0 set means SIGHUP is ignored):
  plain background             0000000000000006
  set -m background            0000000000000000
  nohup                        0000000000000007
  setsid                       0000000000000006

wait $! returned 0 for a normal exit
wait $! returned 143 after SIGTERM (128 + 15)
wait $! returned 7 for exit 7

jobs in a non-interactive shell (the table exists; -m only adds process groups):
[1]+  Running                 sleep 5 &
[1]-  Running                 sleep 5 &
[2]+  Running                 sleep 5 &
[2]+  Terminated              sleep 5
```

- **A plain `cmd &` in a script shares the script's process group and session** (first line: same group yes, same session yes, leads own group no). A terminal Ctrl-C therefore reaches the background job too, **except** that the shell makes the job ignore `SIGINT` and `SIGQUIT` on purpose: the `SigIgn` mask `0x6` means bits 1 and 2 (signals 2 and 3) are ignored. That is why the template test in section 9 found that a `SIGINT` sent to a background job was **ignored**.
- **`set -m` (job control on)** gives the job its **own process group** (leader of a new group) and, as the second mask shows, **does not** ignore the signals (`0x0`). That is what an interactive shell does.
- **`setsid cmd`** starts the command in a **new session**, detached from the controlling terminal (`same session: no`). It keeps the `SIGINT`/`SIGQUIT` mask `0x6` (setsid does not change it). A daemon does this so a closed terminal cannot hang it up.
- **`nohup cmd`** additionally sets `SIGHUP` to ignored (mask `0x7`, the extra bit being signal 1), so the command survives the terminal's hang-up. It does not change the session or group. **`disown`** removes a job from the shell's table so the shell will not send it `SIGHUP` on exit. (A real hang-up needs a terminal; I could not test one here.)
- **`wait $!`** waits for the most recent background job and returns **its** status: 0 for a normal exit, **143 after `SIGTERM`** (128 + 15), 7 for `exit 7`. `wait` with no argument waits for all children and returns 0; this makes `cmd1 & cmd2 & wait` the shell's fork/join, with the catch that you must collect each PID's status if you need failures (`wait "$pid"`).
- **`jobs`** works in non-interactive shells too; the table shows `Running` and `Terminated` entries, with `+` and `-` marking the current and previous job.

The rules to remember: **Ctrl-C goes to the foreground process group**; **a background job in a script is not shielded from `SIGTERM` or `SIGHUP`**; **to survive logout use `nohup` or `setsid`** (or better a service manager, see LX 04 in the plan); and **to stop a script and everything it started you must signal the group** (`kill -- -PGID`), not just the script's PID, as the next section demonstrates.

---

## 9. A robust script, line by line, and how it fails

Everything so far is assembled into one small program: `linecount.sh` counts the lines of each file named on the command line and writes a report, sorted by count, to a file. The report must appear **all at once or not at all**, only one copy may run at a time, and the script must clean up whatever happens. It is short on purpose; the point is the **frame** around the work, which you can reuse.

```bash
#!/usr/bin/env bash
# linecount: count the lines of each file given and write a report, sorted by count,
# to a file. The report appears all at once or not at all.
set -Eeuo pipefail

readonly PROG=${0##*/}
readonly EX_USAGE=64 EX_NOINPUT=66 EX_TEMPFAIL=75 EX_NOTFOUND=127
VERBOSE=0
OUT=report.txt
TMP=
LOCK=${TMPDIR:-/tmp}/$PROG.lock

log() { printf '%s: %s\n' "$PROG" "$*" >&2; }
vlog() { if ((VERBOSE)); then log "$@"; fi; }
die() { log "error: $1"; exit "${2:-1}"; }

usage() {
    cat <<EOF
Usage: $PROG [-o FILE] [-v] FILE...
  -o FILE  write the report to FILE (default: $OUT)
  -v       log progress to stderr
  -h       show this help
EOF
}

cleanup() {
    local status=$?
    trap - EXIT
    if [[ -n $TMP ]]; then rm -f -- "$TMP"; fi
    vlog "cleanup done, exit status $status"
    exit "$status"
}
trap cleanup EXIT
trap 'log "line $LINENO: $BASH_COMMAND failed"' ERR
trap 'exit 130' INT
trap 'exit 143' TERM

main() {
    local opt c f lines
    while getopts ':o:vh' opt; do
        case $opt in
            o) OUT=$OPTARG ;;
            v) VERBOSE=1 ;;
            h) usage; exit 0 ;;
            :) die "option -$OPTARG needs an argument" "$EX_USAGE" ;;
            \?) die "unknown option -$OPTARG" "$EX_USAGE" ;;
        esac
    done
    shift $((OPTIND - 1))
    if (($# == 0)); then usage >&2; exit "$EX_USAGE"; fi

    for c in wc sort mktemp flock; do
        command -v "$c" >/dev/null || die "missing command: $c" "$EX_NOTFOUND"
    done

    exec 9>"$LOCK"
    flock -n 9 || die "another instance is running" "$EX_TEMPFAIL"

    TMP=$(mktemp "$OUT.XXXXXX")
    vlog "writing $# file(s) via $TMP"
    for f in "$@"; do
        [[ -r $f ]] || die "cannot read: $f" "$EX_NOINPUT"
        lines=$(wc -l < "$f")
        printf '%-12s %6d\n' "$f" "$lines"
        if [[ -n ${LINECOUNT_DELAY:-} ]]; then sleep "$LINECOUNT_DELAY"; fi
    done | sort -k2,2nr > "$TMP"
    mv -- "$TMP" "$OUT"
    TMP=
    vlog "wrote $OUT"
}

main "$@"
```

How each part earns its place:

- **Line 1, `#!/usr/bin/env bash`.** The kernel reads this line and starts the named interpreter (see [OS 02](../05_operating_systems/02_processes.md) on `execve`). `env` finds `bash` on `PATH`, which works where bash is not in `/bin` (BSD, NixOS, Homebrew); a hard-coded `/bin/bash` is more predictable for a root-owned daemon. Use `#!/bin/sh` **only** if the script is checked against dash (section 12).
- **Line 4, `set -Eeuo pipefail`**: the safety net of section 6, with `-E` so the `ERR` trap applies inside functions.
- **Lines 6 to 11, constants and state.** `readonly` for what must not change; `EX_*` names for the exit codes (64 is `EX_USAGE`, 66 `EX_NOINPUT`, 75 `EX_TEMPFAIL` from BSD's `sysexits.h`, from memory; 127 matches the shell's "not found"). The lock path uses `${TMPDIR:-/tmp}`, so a caller can move it.
- **Lines 13 to 15, three helpers.** `log` writes to **stderr** with the program name (diagnostics must not mix with data on stdout), `vlog` logs only with `-v`, and `die MESSAGE [CODE]` logs and exits with a **distinct code**. The quoting of `"$*"` in `log` is deliberate: the message is one word.
- **Lines 17 to 24, `usage`** is a here-document, so it can use `$PROG` and `$OUT`.
- **Lines 26 to 36, the cleanup design.** `cleanup` first saves `$?` (the status that is about to be returned), **removes the `EXIT` trap** (`trap - EXIT`, so it cannot run twice if `exit` is called from inside it), deletes the temporary file if one exists, and exits with the **saved** status. It is attached to `EXIT`, which fires on a normal end, an `exit`, a `set -e` failure and (via the next two lines) signals. `INT` and `TERM` handlers call `exit 130` and `exit 143` so the `EXIT` trap then runs, and the status follows the 128 + signal rule. The `ERR` trap prints the failing line and command: that is what produced `line 66: sort -k2,2nr > "$TMP" failed` in the failure test below.
- **Lines 40 to 50, option parsing.** `getopts ':o:vh'` with a **leading colon** is silent mode: a missing argument sets `opt` to `:` and an unknown option sets it to `?`, which the `case` turns into usage errors (64). `shift $((OPTIND - 1))` discards the parsed options; no operands is also a usage error. `-h` prints to stdout and exits 0 (asking for help is not an error); errors print usage to **stderr**.
- **Lines 52 to 54, dependency check.** `command -v` tests for each external command up front, so a missing tool is a clean exit 127 and not a failure half way through.
- **Lines 56 to 57, the lock.** `exec 9>"$LOCK"` opens descriptor 9 on the lock file for the **rest of the script** (the `exec` with no command only changes descriptors, section 4), and `flock -n 9` tries to lock it **without waiting**; if another instance holds it, the script exits 75 (`EX_TEMPFAIL`: "try again later"). The kernel drops the lock when the process dies, so there is no stale lock to clean up (shown in the SIGKILL test).
- **Line 59, `mktemp "$OUT.XXXXXX"`** creates a **new file with a random name, mode 600, using `O_EXCL`** next to the destination, so that the final step is an atomic `mv` on the same file system.
- **Lines 61 to 66, the work.** Quoted `"$@"`; `[[ -r $f ]]` with no quoting needed; `lines=$(wc -l < "$f")` with the file as redirected input so `wc` prints only the number (and so a name starting with `-` is harmless); `printf` with fixed-width fields; the whole loop is **piped to `sort`**, whose output goes to the temporary file.
- **Lines 67 to 69, the publish step.** `mv -- "$TMP" "$OUT"` replaces the report in one atomic rename, then `TMP=` tells `cleanup` there is nothing left to delete. A reader of `OUT` therefore sees the old report or the complete new one, never a partial one.

### 9.1 The test run

A script is not finished until its failure paths have been run. The test file calls it with every kind of failure and then with signals.

```bash
#!/usr/bin/env bash
# e9_template_tests.sh: drive linecount.sh through its success and failure paths
S=$(cd "$(dirname "$0")" && pwd)/linecount.sh
linecount() { "$S" "$@"; }
d=$(mktemp -d) && cd "$d" || exit 1
export TMPDIR=$d
seq 1 3 > a.txt; seq 1 10 > b.txt; seq 1 1 > c.txt
t() { printf '\n$ %s\n' "$1"; eval "$1" 2>&1; echo "[exit $?]"; }
ms() { echo $(( (${EPOCHREALTIME/./} - $1) / 1000 )); }
t "linecount -h"
t "linecount"
t "linecount -z a.txt"
t "linecount -o"
t "linecount -v -o out.txt a.txt b.txt c.txt; cat out.txt"
printf '\n$ linecount -o out2.txt a.txt nosuch.txt\n'
linecount -o out2.txt a.txt nosuch.txt 2>&1; echo "[exit $?]"
echo "files left: $(ls | tr '\n' ' ')"

echo
echo '=== signals (each run in the background)'
echo '--- SIGINT to a background job of a shell without job control: the script cannot trap it'
set +m
LINECOUNT_DELAY=1 "$S" -o s1.txt a.txt b.txt 2>&1 & pid=$!
sleep 0.3; s=${EPOCHREALTIME/./}; kill -INT $pid; wait $pid; echo "[exit $?, finished normally after $(ms $s) ms more]"
set -m
echo '--- SIGTERM to the script only: the trap waits for the foreground child to finish'
LINECOUNT_DELAY=2 "$S" -v -o s2.txt a.txt b.txt 2>&1 & pid=$!
sleep 0.5; s=${EPOCHREALTIME/./}; kill -TERM $pid; wait $pid; echo "[exit $?, handled after $(ms $s) ms]"
echo '--- SIGTERM to the whole process group (what a terminal does for Ctrl-C)'
LINECOUNT_DELAY=2 "$S" -v -o s3.txt a.txt b.txt 2>&1 & pid=$!
sleep 0.5; s=${EPOCHREALTIME/./}; kill -TERM -- -$pid; wait $pid; echo "[exit $?, handled after $(ms $s) ms]"
echo '--- SIGINT to the whole process group'
LINECOUNT_DELAY=2 "$S" -v -o s4.txt a.txt b.txt 2>&1 & pid=$!
sleep 0.5; kill -INT -- -$pid; wait $pid; echo "[exit $?]"
echo "files left after the interrupted runs: $(ls | grep -c '^s[234]') report or temporary files"

echo
echo '=== a second instance while the first runs'
LINECOUNT_DELAY=1 "$S" -o one.txt a.txt b.txt 2>&1 & pid=$!
sleep 0.4
t "linecount -o two.txt a.txt"
wait $pid; echo "[first instance exit $?]"

echo
echo '=== SIGKILL: no handler can run'
LINECOUNT_DELAY=3 "$S" -o killed.txt a.txt b.txt 2>&1 & pid=$!
sleep 0.4; kill -KILL -- -$pid; wait $pid 2>/dev/null; echo "[exit $?]"
echo "temporary files left behind: $(ls | grep -c '^killed.txt\.')"
t "linecount -o after.txt a.txt; cat after.txt"
echo "(the lock is gone: the kernel released it when the process died)"
cd / && rm -rf "$d"
```

```text
$ linecount -h
Usage: linecount.sh [-o FILE] [-v] FILE...
  -o FILE  write the report to FILE (default: report.txt)
  -v       log progress to stderr
  -h       show this help
[exit 0]

$ linecount
Usage: linecount.sh [-o FILE] [-v] FILE...
  -o FILE  write the report to FILE (default: report.txt)
  -v       log progress to stderr
  -h       show this help
[exit 64]

$ linecount -z a.txt
linecount.sh: error: unknown option -z
[exit 64]

$ linecount -o
linecount.sh: error: option -o needs an argument
[exit 64]

$ linecount -v -o out.txt a.txt b.txt c.txt; cat out.txt
linecount.sh: writing 3 file(s) via out.txt.XXXXXX
linecount.sh: wrote out.txt
linecount.sh: cleanup done, exit status 0
b.txt            10
a.txt             3
c.txt             1
[exit 0]

$ linecount -o out2.txt a.txt nosuch.txt
linecount.sh: error: cannot read: nosuch.txt
linecount.sh: line 66: sort -k2,2nr > "$TMP" failed
[exit 66]
files left: a.txt b.txt c.txt linecount.sh.lock out.txt

=== signals (each run in the background)
--- SIGINT to a background job of a shell without job control: the script cannot trap it
[exit 0, finished normally after 1717 ms more]
--- SIGTERM to the script only: the trap waits for the foreground child to finish
linecount.sh: writing 2 file(s) via s2.txt.XXXXXX
linecount.sh: cleanup done, exit status 143
[exit 143, handled after 3518 ms]
--- SIGTERM to the whole process group (what a terminal does for Ctrl-C)
linecount.sh: writing 2 file(s) via s3.txt.XXXXXX
Terminated
linecount.sh: cleanup done, exit status 143
[exit 143, handled after 3 ms]
--- SIGINT to the whole process group
linecount.sh: writing 2 file(s) via s4.txt.XXXXXX
linecount.sh: cleanup done, exit status 130
[exit 130]
files left after the interrupted runs: 0 report or temporary files

=== a second instance while the first runs

$ linecount -o two.txt a.txt
linecount.sh: error: another instance is running
[exit 75]
[first instance exit 0]

=== SIGKILL: no handler can run
[exit 137]
temporary files left behind: 1

$ linecount -o after.txt a.txt; cat after.txt
a.txt             3
[exit 0]
(the lock is gone: the kernel released it when the process died)
```

**Usage and option errors.** `-h` exits 0 and prints usage; no arguments, `-z` and `-o` with no value all exit **64** with a message on stderr. **Normal use.** With `-v` the log shows the temporary name (`out.txt.XXXXXX`, masked), `wrote out.txt` and `cleanup done, exit status 0`; the report is sorted by count, `b.txt 10`, `a.txt 3`, `c.txt 1`.

**A failed input** (`nosuch.txt`): the loop `die`d with exit **66**, and the `ERR` trap fired first and named the line and command (`line 66: sort -k2,2nr > "$TMP" failed`; the loop stage exited 66, `pipefail` made the pipeline fail with that status, and the trap names the pipeline's last command, `sort`). The listing afterwards shows the three inputs, the lock file and `out.txt` from the earlier run, and **no `out2.txt` and no temporary file**: the output did not appear, and the cleanup removed its temporary.

**Signals.** The four signal tests are the reason to read this section. All four run the script in the **background of a non-interactive test shell**, which matters (section 8):

1. **`SIGINT` sent to a background job** was **ignored**: the script finished normally with status 0, 1,717 ms later. A job started with `&` by a shell without job control starts with `SIGINT` and `SIGQUIT` ignored, and a trap on `INT` cannot un-ignore a signal that was ignored **on entry** (bash's rule, measured in section 10). A script that depends on Ctrl-C cleanup must therefore be run in the foreground or in its own process group.
2. **`SIGTERM` sent to the script's PID only** was **delayed for 3,518 ms**, and then the cleanup ran and the status was 143. Bash **defers a trap until the foreground command finishes**: the script was inside `sleep` (the optional `LINECOUNT_DELAY` loop), and the signal reached bash, not the `sleep`; bash recorded it and ran the handler **when the child ended**. A script that must stop quickly has to forward the signal to its children or be signalled together with them.
3. **`SIGTERM` sent to the whole process group** (this is what a terminal does) was handled in **3 ms**: the child died of the same signal (`Terminated`), the script ran its trap at once, and the exit status was 143.
4. **`SIGINT` to the group** exited **130** with the cleanup run.

After the interrupted runs there were **0 report or temporary files** left. **A second instance** exited **75** with `another instance is running` while the first ran to a normal 0.

**`SIGKILL`** cannot be caught: the exit was **137**, **one temporary file was left behind**, and **the next run succeeded** because the kernel had released the lock when the process died (an advisory `flock` lock belongs to the open file description, so it goes when the last descriptor on it closes; from memory). The leftover temporary is the cost of an uncatchable signal; a cron job that removes `*.XXXXXX` files older than a day, or a `systemd` unit with `PrivateTmp`, is the cure.

---

## 10. Traps

A **trap** runs a command string when the shell receives a signal or reaches a condition. The conditions that are not signals are `EXIT` (the shell is about to end), `ERR` (a command failed, with the exemptions of `set -e`), `DEBUG` (before each command) and `RETURN` (a function returns).

```bash
#!/usr/bin/env bash
# e10_traps.sh: how traps behave. Each snippet runs as "bash -c SNIPPET" in a fresh shell, so nothing leaks between cases.
c() { printf '\n$ %s\n' "$1"; { bash -c "$1" 2>&1; st=$?; } 2>/dev/null; echo "[exit $st]"; }
c 'trap "echo EXIT trap ran" EXIT; echo body; exit 3'
c 'set -e; trap "echo EXIT trap ran" EXIT; false; echo not reached'
c 'trap "echo parent EXIT" EXIT; ( echo in subshell ); echo parent body'
c 'trap "echo handler" USR1; ( kill -USR1 $BASHPID; echo subshell survived )'
c 'x=1; trap "echo double-quoted: x=$x" EXIT; x=2'
c $'x=1; trap \'echo single-quoted: x=$x\' EXIT; x=2'
c 'trap "echo caught INT; exit 9" INT; kill -INT $$; echo not reached'
c $'trap "" INT; bash -c \'kill -INT $$; echo child survived: SIGINT was ignored on entry\''
c 'trap "echo got USR1" USR1; sleep 5 & p=$!; (sleep 0.3; kill -USR1 $$) & wait $p; echo "wait returned $? (128 + 10)"; kill $p'
c 'n=0; trap "n=\$((n+1))" USR1; for i in 1 2 3; do kill -USR1 $$; done; echo "handler ran $n times for 3 signals"'
c 'trap "echo ERR at line \$LINENO" ERR; f() { false; echo after-false-in-f; }; f; echo done'
c 'set -E; trap "echo ERR at line \$LINENO" ERR; f() { false; echo after-false-in-f; }; f; echo done'
c 'kill -TERM $$'
c 'kill -KILL $$'
c 'trap "echo cleanup" EXIT; exec sleep 0.1; echo not reached'
```

```text
$ trap "echo EXIT trap ran" EXIT; echo body; exit 3
body
EXIT trap ran
[exit 3]

$ set -e; trap "echo EXIT trap ran" EXIT; false; echo not reached
EXIT trap ran
[exit 1]

$ trap "echo parent EXIT" EXIT; ( echo in subshell ); echo parent body
in subshell
parent body
parent EXIT
[exit 0]

$ trap "echo handler" USR1; ( kill -USR1 $BASHPID; echo subshell survived )
User defined signal 1
[exit 138]

$ x=1; trap "echo double-quoted: x=$x" EXIT; x=2
double-quoted: x=1
[exit 0]

$ x=1; trap 'echo single-quoted: x=$x' EXIT; x=2
single-quoted: x=2
[exit 0]

$ trap "echo caught INT; exit 9" INT; kill -INT $$; echo not reached
caught INT
[exit 9]

$ trap "" INT; bash -c 'kill -INT $$; echo child survived: SIGINT was ignored on entry'
child survived: SIGINT was ignored on entry
[exit 0]

$ trap "echo got USR1" USR1; sleep 5 & p=$!; (sleep 0.3; kill -USR1 $$) & wait $p; echo "wait returned $? (128 + 10)"; kill $p
got USR1
wait returned 138 (128 + 10)
[exit 0]

$ n=0; trap "n=\$((n+1))" USR1; for i in 1 2 3; do kill -USR1 $$; done; echo "handler ran $n times for 3 signals"
handler ran 3 times for 3 signals
[exit 0]

$ trap "echo ERR at line \$LINENO" ERR; f() { false; echo after-false-in-f; }; f; echo done
after-false-in-f
done
[exit 0]

$ set -E; trap "echo ERR at line \$LINENO" ERR; f() { false; echo after-false-in-f; }; f; echo done
ERR at line 1
after-false-in-f
done
[exit 0]

$ kill -TERM $$
[exit 143]

$ kill -KILL $$
[exit 137]

$ trap "echo cleanup" EXIT; exec sleep 0.1; echo not reached
[exit 0]
```

- **`EXIT` runs on every way out except death by signal 9 and `exec`.** It ran after `exit 3` (status kept), after a `set -e` failure, and after the end of the body. **`exec sleep` replaced the shell, so the trap never ran** (the last case): `exec` leaves no shell to run it.
- **A subshell does not run the parent's `EXIT` trap** (the transcript shows `parent EXIT` printed once, by the parent).
- **A subshell also resets traps for signals**: after `trap "echo handler" USR1`, a subshell that sent itself `USR1` died of it (`User defined signal 1`, status **138** = 128 + 10) because handlers are not inherited by subshells, only "ignore" dispositions are.
- **Quote the trap string with single quotes.** With double quotes the variable was expanded **when the trap was set** (`x=1` printed), with single quotes **when the trap ran** (`x=2`). ShellCheck has a rule for this.
- **A signal ignored on entry stays ignored.** `trap "" INT` set the signal to ignored, and a **child** bash that sent itself `SIGINT` survived. The same inheritance is what happened to the background job in section 9.
- **Traps run between commands**: `wait $p` was interrupted by `USR1`, ran the handler, and **returned 138** (`128 + 10`). Three signals sent in a row to a script that trapped `USR1` ran the handler 3 times.
- **`ERR` and functions.** Without `set -E` the `ERR` trap did **not** fire for a failure inside a function; with `set -E` it did, reporting `ERR at line 1`.
- **`kill -TERM $$` and `kill -KILL $$` ended the shell with 143 and 137.**

The cleanup recipe from the template: **one `EXIT` trap that does the cleanup, signal traps that only call `exit N`, cleanup that is safe to run twice, `trap - EXIT` first.** Do not put long work in a trap, and do not rely on one for `SIGKILL`.

---

## 11. Races, locking and temporary files

Shell code has **time-of-check/time-of-use** races like any other program. The experiment is the smallest honest case: 8 background workers each read a counter file, add 1 and write it back, 250 times each.

```bash
#!/usr/bin/env bash
# e11_race.sh: lost updates without a lock, a correct count with flock, atomic appends, and predictable temp names
d=$(mktemp -d) && cd "$d" || exit 1
W=8 N=250
worker_plain() { local i n; for ((i = 0; i < N; i++)); do n=$(<counter); echo $((n + 1)) > counter; done; }
worker_flock() { local i n; for ((i = 0; i < N; i++)); do { flock 8; n=$(<counter); echo $((n + 1)) > counter; } 8>lock; done; }
worker_append() { local i; for ((i = 0; i < N; i++)); do echo "worker $1 line $i padding-padding-padding" >> log; done; }
run() { # run NAME FUNCTION
  local w start end
  echo 0 > counter
  start=$EPOCHREALTIME
  for ((w = 0; w < W; w++)); do "$2" "$w" & done
  wait
  end=$EPOCHREALTIME
  printf '%-14s expected %d, got %-5d (%d updates lost)  %d ms\n' "$1" $((W * N)) "$(<counter)" $((W * N - $(<counter))) $(( (${end/./} - ${start/./}) / 1000 ))
}
echo "$W workers, $N increments each; the counter is read, incremented and written back by plain shell"
for r in 1 2 3; do run "no lock, run $r" worker_plain; done
for r in 1 2 3; do run "flock, run $r" worker_flock; done
echo
: > log
for ((w = 0; w < W; w++)); do worker_append "$w" & done; wait
echo "append: $(wc -l < log) lines written, $(grep -c '^worker [0-9] line [0-9]* padding-padding-padding$' log) intact"
echo
echo '--- a predictable temporary name against mktemp'
echo "important data" > victim
ln -s victim "evil.$$"
echo "attacker made a symlink: evil.\$\$ -> victim"
echo "script writes to a predictable name:"
echo '$ echo "report" > evil.$$'
echo "report" > "evil.$$"
echo "victim now holds: $(cat victim)"
echo "important data" > victim; rm -f "evil.$$"
f=$(mktemp "$d/script.XXXXXX")
echo "mktemp made $(basename "$f" | sed 's/\.[A-Za-z0-9]*$/.XXXXXX/') with mode $(stat -c %a "$f"), created with O_EXCL so a planted name cannot be reused"
ln -s victim "$d/planted"
echo 'planting a symlink where the script will create a file with set -C (noclobber):'
echo '$ ( set -C; echo report > planted )'
( set -C; echo report > planted ) 2>&1 | sed 's/^[^:]*: line [0-9]*: //'
echo "victim still holds: $(cat victim)"
cd / && rm -rf "$d"
```

```text
8 workers, 250 increments each; the counter is read, incremented and written back by plain shell
no lock, run 1 expected 2000, got 6     (1994 updates lost)  196 ms
no lock, run 2 expected 2000, got 16    (1984 updates lost)  230 ms
no lock, run 3 expected 2000, got 3     (1997 updates lost)  142 ms
flock, run 1   expected 2000, got 2000  (0 updates lost)  1761 ms
flock, run 2   expected 2000, got 2000  (0 updates lost)  1844 ms
flock, run 3   expected 2000, got 2000  (0 updates lost)  1762 ms

append: 2000 lines written, 2000 intact

--- a predictable temporary name against mktemp
attacker made a symlink: evil.$$ -> victim
script writes to a predictable name:
$ echo "report" > evil.$$
victim now holds: report
mktemp made script.XXXXXX with mode 600, created with O_EXCL so a planted name cannot be reused
planting a symlink where the script will create a file with set -C (noclobber):
$ ( set -C; echo report > planted )
planted: cannot overwrite existing file
victim still holds: important data
```

- **Without a lock, three runs ended at 6, 16 and 3 instead of 2,000** (1,984 to 1,997 updates lost, in 140 to 230 ms). The loss is **larger than the usual lost update** because `echo ... > counter` first **truncates** the file: a worker that reads between the truncate and the write sees an empty file, treats it as 0, and writes 1. The counts are specific to this machine, but the lesson is general: **read-modify-write in shell is not atomic.**
- **With `flock`** each worker took an exclusive lock on a lock file around the read-modify-write and all three runs gave **exactly 2,000**, in 1.76 to 1.84 s: about 8 to 12 times slower because the workers now run one at a time and each critical section forks a `flock` process. Correctness has a price; the right design is often to **avoid shared mutable state** (one writer, append-only logs).
- **Appends are safe in a narrower sense.** 8 workers appending short lines with `>>` (opened with `O_APPEND`) produced 2,000 lines, **all 2,000 intact**. Appending a single short `write` is atomic with respect to other appends on a local file system; that does not extend to lines longer than a pipe buffer on some systems or to NFS. (The atomicity of `O_APPEND` writes is from memory; the measurement shows no interleaving for these short lines.)
- **Use `mktemp`, never a guessable name.** The demonstration put a symlink named `evil.$$` pointing at a file called `victim`. A script that writes to `evil.$$` **overwrote the victim** (`victim now holds: report`). `mktemp` creates a **random name with `O_EXCL`** (fail if it exists) and mode 600, so it cannot be pre-planted. With **`set -C`** (noclobber) a plain `>` also refuses to write through a symlink that already exists (`cannot overwrite existing file`); the victim kept its data. Use `mktemp -d` for a private directory and put `rm -rf` of it in the `EXIT` trap.

The lock choices: `flock FILE command` for a command; `exec 9>file; flock -n 9` for a script (section 9); `mkdir lockdir` as a portable atomic test (it either creates the directory or fails); a PID file is **not** safe by itself (PIDs are reused and the check races).

---

## 12. POSIX `sh`, bash and portability

Scripts that begin `#!/bin/sh` run under whatever `/bin/sh` is: **dash** on Debian and Ubuntu (it is here: `/bin/sh` is `/usr/bin/dash`), bash in POSIX mode on some systems, **BusyBox `ash`** on small embedded targets, `ksh` or `bash 3.2` on other Unixes. Whether a feature works depends on which.

```bash
#!/usr/bin/env bash
# e12_portable.sh: bashisms against dash, and what the choice of shell costs at start-up
t() { # t SNIPPET: run in bash and in dash, report ok or the first error line
  local sh out st line=()
  for sh in bash dash; do
    out=$($sh -c "$1" 2>&1); st=$?; out=${out//$'\n'/ }
    if ((st == 0)); then line+=("ok: ${out:0:22}"); else line+=("FAIL($st)"); fi
  done
  printf '%-44s %-26s %-12s\n' "$1" "${line[0]}" "${line[1]}"
}
printf '%-44s %-26s %-12s\n' snippet bash dash
t '[[ abc == a* ]] && echo match'
t 'a=(1 2 3); echo ${a[1]}'
t 'echo {1..3}'
t 'cat <<< hi'
t 'echo $((2**3))'
t 'x=abc; echo ${x^^}'
t 'x=abc; echo ${x:1:1}'
t 'echo $RANDOM | grep -c .'
t 'diff <(echo a) <(echo a) && echo same'
t 'echo -e "a\tb" | od -An -c | head -1'
t 'printf "%q\n" "a b"'
t 'f() { local x=1; echo $x; }; f'
t 'echo $'"'"'a\x41'"'"
t 'set -o pipefail; echo ok'
t 'test a == a && echo equal'
t '[ a = a ] && echo equal'
t 'for ((i=0;i<2;i++)); do echo $i; done'
t 'echo ${0##*/}; : ${BASH_VERSION:?no bash}'
t 'source /dev/null && echo sourced'
t '. /dev/null && echo dotted'
t 'function f { echo fn; }; f'
echo
echo "which shell is /bin/sh here: $(readlink -f /bin/sh)"
echo
echo '--- start-up cost: 500 runs of "-c :"'
for sh in bash dash; do
  s=$EPOCHREALTIME
  for ((i = 0; i < 500; i++)); do $sh -c : ; done
  e=$EPOCHREALTIME
  printf '%-5s %5d us per start\n' $sh $(( (${e/./} - ${s/./}) / 500 ))
done
```

```text
snippet                                      bash                       dash
[[ abc == a* ]] && echo match                ok: match                  FAIL(127)
a=(1 2 3); echo ${a[1]}                      ok: 2                      FAIL(2)
echo {1..3}                                  ok: 1 2 3                  ok: {1..3}
cat <<< hi                                   ok: hi                     FAIL(2)
echo $((2**3))                               ok: 8                      FAIL(2)
x=abc; echo ${x^^}                           ok: ABC                    FAIL(2)
x=abc; echo ${x:1:1}                         ok: b                      FAIL(2)
echo $RANDOM | grep -c .                     ok: 1                      FAIL(1)
diff <(echo a) <(echo a) && echo same        ok: same                   FAIL(2)
echo -e "a\tb" | od -An -c | head -1         ok:    a  \t   b  \n       ok:    -   e       a  \t
printf "%q\n" "a b"                          ok: a\ b                   FAIL(2)
f() { local x=1; echo $x; }; f               ok: 1                      ok: 1
echo $'a\x41'                                ok: aA                     ok: $a\x41
set -o pipefail; echo ok                     ok: ok                     FAIL(2)
test a == a && echo equal                    ok: equal                  FAIL(2)
[ a = a ] && echo equal                      ok: equal                  ok: equal
for ((i=0;i<2;i++)); do echo $i; done        ok: 0 1                    FAIL(2)
echo ${0##*/}; : ${BASH_VERSION:?no bash}    ok: bash                   FAIL(2)
source /dev/null && echo sourced             ok: sourced                FAIL(127)
. /dev/null && echo dotted                   ok: dotted                 ok: dotted
function f { echo fn; }; f                   ok: fn                     FAIL(2)

which shell is /bin/sh here: /usr/bin/dash

--- start-up cost: 500 runs of "-c :"
bash   1780 us per start
dash   1517 us per start
```

- **In dash these fail**: `[[ ]]`, arrays, here-strings `<<<`, `**` in arithmetic, `${x^^}` and `${x:1:1}`, `<( )`, `printf %q`, `set -o pipefail`, `==` in `test`, `for ((;;))`, `$BASH_VERSION`, `source`, and the `function f { }` form. The status column is `FAIL(2)` for a syntax error and `FAIL(127)` for "not found". `$RANDOM` is not defined (the `grep -c .` count printed 0 lines, shown as `FAIL(1)`).
- **In dash these run but differently**: `echo {1..3}` printed `{1..3}` (no brace expansion); `echo -e "a\tb"` printed a literal `-e` in front (dash's `echo` takes no options but already turns `\t` into a tab); `$'a\x41'` printed `$a\x41`.
- **These work in both**: `local` (not in POSIX but in every real `sh`), `[ a = a ]`, `.` (the portable name for `source`), and `$(( ))` arithmetic with `+ - * / %`.
- **Start-up cost** was about 1.8 ms for bash and 1.5 ms for dash per `-c :` (single measurement, 500 runs; an earlier run gave 1.9 and 1.3). That gap is why Debian made dash `/bin/sh`: boot scripts run thousands of shell starts.

The practical rules: **say what you mean in the shebang** (`#!/bin/sh` means "dash-clean", `#!/usr/bin/env bash` means "bash features allowed"), **check a `/bin/sh` script with `shellcheck -s sh`** or run it with `dash` in CI, use `printf` instead of `echo -e`, use `[ ]` with quotes and `=` in `sh`, and when you reach for arrays or `pipefail` or `[[ ]]`, switch to bash on purpose. For anything beyond about 100 lines of logic, **switch to Python** (the standard library has `subprocess`, `argparse`, `pathlib`) and keep shell for glue.

---

## 13. Which start-up files run

```bash
#!/usr/bin/env bash
# e13_startup.sh: which start-up files each kind of shell reads (strace of openat)
d=$(mktemp -d) && cd "$d" || exit 1
export HOME=$d
for f in .bashrc .bash_profile .bash_login .profile .bash_logout; do echo "# $f" > $f; done
echo "# env file" > envfile
probe() { # probe LABEL COMMAND...
  local label=$1; shift
  strace -f -o st.txt -e trace=openat "$@" </dev/null >/dev/null 2>&1
  printf '%-34s' "$label"
  grep -E '"(/etc/profile|/etc/bash.bashrc|/etc/bash_logout|'"$d"'/[^"]+|/etc/profile.d)' st.txt | grep -v ENOENT | sed -E 's/.*"([^"]+)".*/\1/' | sed "s#$d/#~/#; s#^/etc/profile\.d/.\+#/etc/profile.d/*#" | grep -v '/etc/profile.d/$' | sort -u | tr '\n' ' '
  # files tried and missing
  echo
}
probe 'bash -c :' bash -c :
probe 'bash -ic :' bash -ic :
probe 'bash -lc :' bash -lc :
probe 'bash -ilc :' bash -ilc :
probe 'BASH_ENV=envfile bash -c :' env BASH_ENV=$d/envfile bash -c :
probe 'sh -c :  (dash)' sh -c :
probe 'sh -lc :  (dash)' sh -lc :
probe 'ENV=envfile sh -ic :  (dash)' env ENV=$d/envfile sh -ic :
echo
echo 'login-shell search order: first of ~/.bash_profile, ~/.bash_login, ~/.profile that exists'
rm .bash_profile
probe 'bash -lc :  (no .bash_profile)' bash -lc :
rm .bash_login
probe 'bash -lc :  (also no .bash_login)' bash -lc :
cd / && rm -rf "$d"
```

```text
bash -c :
bash -ic :                        /etc/bash.bashrc ~/.bashrc
bash -lc :                        /etc/profile /etc/profile.d/* ~/.bash_profile
bash -ilc :                       /etc/bash.bashrc /etc/profile /etc/profile.d/* ~/.bash_profile
BASH_ENV=envfile bash -c :        ~/envfile
sh -c :  (dash)
sh -lc :  (dash)                  /etc/profile /etc/profile.d/* ~/.profile
ENV=envfile sh -ic :  (dash)      ~/envfile

login-shell search order: first of ~/.bash_profile, ~/.bash_login, ~/.profile that exists
bash -lc :  (no .bash_profile)    /etc/profile /etc/profile.d/* ~/.bash_login
bash -lc :  (also no .bash_login) /etc/profile /etc/profile.d/* ~/.profile
```

These were recorded by tracing the files the shell opens (`strace`) with stub files in a scratch `HOME`.

| Invocation | Files read (bash) |
| --- | --- |
| `bash -c cmd`, a script | **none** (unless `BASH_ENV` is set, then that file) |
| interactive, non-login (`bash -i`) | `/etc/bash.bashrc` (a Debian addition) and `~/.bashrc` |
| login (`bash -l`) | `/etc/profile` (which reads `/etc/profile.d/*`) and the **first** of `~/.bash_profile`, `~/.bash_login`, `~/.profile` |
| `dash` login | `/etc/profile`, `~/.profile` |
| `dash` interactive | the file named by `$ENV` |

Consequences you meet in practice: a **non-interactive script does not read `~/.bashrc`**, so aliases and functions defined there are not available (and `PATH` changes made there are not applied); an **ssh session** runs a login shell for interactive logins and a non-interactive shell for `ssh host command` (which reads `.bashrc` only if the server's bash was built to, usually through `/etc/ssh` configuration, from memory); `~/.bash_profile` often sources `~/.bashrc` so the two cases match; and `BASH_ENV` is a reason a script can behave differently on a machine with a customised environment. For a script you ship, **set `PATH` explicitly** and do not rely on the caller's startup files.

---

## 14. How fast is the shell

```bash
#!/usr/bin/env bash
# e14_speed.sh: the cost of a fork and an exec, measured through shell loops
d=$(mktemp -d) && cd "$d" || exit 1
seq 1 200000 > nums
lap() { # lap LABEL COUNT: time the loop body given as the function "body"
  local s e
  s=$EPOCHREALTIME; body "$2"; e=$EPOCHREALTIME
  printf '%-44s %8d iterations %9.2f us each\n' "$1" "$2" "$(awk -v a="${s/./}" -v b="${e/./}" -v n="$2" 'BEGIN{print (b-a)/n}')"
}
body() { local i x=0; for ((i = 0; i < $1; i++)); do x=$((x + i)); done; }
lap 'arithmetic in the shell: x=$((x+i))' 200000
body() { local i x; for ((i = 0; i < $1; i++)); do x=$(printf %s "$i"); done; }
lap 'x=$(printf ...)  (builtin in a forked subshell)' 5000
body() { local i x; for ((i = 0; i < $1; i++)); do printf -v x %s "$i"; done; }
lap 'printf -v x ...  (builtin, no fork)' 200000
body() { local i x; for ((i = 0; i < $1; i++)); do x=$(/bin/echo "$i"); done; }
lap 'x=$(/bin/echo ...)  (fork + exec)' 2000
body() { local i x; for ((i = 0; i < $1; i++)); do x=$(expr "$i" + 1); done; }
lap 'x=$(expr ...)  (fork + exec)' 2000
body() { local i x; for ((i = 0; i < $1; i++)); do x=$(date +%s); done; }
lap 'x=$(date +%s)  (fork + exec, dynamic link)' 2000
body() { local i x; for ((i = 0; i < $1; i++)); do x=$(<nums); done; }
lap 'x=$(<nums)  (1.3 MB, no process)' 200
body() { local i x; for ((i = 0; i < $1; i++)); do x=$(cat nums); done; }
lap 'x=$(cat nums)  (1.3 MB, fork + exec)' 200
echo
echo '--- summing 200000 numbers four ways'
s=$EPOCHREALTIME; t=0; while read -r n; do t=$((t + n)); done < nums; e=$EPOCHREALTIME
printf '%-44s sum %d  %6d ms\n' 'while read; $((t+n))' "$t" $(( (${e/./} - ${s/./}) / 1000 ))
s=$EPOCHREALTIME; t=$(awk '{s+=$1} END{print s}' nums); e=$EPOCHREALTIME
printf '%-44s sum %d  %6d ms\n' 'awk' "$t" $(( (${e/./} - ${s/./}) / 1000 ))
s=$EPOCHREALTIME; t=$(paste -sd+ nums | bc); e=$EPOCHREALTIME
printf '%-44s sum %d  %6d ms\n' 'paste -sd+ | bc' "$t" $(( (${e/./} - ${s/./}) / 1000 ))
s=$EPOCHREALTIME; mapfile -t arr < nums; t=0; for n in "${arr[@]}"; do t=$((t + n)); done; e=$EPOCHREALTIME
printf '%-44s sum %d  %6d ms\n' 'mapfile + for' "$t" $(( (${e/./} - ${s/./}) / 1000 ))
echo
echo '--- xargs batching: 2000 arguments through echo'
s=$EPOCHREALTIME; seq 1 2000 | xargs -n1 echo >/dev/null; e=$EPOCHREALTIME
printf '%-44s %6d ms\n' 'xargs -n1 (one process per argument)' $(( (${e/./} - ${s/./}) / 1000 ))
s=$EPOCHREALTIME; seq 1 2000 | xargs echo >/dev/null; e=$EPOCHREALTIME
printf '%-44s %6d ms\n' 'xargs (batched)' $(( (${e/./} - ${s/./}) / 1000 ))
s=$EPOCHREALTIME; seq 1 2000 | xargs -n1 -P2 echo >/dev/null; e=$EPOCHREALTIME
printf '%-44s %6d ms\n' 'xargs -n1 -P2 (two at a time)' $(( (${e/./} - ${s/./}) / 1000 ))
cd / && rm -rf "$d"
```

```text
arithmetic in the shell: x=$((x+i))            200000 iterations      2.65 us each
x=$(printf ...)  (builtin in a forked subshell)     5000 iterations    369.78 us each
printf -v x ...  (builtin, no fork)            200000 iterations      3.19 us each
x=$(/bin/echo ...)  (fork + exec)                2000 iterations   1589.67 us each
x=$(expr ...)  (fork + exec)                     2000 iterations   1626.86 us each
x=$(date +%s)  (fork + exec, dynamic link)       2000 iterations   1697.45 us each
x=$(<nums)  (1.3 MB, no process)                  200 iterations   5054.23 us each
x=$(cat nums)  (1.3 MB, fork + exec)              200 iterations  11679.10 us each

--- summing 200000 numbers four ways
while read; $((t+n))                         sum 20000100000     718 ms
awk                                          sum 20000100000      17 ms
paste -sd+ | bc                              sum 20000100000      51 ms
mapfile + for                                sum 20000100000     473 ms

--- xargs batching: 2000 arguments through echo
xargs -n1 (one process per argument)           2276 ms
xargs (batched)                                   5 ms
xargs -n1 -P2 (two at a time)                  1338 ms
```

All numbers are per iteration (single runs; expect 10 to 30 percent spread):

| Operation | Time |
| --- | --- |
| `x=$((x+i))`, arithmetic in the shell | **2.65 microseconds** |
| `printf -v x ...`, a builtin, no fork | 3.19 microseconds |
| `x=$(printf ...)`, a builtin inside a forked subshell | **370 microseconds** |
| `x=$(/bin/echo ...)`, fork + exec | **1,590 microseconds** |
| `x=$(expr ...)` | 1,627 microseconds |
| `x=$(date +%s)` (a dynamically linked program) | 1,697 microseconds |
| `x=$(<file)`, 1.3 MB read by the shell, no process | 5,054 microseconds |
| `x=$(cat file)`, 1.3 MB, fork + exec | 11,679 microseconds |

Three orders of magnitude separate the layers: **in-shell work** (microseconds), **a forked subshell** (hundreds of microseconds: the `$( )` itself costs, even when the command is a builtin), and **a fork plus exec of an external program** (about 1.6 ms, here including this VM's overhead). The rule: **do not call an external program inside a loop for something the shell can do** (`$(basename "$f")` against `${f##*/}`, `$(echo ... | tr ...)` against `${v^^}`, `expr` against `$((...))`).

Summing 200,000 numbers four ways, all giving the correct 20,000,100,000:

| Method | Time |
| --- | --- |
| `while read; $((t+n))` | 718 ms |
| `mapfile` then `for` | 473 ms |
| `paste -sd+ \| bc` | 51 ms |
| `awk` | **17 ms** |

The shell loop is about **40 times slower** than awk: each iteration parses and expands a command. **Use the shell to start and connect programs, and a program (awk, sort, a Python script) to do bulk per-line work.**

**`xargs` batching.** 2,000 arguments through `echo`: one process per argument (`-n1`) took **2,276 ms**, batched (the default) **5 ms** (a few `echo` processes with many arguments each), and `-n1 -P2` (two at a time) 1,338 ms. Batching is a 450-times difference; parallelism gave less than a 2-times gain on 2 vCPUs. `find ... -exec cmd {} +` batches the same way; `-exec cmd {} \;` does not.

---

## 15. ShellCheck and debugging

**ShellCheck** is a static checker for shell scripts. Here is **ShellCheck 0.11.0** run on `bad.sh`, a short script in which nearly every line is a classic bug, and then on `good.sh`, the same job written safely.

```bash
#!/bin/bash
# bad.sh: summarise the .txt files of a directory (nearly every line is a classic bug)
dir=$1
cd $dir
for f in $(ls *.txt); do
    name=`basename $f .txt`
    if [ $name == "skip" ]; then continue; fi
    count=$(wc -l $f | cut -d' ' -f1)
    echo "$name has $count lines" >> report
done
cat report | sort -k4 -n
echo would run: rm -rf $dir/tmp*
read -p "continue? " answer
```

```bash
#!/usr/bin/env bash
# good.sh: the same job, written so that no file name can break it
set -Eeuo pipefail
dir=${1:?usage: good.sh DIR}
cd -- "$dir"
: > report
for f in ./*.txt; do
    [[ -e $f ]] || continue
    name=${f##*/}
    name=${name%.txt}
    if [[ $name == skip ]]; then continue; fi
    count=$(wc -l < "$f")
    printf '%s has %d lines\n' "$name" "$count" >> report
done
sort -k4,4n report
echo "would run: rm -rf --" "${dir:?}"/tmp*
```

```text
ShellCheck 0.11.0

$ shellcheck -f gcc bad.sh
bad.sh:4:1: warning: Use 'cd ... || exit' or 'cd ... || return' in case cd fails. [SC2164]
bad.sh:4:4: note: Double quote to prevent globbing and word splitting. [SC2086]
bad.sh:5:10: error: Iterating over ls output is fragile. Use globs. [SC2045]
bad.sh:5:15: note: Use ./*glob* or -- *glob* so names with dashes won't become options. [SC2035]
bad.sh:6:10: note: Use $(...) notation instead of legacy backticks `...`. [SC2006]
bad.sh:6:20: note: Double quote to prevent globbing and word splitting. [SC2086]
bad.sh:7:10: note: Double quote to prevent globbing and word splitting. [SC2086]
bad.sh:8:19: note: Double quote to prevent globbing and word splitting. [SC2086]
bad.sh:12:24: note: Double quote to prevent globbing and word splitting. [SC2086]
bad.sh:13:1: note: read without -r will mangle backslashes. [SC2162]
bad.sh:13:22: warning: answer appears unused. Verify use (or export if used externally). [SC2034]

$ shellcheck good.sh
(no findings)

--- both scripts on a directory holding: a;b.txt,big.txt,plain.txt,skip.txt,two words.txt,

$ bad.sh DIR </dev/null
wc: two: No such file or directory
wc: words.txt: No such file or directory
a;b has 3 lines
big has 12 lines
plain has 3 lines
two has  lines
words has  lines
would run: rm -rf DIR/tmp*

$ good.sh DIR
a;b has 3 lines
big has 12 lines
plain has 3 lines
two words has 3 lines
would run: rm -rf -- DIR/tmp*

$ good.sh   (no argument)
good.sh: line 4: 1: usage: good.sh DIR
```

What ShellCheck found in `bad.sh`, by code (the codes are the ones printed above):

| Code | Line | Problem |
| --- | --- | --- |
| SC2164 | 4 | `cd $dir` with no `|| exit`: if `cd` fails the script goes on in the wrong directory |
| SC2086 | 4, 6, 7, 8, 12 | unquoted variable: word splitting and globbing |
| SC2045 | 5 | `for f in $(ls ...)`: iterating over `ls` output (rated an **error**) |
| SC2035 | 5 | a glob starting with `*` can make a name like `-c` an option: use `./*` or `--` |
| SC2006 | 6 | legacy backticks |
| SC2162 | 13 | `read` without `-r` mangles backslashes |
| SC2034 | 13 | the variable `answer` is assigned and never used |

`good.sh` produced **no findings**. The directory test makes the difference concrete. The directory held `a;b.txt`, `big.txt`, `plain.txt`, `skip.txt` and `two words.txt`: **`bad.sh` split `two words.txt` into `two` and `words.txt`** (two `wc` errors and two lines `has  lines` with an empty count), and its last line would have run `rm -rf DIR/tmp*` with **no `--`**; `good.sh` handled every name and printed `rm -rf -- DIR/tmp*`. Run with no argument, `good.sh` stopped on `${1:?usage...}` with the usage text and the line number.

**Use it in your workflow**: run `shellcheck script.sh` before every commit (or `shellcheck -s sh` for a POSIX script), put it in CI, and **read the message** before applying a suppression comment (`# shellcheck disable=SC2086`); a suppression with a reason beside it is fine, a blanket one is not. ShellCheck cannot know what your data contains, so it errs towards quoting; it also cannot see runtime state such as the environment a script will run in.

**Debugging tools.** The transcript below traces a four-line script.

```bash
#!/usr/bin/env bash
# e17_debug.sh: tracing a script with set -x and PS4, and checking syntax without running
d=$(mktemp -d) && cd "$d" || exit 1
cat > t.sh <<'EOF'
f() {
    local a=$1
    echo "f got $a"
}
for i in 1 2; do
    f "$i"
done
EOF
cat > broken.sh <<'EOF'
if true; then
    echo missing fi
EOF
echo '$ bash -x t.sh'
bash -x t.sh 2>&1
echo
echo '$ PS4="+ env: " bash -c '"'"'declare -p PS4'"'"'   (is an exported PS4 honoured?)'
PS4="+ env: " bash -c 'declare -p PS4'
echo
{ echo 'PS4='"'"'+ ${BASH_SOURCE##*/}:${LINENO}:${FUNCNAME[0]:-main}: '"'"; cat t.sh; } > t2.sh
echo '$ head -1 t2.sh'
head -1 t2.sh
echo '$ bash -x t2.sh'
bash -x t2.sh 2>&1
echo
echo '$ bash -n broken.sh; echo "status $?"'
bash -n broken.sh; echo "status $?"
echo
echo '$ bash -n t.sh; echo "status $?"'
bash -n t.sh; echo "status $?"
echo
echo '$ bash -c '"'"'set -x; x=$(echo hi); echo "$x" > /dev/null'"'"
bash -c 'set -x; x=$(echo hi); echo "$x" > /dev/null' 2>&1
cd / && rm -rf "$d"
```

```text
$ bash -x t.sh
+ for i in 1 2
+ f 1
+ local a=1
+ echo 'f got 1'
f got 1
+ for i in 1 2
+ f 2
+ local a=2
+ echo 'f got 2'
f got 2

$ PS4="+ env: " bash -c 'declare -p PS4'   (is an exported PS4 honoured?)
declare -x PS4="+ "

$ head -1 t2.sh
PS4='+ ${BASH_SOURCE##*/}:${LINENO}:${FUNCNAME[0]:-main}: '
$ bash -x t2.sh
+ PS4='+ ${BASH_SOURCE##*/}:${LINENO}:${FUNCNAME[0]:-main}: '
+ t2.sh:6:main: for i in 1 2
+ t2.sh:7:main: f 1
+ t2.sh:3:f: local a=1
+ t2.sh:4:f: echo 'f got 1'
f got 1
+ t2.sh:6:main: for i in 1 2
+ t2.sh:7:main: f 2
+ t2.sh:3:f: local a=2
+ t2.sh:4:f: echo 'f got 2'
f got 2

$ bash -n broken.sh; echo "status $?"
broken.sh: line 3: syntax error: unexpected end of file
status 2

$ bash -n t.sh; echo "status $?"
status 0

$ bash -c 'set -x; x=$(echo hi); echo "$x" > /dev/null'
++ echo hi
+ x=hi
+ echo hi
```

- **`bash -x script`** (or `set -x` inside) prints every command **after expansion**, prefixed with `PS4` (default `+ `); nested command substitutions are prefixed with `++`, one more `+` per level (the last transcript lines show `++ echo hi` for the substitution and `+ x=hi` for the assignment). `set +x` turns it off; `bash -v` prints lines as they are read, before expansion.
- **A useful `PS4`** includes the file, line and function: `PS4='+ ${BASH_SOURCE##*/}:${LINENO}:${FUNCNAME[0]:-main}: '` produced `+ t2.sh:3:f: local a=1`. **An exported `PS4` was ignored in the one test I ran** (`PS4="+ env: " bash -c 'declare -p PS4'` printed `declare -x PS4="+ "`, bash 5.2.21, as root); I did not find the reason, so **set `PS4` inside the script**, which worked.
- **`bash -n script`** parses without running: for a script with a missing `fi` it printed `syntax error: unexpected end of file` and exited 2, and for a good script exited 0. It finds syntax errors only.
- **`trap ... ERR`** with `set -E` (section 10), **`$LINENO`**, **`$BASH_COMMAND`**, and **`caller`** (prints the call site of a function) are the tools for printing where a failure happened. For a real debugger there is `bashdb`, but in practice `set -x` with a good `PS4` is what people use.
- **`strace -f -e trace=execve,openat`** shows what a script really runs and opens, including which `PATH` entry won (section 1.4) and which start-up files were read (section 13).

---

## 16. Security hazards

Shell is the language of installers, deploy scripts and cron jobs, and these run with privileges. The previous sections each contain a hazard; this one collects them with the measured evidence.

```bash
#!/usr/bin/env bash
# e16_hazards.sh: the shell hazards that become security problems; everything runs in a scratch directory
. "$(dirname "$0")/h.sh"
d=$(mktemp -d) && cd "$d" || exit 1
show 'dir=; set -- $dir/*; echo "unquoted empty \$dir/* became $# words; first: $1"'
show 'dir=; bash -c '"'"': "${dir:?dir is empty}"; echo would-delete'"'"
show 'x=; [ $x = y ] && echo yes; echo "status $?"'
show 'x="a b"; [ $x = y ] && echo yes; echo "status $?"'
show 'x="a b"; [[ $x = y ]] && echo yes; echo "status $?"'
show 'cd /nonexistent/dir; echo "still in: $(basename "$PWD" | sed "s/tmp\..*/SCRATCH/")"'
show 'echo "  lead\\ttrail  " | { read x; echo "[$x]"; }; echo "  lead\\ttrail  " | { IFS= read -r x; echo "[$x]"; }'
echo
echo '--- eval on a file name'
touch '$(touch INJECTED)'
show 'ls'
show 'for f in *; do eval "echo checking $f" >/dev/null; done; ls'
rm -f INJECTED
echo
echo '--- an empty PATH entry means the current directory'
printf '#!/bin/sh\necho "planted ls ran instead of /usr/bin/ls"\n' > ls
chmod +x ls
show 'PATH=/usr/bin:/bin ls | head -1'
show 'PATH=":/usr/bin:/bin"; ls'
show 'PATH="/usr/bin:/bin:"; ls'
cd / && rm -rf "$d"
```

```text
$ dir=; set -- $dir/*; echo "unquoted empty \$dir/* became $# words; first: $1"
unquoted empty $dir/* became 27 words; first: /bin
$ dir=; bash -c ': "${dir:?dir is empty}"; echo would-delete'
bash: line 1: dir: dir is empty
$ x=; [ $x = y ] && echo yes; echo "status $?"
bash: [: =: unary operator expected
status 2
$ x="a b"; [ $x = y ] && echo yes; echo "status $?"
bash: [: too many arguments
status 2
$ x="a b"; [[ $x = y ]] && echo yes; echo "status $?"
status 1
$ cd /nonexistent/dir; echo "still in: $(basename "$PWD" | sed "s/tmp\..*/SCRATCH/")"
bash: cd: /nonexistent/dir: No such file or directory
still in: SCRATCH
$ echo "  lead\\ttrail  " | { read x; echo "[$x]"; }; echo "  lead\\ttrail  " | { IFS= read -r x; echo "[$x]"; }
[leadttrail]
[  lead\ttrail  ]

--- eval on a file name
$ ls
$(touch INJECTED)
$ for f in *; do eval "echo checking $f" >/dev/null; done; ls
$(touch INJECTED)
INJECTED

--- an empty PATH entry means the current directory
$ PATH=/usr/bin:/bin ls | head -1
$(touch INJECTED)
$ PATH=":/usr/bin:/bin"; ls
planted ls ran instead of /usr/bin/ls
$ PATH="/usr/bin:/bin:"; ls
$(touch INJECTED)
ls
```

- **An unquoted empty variable in a glob.** `dir=; set -- $dir/*` expanded to **27 words, the first being `/bin`** (the pattern `/*` matched the root directory; the count is specific to this machine). With `rm -rf $dir/*` that is the classic "deleted the system" bug. **`${dir:?}` aborts instead** (`dir is empty`).
- **`[ $x = y ]`** fails with `unary operator expected` when `x` is empty and with `too many arguments` when `x` holds two words (both status 2, not "false"), because the expansion changed the number of arguments to `[`. **`[[ $x = y ]]` is safe** (status 1 for "false").
- **`cd` that fails does not stop the script.** After `cd /nonexistent/dir` the shell was still in the old directory (the transcript prints the masked name), so the commands that follow run **in the wrong place**; use `cd -- "$dir" || exit` or `set -e`.
- **`read` without `-r` and `IFS=`** mangles lines: `read x` turned `  lead\ttrail  ` into `leadttrail` (the backslash was consumed and the spaces trimmed); `IFS= read -r x` kept the line exactly.
- **`eval` with data is code execution.** A file named `$(touch INJECTED)` was in the directory; `eval "echo checking $f"` for each file **ran the command in the name and created `INJECTED`**. Never `eval` a string with untrusted parts; use arrays (`cmd=(prog "$arg"); "${cmd[@]}"`), `printf %q`, or `declare -n` name references instead.
- **An empty `PATH` entry means the current directory.** With `PATH=":/usr/bin:/bin"` a planted `./ls` ran instead of `/usr/bin/ls`; a **trailing** empty element (`/usr/bin:/bin:`) put the current directory last, so the real `ls` won in that case, but the rule is the same: a `PATH` with an empty element, `.`, or a writable directory in front is an invitation. Set `PATH` explicitly at the top of a privileged script.
- **Predictable temporary files and symlinks**: section 11 (use `mktemp`, `set -C`).
- **Options from file names**: section 3 (`--`, `./`).
- **Setuid shell scripts are not honoured** by the Linux kernel (from memory, not tested here): the race between reading the `#!` line and opening the script makes them unsafe, so privileged behaviour belongs in a compiled helper, `sudo` rules or a service manager.
- **`sudo` and the environment.** A script run under `sudo` should not trust `PATH`, `IFS` or `BASH_ENV` from the caller (bash ignores `BASH_ENV` when it runs with a different effective user, from memory).

---

## 17. What it costs: the numbers worth remembering

All values are from the experiments above on bash 5.2.21 on one VM; quote them as ranges and say they were measured on one machine.

| Quantity | Value |
| --- | --- |
| Shell arithmetic `x=$((x+i))` | 2.65 microseconds |
| `printf -v` (builtin, no fork) | 3.19 microseconds |
| `$(builtin)` (fork only) | 370 microseconds |
| `$(external)` (fork and exec) | 1,590 to 1,700 microseconds |
| `$(<bigfile)` against `$(cat bigfile)`, 1.3 MB | 5,054 against 11,679 microseconds |
| Summing 200,000 numbers: shell loop, `mapfile`, `bc`, awk | 718, 473, 51, 17 ms |
| `xargs -n1` against batched, 2,000 args | 2,276 against 5 ms |
| `bash -c :` against `dash -c :` start-up | 1.8 against 1.5 ms (500 runs) |
| Unlocked 8-by-250 counter: final value | 3 to 16 of 2,000 |
| The same with `flock` | 2,000 of 2,000, about 1.8 s |
| `SIGTERM` to a script waiting on a child | handled after 3,518 ms (the child's remaining time) |
| `SIGTERM` to the whole process group | handled after 3 ms |

---

## 18. Common mistakes

1. **Unquoted expansions.** `$x` should almost always be `"$x"` (sections 1 to 3). The exceptions are deliberate splitting and assignment on the right of `=`.
2. **`for f in $(ls)` or `ls | while read`.** Use a glob, or `find -print0` with `read -d ''` (section 3).
3. **A name that starts with `-`.** Use `--` or `./*` (section 3).
4. **`echo` for data.** Use `printf '%s\n' "$x"` (section 2).
5. **`cmd | while read x; do v=...; done` and then using `v`.** The loop ran in a subshell (section 5).
6. **Trusting `set -e`.** It stops for fewer cases than people think, and **not** inside a function called from `if`, `&&` or `||` (section 6).
7. **`local v=$(cmd)`**, which hides the status of `cmd` (section 6); declare, then assign.
8. **Comparing numbers with `<` in `[[ ]]`**, which compares strings; use `-lt` or `(( ))` (section 7).
9. **A leading zero in arithmetic.** `$((08))` is an error and `$((010))` is 8 (section 7).
10. **Unquoted `$@` or `$*`.** Use `"$@"` to forward arguments (section 2).
11. **Missing `-r` on `read`, and missing `IFS=`** (sections 7 and 16).
12. **A double-quoted `trap` string** that freezes the variables (section 10).
13. **Assuming a trap fires on `SIGKILL`, or on a signal while the shell waits for a child** (section 9).
14. **A guessable temporary file name.** Use `mktemp` (section 11).
15. **A script that works under bash but runs under `sh`** (section 12).
16. **`cd` without checking, and `rm -rf $var/*` without `${var:?}`** (section 16).
17. **Parsing `$(date)` or `$(basename)` in a hot loop.** Use `printf '%(...)T'` and parameter expansion (section 14).
18. **`kill $pid` for a script and expecting its children to die.** Signal the process group (section 9).

---

## 19. Questions out loud

Say each answer in about 30 to 60 seconds, with one number or one example from the transcripts.

1. **What happens, in order, when the shell runs a command line?** Tokenise and parse; brace expansion; tilde, parameter, arithmetic and command substitution left to right; word splitting; globbing; quote removal; redirections; then find the command (function, builtin or `PATH`) and fork and exec if it is external.
2. **Why quote `"$var"`?** Unquoted results are word-split and globbed: an empty variable vanishes and `*` expands. Example: `for f in $(ls)` ran 7 times over 5 files.
3. **`$*` against `"$*"` against `$@` against `"$@"`?** Three words, one word, three words, three words with empty and spaced words preserved. `"$@"` forwards arguments faithfully.
4. **What does `2>&1 >o` do against `>o 2>&1`?** Redirections are applied left to right and `2>&1` copies where descriptor 1 points at that moment. The first sends stderr to the old stdout and stdout to the file; the second sends both to the file.
5. **Why does `echo x | read v` leave `v` empty?** Every pipeline stage is a forked process, `read` runs in one, and the assignment dies with it. Fix: `lastpipe`, process substitution, here-string, or a file.
6. **What is `$?` for a pipeline, and what does `pipefail` change?** The last command's status; with `pipefail` the rightmost non-zero status. `yes | head -1` shows `141 0`.
7. **Where does `set -e` not apply?** `if` and loop conditions, non-last parts of `&&` and `||`, `!`, non-last pipeline stages without `pipefail`, `local x=$(cmd)`, and inside a function that was itself called from one of those contexts.
8. **What is a subshell and when do you get one?** A forked copy of the shell: `( )`, `$( )`, pipeline stages, background jobs. Its variable changes and `cd` disappear.
9. **What do `trap EXIT`, `trap ERR` and `trap INT` do, and what do they miss?** Cleanup at exit, a hook on failures (needs `-E` in functions), a signal handler. They miss `SIGKILL` and `exec`, and signal traps wait for a foreground child to finish.
10. **How do you make a script safe to run twice at once?** `flock -n` on a descriptor held for the script's life, exit 75 if busy; the kernel releases it on death. Measured: 8 workers lost 1,984 to 1,997 of 2,000 updates without it.
11. **How do you write a file so readers never see a partial one?** Write to `mktemp` in the same directory, then `mv` (a rename is atomic).
12. **What is the difference between `nohup`, `setsid` and `disown`?** `nohup` makes the command ignore `SIGHUP`; `setsid` puts it in a new session, detached from the terminal; `disown` stops the shell sending it `SIGHUP`. A service manager is better than any of them.
13. **How do you pass a list of files with any name through a pipe?** NUL-separated: `find -print0 | xargs -0` or `read -d ''`.
14. **When would you stop writing shell?** When you need data structures beyond arrays, real error handling, floating point, or more than about 100 lines of logic; switch to Python and keep shell for glue.

---

## Problems

**Where to solve them.** In a scratch directory on any Linux machine with bash 4 or later and `shellcheck` if you have it (`pip install shellcheck-py` works). For each answer, **run the broken version first and see it fail**, then run your fix; the shell is the one language where reading the answer is much weaker than watching the failure. For more practice, the ShellCheck wiki lists each warning with a bad and a good example, and the GNU Bash manual's "Shell Expansions" and "The Set Builtin" pages are short.

**How to approach them.** Work out the expected output on paper, run it, and if the two differ find out which word or process boundary you got wrong. For Problem E, write the usage and the failure paths before the happy path.

### Problem A (E, entry level): counting words

A function is called as `f 'a b' '' c`. How many arguments does each of `$*`, `"$*"`, `$@` and `"$@"` produce in `for x in ...`? Which one forwards the arguments unchanged to another command?

### Problem B (E, entry level): a loop that survives

`for f in $(ls *.log); do gzip $f; done` must compress every `.log` file in a directory that holds `a.log`, `two words.log`, `-n.log` and a file whose name contains a newline. Say what goes wrong and write the safe loop. Run both.

### Problem C (M, medium): the loop that counts zero

`n=0; cat in.txt | while read -r l; do n=$((n+1)); done; echo $n` prints 0 for a three-line file. Explain, then give three different fixes.

### Problem D (M, medium): `set -e` and a function

With `set -e`, a function `check() { false; echo "check: continued"; }` is called as `if check; then ...`. What prints? Write a version of `check` that stops at the first failure whether or not it is used as a condition, and say what `check` as a plain statement does.

### Problem E (H, hard): a lock wrapper

Write `runlocked NAME COMMAND [ARG...]` that runs the command while holding a lock named `NAME`. Requirements: exit 75 if another copy holds the lock; exit 64 on bad usage; otherwise exit with the command's status; on `SIGTERM` or `SIGINT` kill the child, release the lock and delete the working directory; no stale lock after `SIGKILL`.

---

## Answers

### Problem A

For the parameters `'a b'`, an empty string and `c`: `$*` and `$@` each give **3** words (`a`, `b`, `c`; the empty one vanishes), `"$*"` gives **1** word (`a b c ` joined with the first character of `IFS`), and `"$@"` gives **3** words (`a b`, empty, `c`), which is the measured result in section 2. **`"$@"` forwards unchanged.**

### Problem B

```text
=== Problem B: compress every .log file, whatever its name
$ broken 2>&1 | head -3; ls | grep -c "\.gz$"
ls: invalid option -- '.'
Try 'ls --help' for more information.
0
$ fixed; ls | grep -c "\.gz$"
4

=== Problem C: count the lines of a file inside a loop
$ n=0; cat in.txt | while read -r l; do n=$((n+1)); done; echo "n=$n"
n=0
$ n=0; while read -r l; do n=$((n+1)); done < in.txt; echo "n=$n"
n=3
$ n=0; while read -r l; do n=$((n+1)); done < <(cat in.txt); echo "n=$n"
n=3
$ shopt -s lastpipe; set +m; n=0; cat in.txt | while read -r l; do n=$((n+1)); done; echo "n=$n"
n=3

=== Problem D: set -e and a function used as a condition
$ bash d1.sh; echo "status $?"
check: continued after false
check passed
status 0
$ bash d2.sh; echo "status $?"
check failed
status 0
$ bash d3.sh; echo "status $?"
status 1
```

The first block of the transcript shows the failure: `ls *.log` treats the file `-n.log` as an option (`invalid option -- '.'`), so the command **fails before the loop starts** and **0 files were compressed**; and even if it had listed the names, the `$(...)` would be word-split into `two` and `words.log` and the newline name into two items, and the unquoted `$f` in `gzip $f` is split again. The safe loop is:

```text
for f in ./*.log; do [[ -e $f ]] || continue; gzip -- "$f"; done
```

It produced **4** compressed files. The `./` prefix means no name can look like an option, the quotes stop splitting, `[[ -e $f ]]` skips the literal pattern when nothing matches (without `nullglob`, an unmatched glob stays as the pattern), and `--` is a second guard.

### Problem C

The second block of the same transcript: with the pipe the result is `n=0`. The `while` loop is the **last stage of a pipeline**, so bash runs it in a **subshell**, the increments happen there, and the parent's `n` is never changed. Three fixes, all printed `n=3`: **redirect the file into the loop** (`done < in.txt`, no pipe, no `cat`), **process substitution** (`done < <(cat in.txt)`), and **`shopt -s lastpipe; set +m`**, which runs the last stage in the current shell (in a script, with job control off). The first is best because it also avoids the extra process.

### Problem D

The third block of the transcript. With `if check`, **`check` kept running after `false` and printed `check: continued after false`**, because the call is in an `if` condition, where `-e` is ignored, **and the exemption reaches inside the function** (section 6). The version that stops itself is `check() { false || return 1; echo "check: reached the end"; }`: used as a condition it printed `check failed` and **never reached the end**, because the failure is handled explicitly and not left to `-e`. Called as a plain statement (`check` on its own line), the original function **stopped the script with status 1** (the third script, which printed nothing and exited 1). Lesson: **inside a function that may be used as a condition, test each step with `|| return`**.

### Problem E

```bash
#!/usr/bin/env bash
# runlocked: run a command while holding a lock; at most one copy per lock name.
# Exit 75 if busy, 64 on bad usage, otherwise the command's own status (128+N if it was killed by signal N).
set -uo pipefail
if (($# < 2)); then echo "usage: ${0##*/} NAME COMMAND [ARG...]" >&2; exit 64; fi
name=$1; shift
work=$(mktemp -d) || exit 1
child=
cleanup() {
    trap - EXIT
    if [[ -n $child ]]; then kill "$child" 2>/dev/null; wait "$child" 2>/dev/null; fi
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 143' TERM
trap 'exit 130' INT
exec 9>"${TMPDIR:-/tmp}/runlocked.$name.lock"
flock -n 9 || { echo "${0##*/}: $name is busy" >&2; exit 75; }
"$@" &
child=$!
wait "$child"
status=$?
child=
exit "$status"
```

```text
$ runlocked
usage: runlocked.sh NAME COMMAND [ARG...]
[status 64]
$ runlocked job sh -c 'exit 7'
[status 7]
$ runlocked job true
[status 0]
--- two at once
$ runlocked job true
runlocked.sh: job is busy
[status 75]
--- TERM to the wrapper: the child is killed, the lock is free, nothing is left
[wrapper status 143]
child gone
$ runlocked job true
[status 0]
leftover work directories: 0
```

How it works and what the tests show: `exec 9>"...lock"` plus `flock -n 9` takes the lock without waiting (a second copy printed `job is busy` and exited **75**; no argument gave usage and **64**). The command runs in the **background** and the script `wait`s for it, which matters: **a trap runs as soon as `wait` is interrupted by a signal**, whereas a trap would wait for a foreground child to finish (section 9). The `TERM` and `INT` traps just `exit 143` / `exit 130`, so the `EXIT` trap does the work: `cleanup` removes itself (`trap - EXIT`), **kills and reaps the child**, and removes the work directory. Tests: the child's status passed through (`sh -c 'exit 7'` gave 7), a second copy while one ran got 75, `SIGTERM` to the wrapper ended it with **143**, **the child was gone** (`sleep 30` not running), the next run succeeded (the child had been holding the lock through its inherited descriptor 9, so killing it was what freed the lock), and **no work directory was left**. After `SIGKILL` the wrapper cannot clean up, but the child, if still running, would keep the lock until it exits; a fully robust version runs the child in its own process group and kills the group, or uses a service manager.

---

## Cheat sheet

```text
Expansion order   braces > tilde/param/arith/command (left to right) > word split > glob > quote removal
Quote             "$x"  "$@"  "${arr[@]}"  --  ./*   printf '%s\n'   [[ ]]   read -r   IFS=
Redirect          >f 2>&1 (both to f)   2>&1 >f (stderr to old stdout)   exec 3>f   <<'EOF'   <<<   <( )
Pipes             each stage is a process; PIPESTATUS; pipefail; lastpipe; read v < <(cmd)
Exit codes        0 ok  1 error  2 usage  64 usage  66 no input  75 try later  126 not exec  127 not found  128+N signal N
set -e does not   if/while conditions, non-last of && ||, !, non-last pipe stage, local x=$(), inside function called from those
Params            ${v:-d} ${v:=d} ${v:?m} ${v#p} ${v##p} ${v%p} ${v%%p} ${v/p/r} ${v^^} ${#v} ${v:o:l}
Arrays            a=(x "y z"); "${a[@]}" "${!a[@]}" "${#a[@]}"; declare -A m; mapfile -t a < <(cmd); declare -p a
Arithmetic        $((..)) 64-bit wraps; 010 is octal (10#010); [[ 9 < 10 ]] is false (string); no floats
Robust script     set -Eeuo pipefail; trap cleanup EXIT; trap 'exit 130' INT; trap 'exit 143' TERM; mktemp; mv; flock -n 9
Signals           trap waits for a foreground child; ignored on entry stays ignored; SIGKILL: no trap; signal the group
Jobs              & : same group (script); set -m : own group; setsid : new session; nohup : ignore SIGHUP; wait $! : status
Portability       /bin/sh = dash here: no [[ ]], arrays, <<<, pipefail, ${v^^}, brace expansion; printf not echo -e
Start-up          bash -c reads nothing; -i reads ~/.bashrc; -l reads /etc/profile + first of ~/.bash_profile ~/.bash_login ~/.profile
Speed             $((..)) 2.6 us; $(builtin) 370 us; $(external) 1.6 ms; use awk for bulk; xargs batches
Debug             bash -x with PS4='+ ${BASH_SOURCE##*/}:${LINENO}:${FUNCNAME[0]:-main}: '; bash -n; shellcheck
Hazards           empty var in rm -rf $d/*; eval of data; empty PATH entry; predictable /tmp names; cd without ||
```

---

## Sources

Fetched in this session:

- [Shell Expansions (GNU Bash manual)](https://www.gnu.org/software/bash/manual/html_node/Shell-Expansions.html): the order of expansions (brace; tilde, parameter and variable, arithmetic and command substitution, process substitution, left to right; word splitting; filename expansion; quote removal), and the exceptions `"$@"` and `"${name[@]}"` to the word-count rule. It does not say what happens to expansion results inside double quotes.
- [The Set Builtin (GNU Bash manual)](https://www.gnu.org/software/bash/manual/html_node/The-Set-Builtin.html): the cases in which `-e` is ignored, the effect inside functions and compound commands, `-u` with `@` and `*`, `pipefail` giving the rightmost failing status, `-E` and the `ERR` trap, `-x` with `PS4`.

Run in this session: bash 5.2.21 and dash (experiments above), ShellCheck 0.11.0 (`pip install shellcheck-py`), `strace`, `flock`, `xargs`, GNU coreutils, on Linux 6.18.44.

Cross-references: [OS 02](../05_operating_systems/02_processes.md) (processes, `fork`, `exec`, inherited descriptors), [OS 21](../05_operating_systems/21_os_interview_question_bank.md) (the Ctrl-C walkthrough, section 8.3).

From memory, not fetched or not tested in this session: the command-lookup order and the meaning of statuses 126 and 127; the `sysexits.h` names behind 64, 66 and 75; the atomicity of `O_APPEND` writes; that setuid scripts are not honoured by Linux; that bash ignores `BASH_ENV` when the real and effective users differ; what the pipefail option is in the POSIX standard (I did not fetch it, so I do not claim it is standardised); and the behaviour of other bash versions, BusyBox and zsh. The unexplained result is that an exported `PS4` was not honoured on this machine.
