# LX 02: Text processing and one-liners

*Part of the [Linux plan](00_plan.md) · Time: about 4 hours · You need: [LX 01](01_shell_and_scripting.md) (quoting, pipes, `pipefail`, process substitution; every command below is a shell command line, so the expansion rules of LX 01 apply to it).*

On a production Linux box the log is a text file, the configuration is a text file, `/proc` is text, and the first answer to "what is going on" is a pipeline of small programs. Interviewers know this, and they ask it two ways: "what does this one-liner do?" (read a pipeline) and "write a command that prints the ten slowest endpoints in this log" (compose one). Both are tests of whether you know **what each tool does to a stream of lines**, in which order, and where the traps are.

This tutorial is built around **experiments**. Every transcript below was produced by a script on the machine described under "Where the numbers come from". The sequence is: how to choose a tool (section 1), regular expressions (2), `grep` (3), `sed` (4), `awk` (5), the small tools `cut`, `tr`, `sort`, `uniq`, `join`, `comm`, `paste`, `column` (6), `find` and `xargs` (7), `jq` (8), locale and speed (9), the mistakes that cost an afternoon (10), and then a **bank of 30 one-liners** over a web-server log, an authentication log, a syslog and JSON lines (11), each checked **byte for byte against a Python computation of the same question**.

**Four honest limits, stated first.** (1) Everything is measured with **GNU** tools on one Linux 6.18 VM: GNU `grep` 3.11, `sed` 4.9, `gawk` 5.2.1 (with `mawk` for comparison), coreutils 9.4, `jq` 1.7, ripgrep 14.1. BSD and macOS versions of `sed`, `grep`, `sort` and `awk` differ in places (the text says where I know, from memory). (2) The data is **synthetic**: a seeded Python script generated the four logs, so the counts below are specific to it; the *commands* and the *relations between numbers* are the lesson. (3) Timings are the **fastest of three runs** with a warm page cache on a VM, and vary by 10 to 30 percent between runs. (4) I could not test a terminal or a non-English locale beyond the two I built (`en_US.UTF-8`, compiled locally with `localedef`, and `C.UTF-8`); collation in other locales will differ.

By the end you can:

- say which tool fits a task (measured: for a count of 500 responses in a 600,000-line log, `grep -c` took about 80 ms, `rg -c` 14 ms, `gawk` 200 ms and `python3` 112 ms, and a test with stdout on `/dev/null` reported **2 ms** because GNU `grep` stops at the first match there);
- write BRE, ERE and PCRE patterns and know what each lacks (measured: `grep -E '\d'` matched the letter `d`, not a digit; `grep -P '^(a+)+$'` on 28 `a` plus `b` stopped with "exceeded PCRE's backtracking limit" and status 2, while `-E` answered in 2 ms);
- use `sed` addresses, `s///` flags, the hold space and `-i` safely (measured: `sed -i` on a symlink **replaced the link with a regular file**; `--follow-symlinks` kept it);
- write `awk` programs with patterns, arrays and `printf`, and avoid its traps (measured: `echo '10 abc' | awk '{print ($1 > $2)}'` printed 0 because a non-number forces a string comparison; shell text spliced into an awk program ran a `system()` call, `-v` did not);
- use `sort`, `uniq`, `join` and `comm` correctly (measured: `uniq -c` without `sort` gave four counts of 1 for `b a b a`; an unsorted `join` stopped with "input is not in sorted order");
- traverse files safely with `find -print0 | xargs -0` (measured: one file name with a newline made a plain `find | xargs` fail on 4 error lines, and the NUL version had none);
- query JSON with `jq` (measured: `jq` kept a 20-digit integer intact when unchanged and printed `12345678901234567000` after `+ 1`);
- explain why `LC_ALL=C` matters (measured: `sort` of 600,000 lines took 266 ms in the C locale and **1,300 ms in en_US.UTF-8**, about 5 times longer, and the two orders differ: `A B a b` against `a A b B`);
- answer a question about a log with a short pipeline and check it (measured: 30 one-liners over 200,000 lines, **30 of 30 matched an independent Python computation**);
- predict how `set -e` and `pipefail` treat `grep` (measured: `grep` returns **1 when nothing matches**, which stops a `set -e` script).

**Where the numbers come from.** Eight scripts plus the bank were run with bash 5.2.21 on Linux 6.18.44 in a Firecracker micro-VM (2 vCPUs, about 7 GiB of memory), running as root, with GNU grep 3.11, GNU sed 4.9, GNU Awk 5.2.1 (also `mawk`), GNU coreutils 9.4 (`sort`, `uniq`, `join`, `comm`, `paste`, `column`), findutils, `jq` 1.7, ripgrep 14.1 and Python 3 for the checks. The `en_US.UTF-8` locale was compiled into a scratch directory for this session (`localedef -i en_US -f UTF-8`) and selected with `LOCPATH`, because the VM ships only `C`, `POSIX` and `C.utf8`. Transcripts print `$ command` before each command's output; scripts that create temporary names print them masked or not at all.

**Data and timing.** The four logs are **synthetic**, generated deterministically (a seeded Python script, the same output on every run): `access.log` (200,000 lines, 23 MiB, a web server's combined format with the request time added as the last field, covering 18:00 to 23:59 on 14 November 2026 from 3,000 client IPs with a skewed popularity), `access_big.log` (600,000 lines, 69 MiB, same format, used for the speed tests), `auth.log` (9,900 lines of `sshd` messages: failed passwords, invalid users, accepted logins, session open and close), `syslog` (50,000 lines from six programs) and `app.jsonl` (5,000 JSON records). A sample line of each is printed where it is first used. Timings are the fastest of three runs; quote them as ranges.

---

## 1. The model: a stream of lines, and which tool to pick

Nearly every tool in this tutorial reads **lines** from files or standard input and writes lines to standard output, so they compose with `|` (see [LX 01](01_shell_and_scripting.md), section 5: each stage is a separate process connected by a pipe). A one-liner is a chain of **filters** (keep or drop lines), **transformers** (change each line), **extractors** (keep some columns) and **aggregators** (count, sum, sort, group).

| Job | First choice | Why |
| --- | --- | --- |
| keep lines that match | `grep` | fastest; `-v` inverts, `-c` counts, `-o` prints only the match |
| change text inside lines | `sed 's/old/new/'` | one substitution per line, with a regex |
| pick columns (fixed delimiter) | `cut -d, -f1,3` | fast and simple; no repeated delimiters |
| pick columns (blanks, any delimiter regex), arithmetic, grouping | `awk` | a real language: fields, variables, arrays, `printf` |
| count equal lines | `sort \| uniq -c` | the classic frequency table; `uniq` only sees **adjacent** equal lines |
| order lines | `sort` | `-k` keys, `-n` numeric, `-r`, `-u`, `-s` stable |
| combine two files by key | `join`, `comm` | both need **sorted** input |
| JSON | `jq` | `grep` and `sed` on JSON break on nesting and escaping |
| find files | `find` | tests on name, type, size, time; `-print0` for safe names |
| run a command over many items | `xargs` | batches arguments, runs in parallel with `-P` |

Three rules summarise the model. **One**: choose the narrowest tool (`grep` before `awk` before Python), but stop at the first that fits without contortions. **Two**: **aggregation needs a hash table or a sort**: `awk '{c[$1]++}'` keeps a table in memory (cost proportional to the number of distinct keys), `sort | uniq -c` sorts on disk if needed (cost proportional to the number of lines times log n), and section 9 measures the difference. **Three**: **a line has structure only by convention**. Logs separate fields with spaces, but a user agent or a path with a space breaks `awk '{print $9}'`; check the format before trusting a column number (in the access log the quoted fields are why `awk -F'"'` is used to reach the user agent in bank item 11).

A line of each log, so the column numbers below make sense:

```text
192.243.4.30 - - [14/Nov/2026:18:00:00 +0000] "GET /api/search?q=router&page=4 HTTP/1.1" 200 1156 "-" "Mozilla/5.0 (X11; Linux x86_64) Firefox/120.0" 0.791
Nov 15 00:00:24 web01 sshd[29973]: Failed password for invalid user git from 10.101.224.64 port 9731 ssh2
Nov 15 00:00:00 web01 app-worker[29117]: ERROR job 5870 failed: timeout
{"ts":1763143200,"level":"info","svc":"search","ms":878,"user":"u30","tags":["b","a","d"]}
```

In the access line, `awk` fields are `$1` the IP, `$4` the timestamp with a leading `[`, `$6` the method with a leading quote, `$7` the path, `$9` the status, `$10` the bytes, and `$NF` (the last) the request time in seconds.

---

## 2. Regular expressions: BRE, ERE and PCRE

A **regular expression** (regex) describes a set of strings. Unix has three dialects in common use, and most "regex does not work" bugs are a dialect mismatch.

| Dialect | Tools | `+` `?` `\|` `()` `{}` | Notes |
| --- | --- | --- | --- |
| **BRE** (basic) | `grep`, `sed` by default | literal; `\+ \? \| \( \) \{ \}` are operators (a GNU extension for `\+ \? \|`) | the default for `grep` and `sed` |
| **ERE** (extended) | `grep -E`, `sed -E`, `awk` | operators without backslashes | what most people expect |
| **PCRE** (Perl) | `grep -P`, `rg`, Perl, Python's `re` (similar) | all of ERE plus `\d \w \s`, lazy `*?`, lookaround, `\K`, named groups | not in POSIX; **backtracking** engine |

The transcript runs the same questions through each dialect.

```bash
#!/usr/bin/env bash
# e1_regex.sh: BRE, ERE and PCRE with grep, and what goes wrong
. "$(dirname "$0")/h.sh"
cd "$(mktemp -d)" || exit 1
printf 'ab\naab\nb\ncat\ndog\nbird\n' > t.txt
show "grep 'a+b' t.txt; echo \"status \$?\""
show "grep 'a\\+b' t.txt"
show "grep -E 'a+b' t.txt"
show "grep 'cat\\|dog' t.txt"
show "grep -E 'cat|dog' t.txt"
show "grep -E '^(ab|b)$' t.txt"
show "printf 'x1\nxd\n' | grep -E '\\d'"
show "printf 'x1\nxd\n' | grep -P '\\d'"
show "printf 'x1\nxd\n' | grep '[[:digit:]]'"
echo
echo '--- greedy against lazy, lookbehind, \K'
show "echo '<b>x</b> and <b>y</b>' | grep -o '<b>.*</b>'"
show "echo '<b>x</b> and <b>y</b>' | grep -oP '<b>.*?</b>'"
show "echo '<b>x</b> and <b>y</b>' | grep -o '<b>[^<]*</b>'"
show "echo 'user=alice id=42' | grep -oP '(?<=user=)\\w+'"
show "echo 'user=alice id=42' | grep -oP 'id=\\K\\d+'"
show "printf 'aab\nabc\nxyzzy\n' | grep -E '(.)\\1'"
echo
echo '--- word, line, count, invert, exit status'
printf 'the cat sat\nconcatenate\ncat\nCat\n' > w.txt
show "grep -c cat w.txt; grep -w cat w.txt; grep -x cat w.txt; grep -i -c cat w.txt; grep -v cat w.txt"
show "grep -q cat w.txt; echo \"found: \$?\"; grep -q nosuch w.txt; echo \"not found: \$?\"; grep cat nosuchfile; echo \"error: \$?\""
show "grep -c nosuch w.txt; echo \"status \$?\""
echo
echo '--- context, -m, several files, binary files'
seq 1 9 > n.txt
show "grep -n -B1 -A1 5 n.txt"
show "grep -m2 '[0-9]' n.txt"
printf 'one\ntwo\n' > a.txt; printf 'two\nthree\n' > b.txt; printf 'x\0two\n' > bin.dat
show "grep -c two a.txt b.txt; grep -l two a.txt b.txt bin.dat; grep -L three a.txt b.txt; grep -H two a.txt"
show "grep two bin.dat; grep -a two bin.dat | od -c | head -2"
echo
echo '--- backtracking: the same pattern in ERE (finite automaton) and PCRE (backtracking)'
s=$(printf 'a%.0s' $(seq 1 28))b
for engine in -E -P; do
  start=$EPOCHREALTIME
  out=$(printf '%s\n' "$s" | grep $engine '^(a+)+$' 2>&1; echo "status $?")
  end=$EPOCHREALTIME
  printf 'grep %s on 28 a plus b: %s   %d ms\n' "$engine" "$(echo "$out" | tr '\n' ' ')" $(( (${end/./} - ${start/./}) / 1000 ))
done
s=$(printf 'a%.0s' $(seq 1 40))b
for engine in -E -P; do
  start=$EPOCHREALTIME
  out=$(printf '%s\n' "$s" | timeout 20 grep $engine '^(a+)+$' 2>&1; echo "status $?")
  end=$EPOCHREALTIME
  printf 'grep %s on 40 a plus b: %s   %d ms\n' "$engine" "$(echo "$out" | tr '\n' ' ' | cut -c1-80)" $(( (${end/./} - ${start/./}) / 1000 ))
done
echo "Python's re module, the same pattern, no limit:"
python3 - <<'P'
import re, time
for n in (20, 22, 24, 26):
    s = 'a' * n + 'b'
    t = time.time(); re.match(r'^(a+)+$', s)
    print('  %d a plus b: %5d ms' % (n, round((time.time() - t) * 1000)))
P
```

```text
$ grep 'a+b' t.txt; echo "status $?"
status 1
$ grep 'a\+b' t.txt
ab
aab
$ grep -E 'a+b' t.txt
ab
aab
$ grep 'cat\|dog' t.txt
cat
dog
$ grep -E 'cat|dog' t.txt
cat
dog
$ grep -E '^(ab|b)$' t.txt
ab
b
$ printf 'x1\nxd\n' | grep -E '\d'
xd
$ printf 'x1\nxd\n' | grep -P '\d'
x1
$ printf 'x1\nxd\n' | grep '[[:digit:]]'
x1

--- greedy against lazy, lookbehind, \K
$ echo '<b>x</b> and <b>y</b>' | grep -o '<b>.*</b>'
<b>x</b> and <b>y</b>
$ echo '<b>x</b> and <b>y</b>' | grep -oP '<b>.*?</b>'
<b>x</b>
<b>y</b>
$ echo '<b>x</b> and <b>y</b>' | grep -o '<b>[^<]*</b>'
<b>x</b>
<b>y</b>
$ echo 'user=alice id=42' | grep -oP '(?<=user=)\w+'
alice
$ echo 'user=alice id=42' | grep -oP 'id=\K\d+'
42
$ printf 'aab\nabc\nxyzzy\n' | grep -E '(.)\1'
aab
xyzzy

--- word, line, count, invert, exit status
$ grep -c cat w.txt; grep -w cat w.txt; grep -x cat w.txt; grep -i -c cat w.txt; grep -v cat w.txt
3
the cat sat
cat
cat
4
Cat
$ grep -q cat w.txt; echo "found: $?"; grep -q nosuch w.txt; echo "not found: $?"; grep cat nosuchfile; echo "error: $?"
found: 0
not found: 1
grep: nosuchfile: No such file or directory
error: 2
$ grep -c nosuch w.txt; echo "status $?"
0
status 1

--- context, -m, several files, binary files
$ grep -n -B1 -A1 5 n.txt
4-4
5:5
6-6
$ grep -m2 '[0-9]' n.txt
1
2
$ grep -c two a.txt b.txt; grep -l two a.txt b.txt bin.dat; grep -L three a.txt b.txt; grep -H two a.txt
a.txt:1
b.txt:1
a.txt
b.txt
bin.dat
a.txt
a.txt:two
$ grep two bin.dat; grep -a two bin.dat | od -c | head -2
grep: bin.dat: binary file matches
0000000   x  \0   t   w   o  \n
0000006

--- backtracking: the same pattern in ERE (finite automaton) and PCRE (backtracking)
grep -E on 28 a plus b: status 1    2 ms
grep -P on 28 a plus b: grep: (standard input): exceeded PCRE's backtracking limit status 2    30 ms
grep -E on 40 a plus b: status 1    5 ms
grep -P on 40 a plus b: grep: (standard input): exceeded PCRE's backtracking limit status 2    20 ms
Python's re module, the same pattern, no limit:
  20 a plus b:    36 ms
  22 a plus b:   155 ms
  24 a plus b:   613 ms
  26 a plus b:  2660 ms
```

Reading it:

- **BRE needs backslashes for the operators.** `grep 'a+b'` found nothing and exited with status 1 (it looked for the literal text `a+b`); `grep 'a\+b'` and `grep -E 'a+b'` matched `ab` and `aab`. In the same way `\|` is alternation in BRE and `|` in ERE. **The interview version**: "what is the difference between `grep` and `egrep`?" `egrep` is `grep -E`.
- **`\d` is PCRE only.** `grep -E '\d'` matched `xd` (the letter), because in ERE `\d` is just an escaped `d`; `grep -P '\d'` matched `x1`. The portable digit class is `[[:digit:]]`, which matched `x1`. The POSIX classes `[:alpha:] [:digit:] [:alnum:] [:space:] [:upper:] [:lower:] [:punct:]` go **inside** a bracket expression, so `[[:digit:]]`, not `[:digit:]`.
- **Greedy against lazy.** `.*` matches as much as possible: `grep -o '<b>.*</b>'` printed the whole `<b>x</b> and <b>y</b>` as one match. PCRE has the lazy `.*?` (`grep -oP '<b>.*?</b>'` printed the two tags separately), and the portable cure in ERE is a **negated class**: `<b>[^<]*</b>` gave the same two matches. You need that form in `sed` and `awk`, which have no lazy quantifier.
- **Lookbehind and `\K` (PCRE).** `(?<=user=)\w+` printed only `alice`, the text after `user=`; `id=\K\d+` printed `42`, because `\K` resets the start of the reported match. These are the usual way to cut one value out of a line without a second command.
- **Backreferences.** `(.)\1` matched lines that contain a doubled character (`aab`, `xyzzy`) in ERE; it needs a backtracking matcher, which GNU `grep` provides for this case.
- **Backtracking can fail loudly.** The pattern `^(a+)+$` is the standard catastrophic case: it needs exponential time on a string of `a` characters ending in `b` in a backtracking engine. Measured with 28 `a` plus `b`: `grep -E` answered in **2 ms** with status 1 (a finite automaton does not backtrack), and `grep -P` printed `exceeded PCRE's backtracking limit` and exited **2** after 30 ms (GNU grep limits the engine; with 40 `a` the result was the same, 20 ms). Two lessons: **an error from `grep -P` is not "no match"**, and **a regex you did not write must not be run with a backtracking engine on untrusted input** (a denial-of-service technique known as ReDoS). Python's `re` has no such limit: the last lines of the transcript show its time roughly **quadrupling for every two characters added** (about 40 ms at 20 characters, 2.7 s at 26), so by extrapolation 28 characters would take about 10 seconds and 40 would take hours.

**Anchors and boundaries.** `^` and `$` anchor to the line; `\b` is a word boundary (GNU); `-w` on `grep` requires the match to be a whole word; `-x` a whole line. **Escaping.** A dot matches any character: `grep 'a.b'` matched both `a.b` and `axb`, while `grep -F 'a.b'` (fixed string) and `grep 'a\.b'` matched only the first; `grep 'file[1].txt'` treated the brackets as a class and matched `file[1].txt` only by accident. **Use `-F` for literal text** (a file name, an IP address with dots, a user-supplied string). **Case.** `-i` ignores case.

---

## 3. grep

`grep PATTERN [FILE...]` prints the lines that match. The options worth knowing from the transcript:

| Option | Meaning | Measured example |
| --- | --- | --- |
| `-c` | count matching lines | 3 for `cat` in four lines (`the cat sat`, `concatenate`, `cat`; not `Cat`) |
| `-w`, `-x` | whole word, whole line | `-w cat` skipped `concatenate`; `-x cat` printed only the line `cat` |
| `-i`, `-v` | ignore case, invert | `-i -c cat` gave 4; `-v cat` printed `Cat` |
| `-n`, `-H` | line numbers, file names | `-n -B1 -A1 5` printed `4-4`, `5:5`, `6-6` (`:` marks a match, `-` marks context) |
| `-A N -B N -C N` | lines after, before, around | context lines are separated by `--` between groups |
| `-m N` | stop after N matches | `-m2` printed `1` and `2` |
| `-o` | print only the matching part | one match per output line |
| `-l`, `-L` | list files with / without a match | `-l two a.txt b.txt bin.dat` listed all three |
| `-r`, `--include=GLOB` | recurse; filter by file name | `grep -rn k1 . --include='*.txt'` |
| `-q` | quiet: no output, only the status | for `if grep -q ...; then` |
| `-F`, `-E`, `-P` | fixed string, extended, Perl | see section 2 |
| `-a` | treat a binary file as text | without it `grep two bin.dat` printed `binary file matches` |
| `-z` | NUL-separated records | pairs with `find -print0` |

**Exit status** is part of the interface and the source of scripting surprises. The measured values: **0** (a line matched), **1** (no line matched), **2** (an error such as a missing file). So `grep -q cat w.txt` returned 0 and `grep -q nosuch w.txt` returned 1, `grep cat nosuchfile` returned 2 with the message on stderr, and `grep -c nosuch w.txt` printed `0` **and still returned 1**. Section 10 shows what that does under `set -e` and `pipefail`.

**Binary files.** A file containing a NUL byte is treated as binary: `grep two bin.dat` printed only `binary file matches` (a message on stderr in the version tested; older versions, from memory, printed `Binary file ... matches` on stdout), and `grep -a` forced the text. **Several files.** With more than one file `grep` prefixes each line with the file name (`-h` suppresses, `-H` forces), and `-c` prints one count per file (`a.txt:1`, `b.txt:1`).

**Other implementations.** `rg` (ripgrep) uses PCRE-like syntax by default, ignores files listed in `.gitignore` and hidden files unless told otherwise, and is multithreaded; in the speed test it counted 500 responses in a 69 MiB file in about **14 ms** against **77 to 88 ms** for GNU `grep` (section 9). BSD `grep` on macOS lacks `-P`. `git grep` searches only tracked files and is the right default inside a repository.

---

## 4. sed

`sed` is a **stream editor**. It reads the input one line at a time into a buffer called the **pattern space**, runs the script on it, prints the pattern space (unless `-n` is given), and moves to the next line. A **hold space** is a second buffer that survives from line to line. A script is a list of `[address]command` pairs.

```bash
#!/usr/bin/env bash
# e2_sed.sh: sed addresses, substitution, the hold space, in-place editing
. "$(dirname "$0")/h.sh"
cd "$(mktemp -d)" || exit 1
printf 'alpha 1\nbeta 2\ngamma 3\ndelta 4\nepsilon 5\n' > f.txt
show "sed -n '2p' f.txt; sed -n '2,3p' f.txt; sed -n '\$p' f.txt; sed -n '\$=' f.txt"
show "sed -n '/beta/,/delta/p' f.txt"
show "sed -n '2~2p' f.txt; sed '2q' f.txt; sed '3,\$d' f.txt"
show "sed '2!d' f.txt; sed -n '/gamma/{n;p}' f.txt"
show "sed 's/a/A/' f.txt | head -2; sed 's/a/A/g' f.txt | head -2; sed 's/a/A/2' f.txt | head -1"
show "echo 'one two three' | sed 's/\\(one\\) \\(two\\)/\\2 \\1/'; echo 'one two three' | sed -E 's/(one) (two)/\\2 \\1/'"
show "echo 'path/to/file' | sed 's/\\//|/g'; echo 'path/to/file' | sed 's#/#|#g'"
show "echo 'hello' | sed 's/l*/X/g'; echo 'hello' | sed 's/l\\+/[&]/'"
show "echo 'Hello World' | sed 's/.*/\\L&/; s/^./\\u&/'; echo 'a.b.c' | sed 's/\\./-/g'"
show "echo '<b>x</b> <b>y</b>' | sed 's/<.*>//'; echo '<b>x</b> <b>y</b>' | sed 's/<[^>]*>//g'"
show "sed -e '1i\\header' -e '\$a\\footer' f.txt | sed -n '1p;\$p'; sed '2c\\CHANGED' f.txt | sed -n 2p"
show "sed -n '/beta/{s/beta/B/;p}' f.txt; sed 's/[0-9]/<&>/;2q' f.txt | tail -1"
show "sed -n 'l' <<< \$'a\\tb\\\\c'"
echo
echo '--- the hold space: tac, join lines, number lines, print the line before a match'
show "sed '1!G;h;\$!d' f.txt"
show "sed ':a;N;\$!ba;s/\\n/,/g' f.txt"
show "sed = f.txt | sed 'N;s/\\n/ /' | head -2"
show "sed -n '/gamma/{x;p;x}; h' f.txt"
echo
echo '--- in-place editing, and what it does to links and backups'
cp f.txt g.txt
show "sed -i.bak 's/alpha/ALPHA/' g.txt; ls; head -1 g.txt g.txt.bak"
rm -f g.txt.bak; cp f.txt g.txt; ln -sf g.txt link.txt
show "sed -i 's/beta/BETA/' link.txt; [ -L link.txt ] && echo 'link.txt is still a symlink' || echo 'link.txt is now a regular file'; sed -n 2p g.txt"
rm -f link.txt; ln -s g.txt link.txt
show "sed --follow-symlinks -i 's/gamma/GAMMA/' link.txt; [ -L link.txt ] && echo 'link.txt is still a symlink' || echo 'link.txt is now a regular file'; sed -n 3p g.txt"
echo
echo '--- CRLF, null-separated, and -s'
printf 'a\r\nb\r\n' > dos.txt
show "sed 's/\\r\$//' dos.txt | od -c | head -1; file dos.txt | sed 's/.*: //'"
printf 'x\ny\0z\0' > z.dat
show "sed -z 's/\\n/ /g' z.dat | od -c | head -1"
show "sed -n '\$=' f.txt g.txt; sed -s -n '\$=' f.txt g.txt"
```

```text
$ sed -n '2p' f.txt; sed -n '2,3p' f.txt; sed -n '$p' f.txt; sed -n '$=' f.txt
beta 2
beta 2
gamma 3
epsilon 5
5
$ sed -n '/beta/,/delta/p' f.txt
beta 2
gamma 3
delta 4
$ sed -n '2~2p' f.txt; sed '2q' f.txt; sed '3,$d' f.txt
beta 2
delta 4
alpha 1
beta 2
alpha 1
beta 2
$ sed '2!d' f.txt; sed -n '/gamma/{n;p}' f.txt
beta 2
delta 4
$ sed 's/a/A/' f.txt | head -2; sed 's/a/A/g' f.txt | head -2; sed 's/a/A/2' f.txt | head -1
Alpha 1
betA 2
AlphA 1
betA 2
alphA 1
$ echo 'one two three' | sed 's/\(one\) \(two\)/\2 \1/'; echo 'one two three' | sed -E 's/(one) (two)/\2 \1/'
two one three
two one three
$ echo 'path/to/file' | sed 's/\//|/g'; echo 'path/to/file' | sed 's#/#|#g'
path|to|file
path|to|file
$ echo 'hello' | sed 's/l*/X/g'; echo 'hello' | sed 's/l\+/[&]/'
XhXeXoX
he[ll]o
$ echo 'Hello World' | sed 's/.*/\L&/; s/^./\u&/'; echo 'a.b.c' | sed 's/\./-/g'
Hello world
a-b-c
$ echo '<b>x</b> <b>y</b>' | sed 's/<.*>//'; echo '<b>x</b> <b>y</b>' | sed 's/<[^>]*>//g'

x y
$ sed -e '1i\header' -e '$a\footer' f.txt | sed -n '1p;$p'; sed '2c\CHANGED' f.txt | sed -n 2p
header
footer
CHANGED
$ sed -n '/beta/{s/beta/B/;p}' f.txt; sed 's/[0-9]/<&>/;2q' f.txt | tail -1
B 2
beta <2>
$ sed -n 'l' <<< $'a\tb\\c'
a\tb\\c$

--- the hold space: tac, join lines, number lines, print the line before a match
$ sed '1!G;h;$!d' f.txt
epsilon 5
delta 4
gamma 3
beta 2
alpha 1
$ sed ':a;N;$!ba;s/\n/,/g' f.txt
alpha 1,beta 2,gamma 3,delta 4,epsilon 5
$ sed = f.txt | sed 'N;s/\n/ /' | head -2
1 alpha 1
2 beta 2
$ sed -n '/gamma/{x;p;x}; h' f.txt
beta 2

--- in-place editing, and what it does to links and backups
$ sed -i.bak 's/alpha/ALPHA/' g.txt; ls; head -1 g.txt g.txt.bak
f.txt
g.txt
g.txt.bak
==> g.txt <==
ALPHA 1

==> g.txt.bak <==
alpha 1
$ sed -i 's/beta/BETA/' link.txt; [ -L link.txt ] && echo 'link.txt is still a symlink' || echo 'link.txt is now a regular file'; sed -n 2p g.txt
link.txt is now a regular file
beta 2
$ sed --follow-symlinks -i 's/gamma/GAMMA/' link.txt; [ -L link.txt ] && echo 'link.txt is still a symlink' || echo 'link.txt is now a regular file'; sed -n 3p g.txt
link.txt is still a symlink
GAMMA 3

--- CRLF, null-separated, and -s
$ sed 's/\r$//' dos.txt | od -c | head -1; file dos.txt | sed 's/.*: //'
0000000   a  \n   b  \n
ASCII text, with CRLF line terminators
$ sed -z 's/\n/ /g' z.dat | od -c | head -1
0000000   x       y  \0   z  \0
$ sed -n '$=' f.txt g.txt; sed -s -n '$=' f.txt g.txt
10
5
5
```

### 4.1 Addresses: which lines a command applies to

| Address | Meaning | Measured on the five lines `alpha 1` to `epsilon 5` |
| --- | --- | --- |
| `N` | line number N | `sed -n '2p'` printed `beta 2` |
| `$` | the last line | `sed -n '$p'` printed `epsilon 5`; `sed -n '$='` printed the line count, `5` |
| `N,M` | a range of lines | `2,3p` printed `beta 2` and `gamma 3` |
| `/re/` | lines matching a regex | |
| `/a/,/b/` | from a match of `a` to the next match of `b` | `/beta/,/delta/p` printed `beta`, `gamma`, `delta` |
| `first~step` (GNU) | every step-th line | `2~2p` printed `beta 2` and `delta 4` |
| `addr!` | lines that do **not** match | `2!d` deleted every line except line 2 |

A command can be followed by `{ ... }` to apply several commands to an address (`/beta/{s/beta/B/;p}`). **A range whose end never matches runs to the end of the input**: `sed -n '/one/,/nomatch/p'` printed both lines (section 10). If the end is a regex, it is first tried on the line **after** the start line, so the range cannot end on its own first line (from the `sed` manual, not run here).

### 4.2 The commands worth knowing

| Command | Effect | Measured |
| --- | --- | --- |
| `p` | print the pattern space (use with `-n`, or it prints twice) | |
| `d` | delete the pattern space and start the next cycle | `3,$d` kept lines 1 and 2 |
| `q` | print (unless `-n`) and quit | `sed '2q'` printed two lines and stopped reading (useful on huge files) |
| `s/re/new/flags` | substitute | below |
| `y/abc/xyz/` | transliterate characters | like `tr` |
| `i\text`, `a\text`, `c\text` (GNU one-line forms) | insert before, append after, replace the line | `-e '1i\header' -e '$a\footer'` added a header and a footer; `2c\CHANGED` replaced line 2 |
| `n`, `N` | read the **next** line (replacing / appending to the pattern space) | `/gamma/{n;p}` printed `delta 4`, the line after the match |
| `h`, `H`, `g`, `G`, `x` | copy / append to hold; get / append from hold; exchange | section 4.4 |
| `=` | print the line number | `sed = f.txt \| sed 'N;s/\n/ /'` numbered the lines (`1 alpha 1`) |
| `l` | print unambiguously | `a\tb\\c$`: tab as `\t`, backslash doubled, `$` marks the end |

### 4.3 Substitution

`s/regex/replacement/flags` replaces the first match in the line. The flags and tricks, with what the transcript showed:

- **`g`** replaces all matches; a **number** replaces only that occurrence (`s/a/A/2` on `alpha 1` gave `alphA 1`, the second `a`; `s/a/A/` gave `Alpha 1`; `s/a/A/g` gave `AlphA 1`). **`p`** prints the line if a substitution happened (use with `-n`); **`I`** ignores case.
- **`&`** is the whole match and `\1` to `\9` are groups. In **BRE** a group is `\( \)` and in **ERE** (`sed -E`) it is `( )`: `s/\(one\) \(two\)/\2 \1/` and `sed -E 's/(one) (two)/\2 \1/'` both turned `one two three` into `two one three`.
- **Any delimiter works**: `s#/#|#g` is easier to read than `s/\//|/g` and both gave `path|to|file`. Use another delimiter whenever the text has `/`, and **escape the delimiter if it occurs in a variable** (section 10: `v='a/b'; sed "s/a/$v/"` failed with `unknown option to 's'`).
- **GNU case conversion** in the replacement: `\L&` lowercases, `\u&` uppercases the next character, so `s/.*/\L&/; s/^./\u&/` turned `Hello World` into `Hello world`. (From memory: BSD `sed` lacks `\L` and `\u`.)
- **Empty matches.** `echo hello | sed 's/l*/X/g'` printed `XhXeXoX`: `l*` also matches the **empty** string at every position, so X is inserted between all other characters, and the `ll` became one X. Use `l\+` (one or more) when you mean "at least one": `s/l\+/[&]/` gave `he[ll]o`.
- **Greedy matching.** `sed 's/<.*>//'` on `<b>x</b> <b>y</b>` deleted **everything** from the first `<` to the last `>` and left an empty line; the negated class `s/<[^>]*>//g` removed only the tags and left `x y`. `sed` has no lazy quantifier.
- **Special characters in the pattern** (`.`, `*`, `[`, `^`, `$`, `\`): escape them or use a bracket (`s/\./-/g` turned `a.b.c` into `a-b-c`).

### 4.4 The hold space: the one trick worth learning

Each cycle, `h` copies the pattern space to the hold space, `G` appends the hold space to the pattern space, `x` swaps them. Three idioms from the transcript:

- **`sed '1!G;h;$!d'` is `tac`** (reverse the lines): on every line except the first, append the hold space (all previous lines, already reversed) to the current line, copy the result back to hold, and delete the line unless it is the last; the last line is printed with everything before it, in reverse. It printed `epsilon 5` first and `alpha 1` last.
- **`sed ':a;N;$!ba;s/\n/,/g'` joins all lines**: define label `a`, append the next line with `N`, branch back to `a` while not on the last line, then replace the newlines. Output: `alpha 1,beta 2,gamma 3,delta 4,epsilon 5`. (A one-line alternative is `paste -sd,`.)
- **`sed -n '/gamma/{x;p;x}; h'`** prints the line **before** each match: on each line `h` saves it, and on a match `x` brings the saved previous line into the pattern space, `p` prints it, and `x` restores the current line. It printed `beta 2`.

If a script needs more than two such tricks, use `awk` or Python; hold-space code is write-only.

### 4.5 Editing files in place, and what it does to them

- **`sed -i`** rewrites the file: it writes a new file next to the original and **renames it over the original**, so it has a new inode and (unless `-i` is given a suffix) no backup. **`sed -i.bak`** keeps the original as `g.txt.bak`: the transcript shows `g.txt` holding `ALPHA 1` and `g.txt.bak` holding `alpha 1`. **There is no undo without the suffix.**
- **On a symlink, plain `-i` replaces the link with a regular file**: the transcript printed `link.txt is now a regular file` and the target `g.txt` still held the old text (`beta 2`). **`--follow-symlinks`** edited the target (`GAMMA 3`) and the link stayed a link. Hard links and file ownership and mode are subject to the same rename (permissions are copied in GNU `sed`; hard links are not preserved, from memory).
- **BSD and macOS `sed -i` takes a mandatory suffix argument** (`sed -i '' 's/a/b/' file`), so a one-liner with `-i` is not portable between them (from memory).
- **`-s`** treats each file separately for `$` and line numbers: `sed -n '$=' f.txt g.txt` counted **10** (one stream), while `sed -s -n '$=' f.txt g.txt` printed `5` and `5`.
- **`-z`** uses NUL as the line separator, so `sed -z 's/\n/ /g'` can edit across newlines; **`s/\r$//`** removes the carriage return of a Windows (CRLF) file, and `file` reports `ASCII text, with CRLF line terminators`.
- **`sed -n`** with no `p` prints nothing, which is the usual cause of "my sed printed nothing".

---

## 5. awk

`awk` is a small language for **records and fields**. A program is a list of `pattern { action }` pairs. For each input line (a **record**), awk splits it into **fields** (`$1`, `$2`, ... , `$NF`; `$0` is the whole line), then runs every action whose pattern is true. A missing pattern means "always", a missing action means "print the line". `BEGIN { }` runs before the first line and `END { }` after the last.

```bash
#!/usr/bin/env bash
# e3_awk.sh: awk as a language: fields, patterns, variables, arrays, and the traps
. "$(dirname "$0")/h.sh"
cd "$(mktemp -d)" || exit 1
printf 'alice 30 paris\nbob 25 delhi\ncarol 35 paris\ndave 28 tokyo\n' > p.txt
show "awk '{print \$1, \$NF}' p.txt | head -2; awk '{print NR, NF}' p.txt | head -1"
show "awk '\$2 > 28' p.txt; awk '\$3 == \"paris\"' p.txt | wc -l; awk '/^[bc]/' p.txt"
show "awk 'NR==2,NR==3' p.txt; awk 'NR%2==0' p.txt; awk 'END{print NR}' p.txt"
show "awk 'BEGIN{print \"name age\"} {s+=\$2} END{print \"total\", s, \"mean\", s/NR}' p.txt"
show "awk '{c[\$3]++} END{for (k in c) print k, c[k]}' p.txt | sort"
show "awk '{sum[\$3]+=\$2; n[\$3]++} END{for (k in sum) printf \"%s %.1f\\n\", k, sum[k]/n[k]}' p.txt | sort"
show "awk -F: '{print \$1}' /etc/passwd | head -2; awk -F'[ :]+' '{print \$2}' <<< 'a: b:  c'"
show "awk -v n=30 '\$2 >= n {print \$1}' p.txt; x=29 awk '\$2 > ENVIRON[\"x\"] {print \$1}' p.txt"
show "echo 'a   b    c' | awk '{print \$2}'; echo 'a   b    c' | awk '{\$1=\$1; print}'; echo 'a   b    c' | awk -F' ' '{print NF}'; echo 'a   b    c' | awk -F'[ ]' '{print NF}'"
show "echo 'a,b,c' | awk 'BEGIN{FS=OFS=\",\"} {\$2=\"X\"; print}'; echo 'a b c' | awk '{\$5=\"e\"; print; print NF}'"
show "awk '{print length(\$1), toupper(substr(\$1,1,1)) substr(\$1,2)}' p.txt | head -2; awk 'BEGIN{print index(\"hello\",\"ll\"), split(\"a:b:c\", p, \":\"), p[3]}'"
show "awk 'BEGIN{s=\"foo bar foo\"; n=gsub(/foo/,\"X\",s); print n, s; print match(\"abc123\", /[0-9]+/), RSTART, RLENGTH}'"
show "awk '!seen[\$3]++' p.txt"
show "awk 'NR==FNR{a[\$1]=\$3; next} \$1 in a {print \$1, a[\$1]}' p.txt <(printf 'bob\\ndave\\nzed\\n')"
show "awk '{printf \"%-6s|%5.1f|%03d\\n\", \$1, \$2, NR}' p.txt | head -2"
show "awk 'BEGIN{while ((\"echo hi; echo there\" | getline line) > 0) print \"got\", line}'"
echo
echo '--- comparisons: numeric or string depends on the operands'
show "echo '10 9' | awk '{print (\$1 > \$2), (\$1\"\" > \$2\"\")}'"
show "echo '10 abc' | awk '{print (\$1 > \$2)}'; awk 'BEGIN{print (\"10\" > \"9\"), (10 > 9), (\"a\" < \"b\")}'"
show "awk 'BEGIN{x; print x+0, \"[\" x \"]\", length(x); if (!x) print \"uninitialised is false\"}'"
show "echo '0x1f 1e3 010' | awk '{print \$1+0, \$2+0, \$3+0}'"
show "awk 'BEGIN{print 2^10, 7%3, -7%3, int(-7/2), int(7/2), 1/3}'"
show "awk 'BEGIN{printf \"%d %d\\n\", 2^31, 2^53+1; print 2^53+1}'"
show "awk 'BEGIN{print substr(\"hello\", 2, 3), substr(\"hello\", 0, 2), substr(\"hello\", -1, 3)}'"
echo
echo '--- gawk against mawk against the POSIX subset'
show "awk --version | head -1; gawk 'BEGIN{print strftime(\"%Y\", 0)}'; mawk 'BEGIN{print strftime(\"%Y\", 0)}' 2>&1 | head -1"
show "gawk 'BEGIN{n=split(\"c a b\", a); asort(a); print a[1] a[2] a[3]}'; mawk 'BEGIN{n=split(\"c a b\", a); asort(a)}' 2>&1 | head -1"
show "echo 'x=1;y=22' | gawk '{match(\$0, /y=([0-9]+)/, m); print m[1]}'; echo 'x=1;y=22' | mawk '{match(\$0, /y=([0-9]+)/, m); print m[1]}' 2>&1 | head -1"
show "gawk 'BEGIN{print length(\"héllo\")}'; LC_ALL=C.utf8 gawk 'BEGIN{print length(\"héllo\")}'; mawk 'BEGIN{print length(\"héllo\")}'"
show "printf 'a\\0b\\0' | gawk -v RS='\\0' 'END{print NR}'; printf 'a;b;c' | awk -v RS=';' 'END{print NR}'"
```

```text
$ awk '{print $1, $NF}' p.txt | head -2; awk '{print NR, NF}' p.txt | head -1
alice paris
bob delhi
1 3
$ awk '$2 > 28' p.txt; awk '$3 == "paris"' p.txt | wc -l; awk '/^[bc]/' p.txt
alice 30 paris
carol 35 paris
2
bob 25 delhi
carol 35 paris
$ awk 'NR==2,NR==3' p.txt; awk 'NR%2==0' p.txt; awk 'END{print NR}' p.txt
bob 25 delhi
carol 35 paris
bob 25 delhi
dave 28 tokyo
4
$ awk 'BEGIN{print "name age"} {s+=$2} END{print "total", s, "mean", s/NR}' p.txt
name age
total 118 mean 29.5
$ awk '{c[$3]++} END{for (k in c) print k, c[k]}' p.txt | sort
delhi 1
paris 2
tokyo 1
$ awk '{sum[$3]+=$2; n[$3]++} END{for (k in sum) printf "%s %.1f\n", k, sum[k]/n[k]}' p.txt | sort
delhi 25.0
paris 32.5
tokyo 28.0
$ awk -F: '{print $1}' /etc/passwd | head -2; awk -F'[ :]+' '{print $2}' <<< 'a: b:  c'
root
daemon
b
$ awk -v n=30 '$2 >= n {print $1}' p.txt; x=29 awk '$2 > ENVIRON["x"] {print $1}' p.txt
alice
carol
alice
carol
$ echo 'a   b    c' | awk '{print $2}'; echo 'a   b    c' | awk '{$1=$1; print}'; echo 'a   b    c' | awk -F' ' '{print NF}'; echo 'a   b    c' | awk -F'[ ]' '{print NF}'
b
a b c
3
8
$ echo 'a,b,c' | awk 'BEGIN{FS=OFS=","} {$2="X"; print}'; echo 'a b c' | awk '{$5="e"; print; print NF}'
a,X,c
a b c  e
5
$ awk '{print length($1), toupper(substr($1,1,1)) substr($1,2)}' p.txt | head -2; awk 'BEGIN{print index("hello","ll"), split("a:b:c", p, ":"), p[3]}'
5 Alice
3 Bob
3 3 c
$ awk 'BEGIN{s="foo bar foo"; n=gsub(/foo/,"X",s); print n, s; print match("abc123", /[0-9]+/), RSTART, RLENGTH}'
2 X bar X
4 4 3
$ awk '!seen[$3]++' p.txt
alice 30 paris
bob 25 delhi
dave 28 tokyo
$ awk 'NR==FNR{a[$1]=$3; next} $1 in a {print $1, a[$1]}' p.txt <(printf 'bob\ndave\nzed\n')
bob delhi
dave tokyo
$ awk '{printf "%-6s|%5.1f|%03d\n", $1, $2, NR}' p.txt | head -2
alice | 30.0|001
bob   | 25.0|002
$ awk 'BEGIN{while (("echo hi; echo there" | getline line) > 0) print "got", line}'
got hi
got there

--- comparisons: numeric or string depends on the operands
$ echo '10 9' | awk '{print ($1 > $2), ($1"" > $2"")}'
1 0
$ echo '10 abc' | awk '{print ($1 > $2)}'; awk 'BEGIN{print ("10" > "9"), (10 > 9), ("a" < "b")}'
0
0 1 1
$ awk 'BEGIN{x; print x+0, "[" x "]", length(x); if (!x) print "uninitialised is false"}'
0 [] 0
uninitialised is false
$ echo '0x1f 1e3 010' | awk '{print $1+0, $2+0, $3+0}'
0 1000 10
$ awk 'BEGIN{print 2^10, 7%3, -7%3, int(-7/2), int(7/2), 1/3}'
1024 1 -1 -3 3 0.333333
$ awk 'BEGIN{printf "%d %d\n", 2^31, 2^53+1; print 2^53+1}'
2147483648 9007199254740992
9007199254740992
$ awk 'BEGIN{print substr("hello", 2, 3), substr("hello", 0, 2), substr("hello", -1, 3)}'
ell he hel

--- gawk against mawk against the POSIX subset
$ awk --version | head -1; gawk 'BEGIN{print strftime("%Y", 0)}'; mawk 'BEGIN{print strftime("%Y", 0)}' 2>&1 | head -1
GNU Awk 5.2.1, API 3.2, PMA Avon 8-g1, (GNU MPFR 4.2.1, GNU MP 6.3.0)
1970
1970
$ gawk 'BEGIN{n=split("c a b", a); asort(a); print a[1] a[2] a[3]}'; mawk 'BEGIN{n=split("c a b", a); asort(a)}' 2>&1 | head -1
abc
mawk: line 2: function asort never defined
$ echo 'x=1;y=22' | gawk '{match($0, /y=([0-9]+)/, m); print m[1]}'; echo 'x=1;y=22' | mawk '{match($0, /y=([0-9]+)/, m); print m[1]}' 2>&1 | head -1
22
mawk: line 1: syntax error at or near ,
$ gawk 'BEGIN{print length("héllo")}'; LC_ALL=C.utf8 gawk 'BEGIN{print length("héllo")}'; mawk 'BEGIN{print length("héllo")}'
6
5
6
$ printf 'a\0b\0' | gawk -v RS='\0' 'END{print NR}'; printf 'a;b;c' | awk -v RS=';' 'END{print NR}'
2
3
```

### 5.1 Fields, patterns and built-in variables

| Variable | Meaning |
| --- | --- |
| `$0`, `$1` ... `$NF` | the line, the fields; `NF` is the field count, `$NF` the last field |
| `NR`, `FNR` | line number overall, and within the current file |
| `FS`, `OFS` | input and output field separator (default: runs of blanks / one space); `-F` sets `FS` |
| `RS`, `ORS` | input and output record separator (default newline) |
| `FILENAME`, `ENVIRON`, `ARGV` | current file, the environment array, the arguments |

The measured patterns, on four lines `alice 30 paris`, `bob 25 delhi`, `carol 35 paris`, `dave 28 tokyo`:

- **Comparison pattern**: `awk '$2 > 28'` printed alice and carol; `awk '$3 == "paris"'` matched 2 lines; **regex pattern** `/^[bc]/` printed bob and carol; **range** `NR==2,NR==3` printed bob and carol; **arithmetic** `NR%2==0` printed bob and dave; `END{print NR}` gave 4.
- **Aggregation**: `{s+=$2} END{print "total", s, "mean", s/NR}` printed `total 118 mean 29.5`; **grouping with an associative array**: `{c[$3]++} END{for (k in c) print k, c[k]}` gave `paris 2`, `delhi 1`, `tokyo 1` (the `for (k in c)` order is **unspecified**, so the transcript pipes through `sort`); two parallel arrays `sum[$3]+=$2; n[$3]++` gave a mean per city (`paris 32.5`).
- **Separators**: `-F: ` or `-F'[ :]+'` (a regex); `-v n=30` passes a variable into the program (`$2 >= n`), and `ENVIRON["x"]` reads the environment. **Never splice a shell variable into the program text** (section 10 shows `system()` being executed from one).
- **The default `FS` is special**: runs of blanks separate fields and leading blanks are ignored (`echo 'a   b    c' | awk '{print $2}'` printed `b`, and `NF` was 3). A literal single-space separator written as a regex, `-F'[ ]'`, treats **every** space as a separator and gave `NF` = 8 (empty fields between the spaces).
- **Assigning to a field rebuilds `$0`** with `OFS`: `{$1=$1; print}` normalised the spacing (`a b c`); `BEGIN{FS=OFS=","} {$2="X"; print}` printed `a,X,c`; assigning `$5` on a three-field line created two new fields and `NF` became 5 (`a b c  e`).
- **Functions**: `length`, `substr(s, start, len)` (**1-based**), `index`, `split(s, arr, sep)`, `sub`, `gsub`, `match` (sets `RSTART` and `RLENGTH`), `toupper`, `tolower`, `sprintf`, `int`, `sin`, `sqrt`, `rand`. The transcript printed `5 Alice`, `3 3 c` (index, split count, the third piece), `2 X bar X` (`gsub` returns the count) and `4 4 3` (`match` found digits at position 4, length 3). **`printf`** works as in C: `%-6s|%5.1f|%03d` printed `alice | 30.0|001`.
- **Idioms**: `!seen[$3]++` prints a line the first time each value of field 3 appears (**deduplicate and keep order**; `sort -u` loses the order); `NR==FNR{a[$1]=$3; next} $1 in a {...}` loads the **first** file into an array and then filters the **second** file against it (a join without sorting; the two files have equal `NR` and `FNR` only while reading the first); `"cmd" | getline line` reads a command's output.

### 5.2 Strings, numbers and comparisons

`awk` variables are **untyped**: each value is a string, a number, or a string that looks like a number (a **strnum**, such as a field read from input), and comparison depends on that.

- **Two numbers or two strnum values compare numerically; otherwise they compare as strings.** For input `10 9`, `$1 > $2` was 1 (numeric), and `$1"" > $2""` (forced strings) was 0 (`"10"` sorts before `"9"`). For `10 abc`, `$1 > $2` was **0**, because `abc` is not numeric so the comparison became a string comparison. Constants in the program: `("10" > "9")` was **0** (string order) while `(10 > 9)` was 1. **`$1 > "10"`** compared as a string, so on the line `5 apples` it reported "string compare says big".
- **Uninitialised variables are both `0` and `""`**: `x+0` was 0, `"[" x "]"` was `[]`, `length(x)` was 0 and `!x` was true.
- **Number parsing is decimal and C-like**: `0x1f` became 0 (gawk does not read hexadecimal from input by default), `1e3` became 1000, `010` stayed 10 (not octal).
- **Doubles, not integers.** `2^53+1` printed `9007199254740992`: awk numbers are double-precision floats, so integers above 2^53 lose precision. `%` and `int()` truncate toward zero (`-7%3` is -1, `int(-7/2)` is -3), `^` is power, `1/3` printed `0.333333` (default output format `%.6g`).
- **Concatenation has no operator**: `print $1 $2` printed `ab`, `print $1, $2` printed `a b`, and `$1 -$2` is **subtraction**: on the line `1 2` it printed `-1`, next to `3` for `$1 + $2` and `12` for `$1 $2`. When in doubt, put spaces and parentheses.
- **`substr` with a start below 1 is not portable.** `substr("hello", 0, 2)` printed `he` and `substr("hello", -1, 3)` printed `hel` in this `gawk` 5.2.1, and `mawk` printed `he` and `hell` for the same calls (checked separately). Do not rely on out-of-range starts.

### 5.3 gawk, mawk and what is portable

`awk` on Debian and Ubuntu is often `mawk` (small and fast to start); on this machine `awk` is `gawk`. POSIX awk is the common subset. The measured differences:

| Feature | gawk 5.2.1 | mawk |
| --- | --- | --- |
| `strftime` | works (`1970`) | **also worked** here (printed `1970`) |
| `asort` (sort an array) | works (`abc`) | `function asort never defined` |
| `match(s, re, arr)` with a capture array | works (`22`) | **syntax error** (the third argument is a gawk extension) |
| `length("héllo")` | 6 in the `C` locale, **5 under `LC_ALL=C.utf8`** | 6 (counts bytes) |
| NUL as the record separator (`RS='\0'`) | works (2 records) | not tested |

So **a script that uses `asort`, `gensub`, three-argument `match`, `PROCINFO`, `strftime` or `@include` needs gawk**; a script for any machine sticks to POSIX features. In the speed test (section 9) `gawk` and `mawk` were within a factor of 1.5 of each other on `print $1` (116 and 169 ms, `gawk` ahead), so "mawk is faster" is **not** a safe assumption on this version; measure.

**When to leave awk**: when you need nested data, real functions with local arrays, floating-point exactness or more than about 20 lines; then use Python. **When awk beats the pipeline**: any time you would write `cut | sort | uniq -c | sort -rn` for a table whose keys fit in memory, `awk '{c[$1]++} END{...}'` does it without sorting 600,000 lines (section 9: 152 ms against 258 ms).

---

## 6. The small tools: `cut`, `tr`, `sort`, `uniq`, `join`, `comm`, `paste`, `column`

These are older than `awk` and still the quickest way to say a simple thing. The transcript runs each on a five-line CSV (`bob,25,delhi`, `alice,30,paris`, and so on) and on small word lists.

```bash
#!/usr/bin/env bash
# e4_coreutils.sh: cut, tr, sort, uniq, join, comm, paste, column, tee, and the sort-order traps
. "$(dirname "$0")/h.sh"
export LOCPATH=/tmp/loc
show() { local cmd=$1 out; printf '$ %s\n' "$cmd"; out=$(eval "$cmd" 2>&1); printf '%s\n' "$out"; }
cd "$(mktemp -d)" || exit 1
printf 'bob,25,delhi\nalice,30,paris\ncarol,35,paris\ndave,28,tokyo\nerin,30,delhi\n' > p.csv
show "cut -d, -f1,3 p.csv | head -2; cut -d, -f2- p.csv | head -1; cut -c1-3 p.csv | head -2"
show "echo 'a   b' | cut -d' ' -f2; echo 'a   b' | tr -s ' ' | cut -d' ' -f2"
show "echo 'Hello, World 123' | tr 'a-z' 'A-Z'; echo 'Hello, World 123' | tr -d '[:digit:]'; echo 'Hello, World 123' | tr -cd '[:alpha:]'; printf 'a\\nb\\n' | tr '\\n' ' '"
show "sort -t, -k2,2n p.csv | head -2; sort -t, -k2,2nr -k1,1 p.csv | head -3"
show "sort -t, -k3,3 -k2,2n p.csv | head -3; sort -t, -k3 p.csv | head -2"
show "printf '10\\n9\\n2\\n100\\n' | sort | tr '\\n' ' '; printf '10\\n9\\n2\\n100\\n' | sort -n | tr '\\n' ' '; printf '1K\\n1M\\n10\\n' | sort -h | tr '\\n' ' '; printf 'v1.10\\nv1.9\\nv1.2\\n' | sort -V | tr '\\n' ' '"
show "printf 'b 1\\na 2\\nb 3\\na 4\\n' | sort -k1,1 -s | tr '\\n' ';'; echo; printf 'b 1\\na 2\\nb 3\\na 4\\n' | sort -u -k1,1 | tr '\\n' ';'; echo; printf 'x\\ny\\nx\\n' | sort -u | tr '\\n' ' '"
show "printf 'a\\na\\nb\\na\\n' | uniq | tr '\\n' ' '; printf 'a\\na\\nb\\na\\n' | sort | uniq -c | tr '\\n' ';'; echo; printf 'a\\na\\nb\\na\\n' | sort | uniq -d; printf 'a\\na\\nb\\na\\n' | sort | uniq -u"
show "printf 'b\\nA\\na\\nB\\n' | LC_ALL=C sort | tr '\\n' ' '; printf 'b\\nA\\na\\nB\\n' | LC_ALL=en_US.UTF-8 sort | tr '\\n' ' '; printf 'a b\\nab\\na c\\n' | LC_ALL=C sort | tr '\\n' ';'; echo; printf 'a b\\nab\\na c\\n' | LC_ALL=en_US.UTF-8 sort | tr '\\n' ';'"
echo
echo '--- join and comm need sorted input (and the same collation)'
printf 'a 1\nb 2\nc 3\n' > l.txt; printf 'b x\nc y\nd z\n' > r.txt
show "join l.txt r.txt; join -a1 l.txt r.txt; join -v1 l.txt r.txt; join -o 1.1,2.2 l.txt r.txt"
printf 'b\nc\nd\n' > s2; printf 'a\nb\nc\n' > s1
show "comm s1 s2 | cat -A | head -4; comm -12 s1 s2 | tr '\\n' ' '; comm -23 s1 s2 | tr '\\n' ' '; comm -13 s1 s2 | tr '\\n' ' '"
printf 'b 1\na 2\n' > uns; printf 'a x\nb y\n' > srt
show "join uns srt; join --check-order uns srt 2>&1 | head -3"
show "paste -d, <(echo a; echo b) <(echo 1; echo 2); paste -sd, s1; paste -sd'+' <(seq 1 5) | bc"
show "column -t -s, p.csv | head -2; printf 'a b\\nccc d\\n' | column -t; printf 'x y\\n' | tee o1 o2 >/dev/null; cat o1 o2 | wc -l"
show "seq 1 5 | tee >(sed -n '\$p' >&2) | wc -l"
show "wc -l p.csv; wc -c < p.csv; wc -w < p.csv; head -n -3 p.csv; tail -n +4 p.csv; tail -n 2 p.csv | head -1; tac p.csv | head -1; rev <<< abc; nl -ba s1 | head -1; expand <<< \$'a\\tb' | cat -A"
```

```text
$ cut -d, -f1,3 p.csv | head -2; cut -d, -f2- p.csv | head -1; cut -c1-3 p.csv | head -2
bob,delhi
alice,paris
25,delhi
bob
ali
$ echo 'a   b' | cut -d' ' -f2; echo 'a   b' | tr -s ' ' | cut -d' ' -f2

b
$ echo 'Hello, World 123' | tr 'a-z' 'A-Z'; echo 'Hello, World 123' | tr -d '[:digit:]'; echo 'Hello, World 123' | tr -cd '[:alpha:]'; printf 'a\nb\n' | tr '\n' ' '
HELLO, WORLD 123
Hello, World
HelloWorlda b
$ sort -t, -k2,2n p.csv | head -2; sort -t, -k2,2nr -k1,1 p.csv | head -3
bob,25,delhi
dave,28,tokyo
carol,35,paris
alice,30,paris
erin,30,delhi
$ sort -t, -k3,3 -k2,2n p.csv | head -3; sort -t, -k3 p.csv | head -2
bob,25,delhi
erin,30,delhi
alice,30,paris
bob,25,delhi
erin,30,delhi
$ printf '10\n9\n2\n100\n' | sort | tr '\n' ' '; printf '10\n9\n2\n100\n' | sort -n | tr '\n' ' '; printf '1K\n1M\n10\n' | sort -h | tr '\n' ' '; printf 'v1.10\nv1.9\nv1.2\n' | sort -V | tr '\n' ' '
10 100 2 9 2 9 10 100 10 1K 1M v1.2 v1.9 v1.10
$ printf 'b 1\na 2\nb 3\na 4\n' | sort -k1,1 -s | tr '\n' ';'; echo; printf 'b 1\na 2\nb 3\na 4\n' | sort -u -k1,1 | tr '\n' ';'; echo; printf 'x\ny\nx\n' | sort -u | tr '\n' ' '
a 2;a 4;b 1;b 3;
a 2;b 1;
x y
$ printf 'a\na\nb\na\n' | uniq | tr '\n' ' '; printf 'a\na\nb\na\n' | sort | uniq -c | tr '\n' ';'; echo; printf 'a\na\nb\na\n' | sort | uniq -d; printf 'a\na\nb\na\n' | sort | uniq -u
a b a       3 a;      1 b;
a
b
$ printf 'b\nA\na\nB\n' | LC_ALL=C sort | tr '\n' ' '; printf 'b\nA\na\nB\n' | LC_ALL=en_US.UTF-8 sort | tr '\n' ' '; printf 'a b\nab\na c\n' | LC_ALL=C sort | tr '\n' ';'; echo; printf 'a b\nab\na c\n' | LC_ALL=en_US.UTF-8 sort | tr '\n' ';'
A B a b a A b B a b;a c;ab;
a b;ab;a c;

--- join and comm need sorted input (and the same collation)
$ join l.txt r.txt; join -a1 l.txt r.txt; join -v1 l.txt r.txt; join -o 1.1,2.2 l.txt r.txt
b 2 x
c 3 y
a 1
b 2 x
c 3 y
a 1
b x
c y
$ comm s1 s2 | cat -A | head -4; comm -12 s1 s2 | tr '\n' ' '; comm -23 s1 s2 | tr '\n' ' '; comm -13 s1 s2 | tr '\n' ' '
a$
^I^Ib$
^I^Ic$
^Id$
b c a d
$ join uns srt; join --check-order uns srt 2>&1 | head -3
join: uns:2: is not sorted: a 2
b 1 y
join: input is not in sorted order
join: uns:2: is not sorted: a 2
$ paste -d, <(echo a; echo b) <(echo 1; echo 2); paste -sd, s1; paste -sd'+' <(seq 1 5) | bc
a,1
b,2
a,b,c
15
$ column -t -s, p.csv | head -2; printf 'a b\nccc d\n' | column -t; printf 'x y\n' | tee o1 o2 >/dev/null; cat o1 o2 | wc -l
bob    25  delhi
alice  30  paris
a    b
ccc  d
2
$ seq 1 5 | tee >(sed -n '$p' >&2) | wc -l
5
5
$ wc -l p.csv; wc -c < p.csv; wc -w < p.csv; head -n -3 p.csv; tail -n +4 p.csv; tail -n 2 p.csv | head -1; tac p.csv | head -1; rev <<< abc; nl -ba s1 | head -1; expand <<< $'a\tb' | cat -A
5 p.csv
71
5
bob,25,delhi
alice,30,paris
dave,28,tokyo
erin,30,delhi
dave,28,tokyo
erin,30,delhi
cba
     1  a
a       b$
```

### 6.1 `cut` and `tr`

- **`cut -d, -f1,3`** picks fields by a **single-character** delimiter (`bob,delhi`); `-f2-` means "from field 2 to the end" (`25,delhi`); `-c1-3` picks characters (`ali`); `-b` picks bytes. `cut` has **no regex delimiter and no "run of spaces"**: `echo 'a   b' | cut -d' ' -f2` printed an **empty** field (the space between the first two of the three spaces), and `tr -s ' '` first (squeeze runs into one) fixed it, giving `b`. When the columns are separated by runs of blanks, `awk '{print $2}'` is the right tool.
- **`tr SET1 SET2`** translates characters, and works on bytes, not words: `tr 'a-z' 'A-Z'` upper-cased the text, `tr -d '[:digit:]'` deleted digits, `tr -cd '[:alpha:]'` kept **only** letters (`-c` complements the set), `tr -s` squeezes repeats, and `tr '\n' ' '` joins lines. It reads standard input only, takes no file argument and no regex.

### 6.2 `sort`

`sort` orders lines by **keys**. The rules that matter:

- **The default is lexicographic**, so numbers misorder: `10 9 2 100` sorted as `10 100 2 9`. **`-n`** compares numerically (`2 9 10 100`), **`-h`** understands size suffixes (`10 1K 1M`), **`-V`** understands version numbers (`v1.2 v1.9 v1.10`, where a plain sort would put `v1.10` before `v1.2`), **`-r`** reverses.
- **`-k F[,L]`** sorts by the key from field F to field L. **Always give the end of the key**: `-k2` means "from field 2 to the **end of the line**", so later fields take part in the comparison and can give a surprise. Write `-k2,2`. Modifiers attach to a key: `-k2,2nr` is numeric and reverse for that key only. `-t,` sets a single-character separator; without `-t` the separator is a transition from non-blank to blank, and **leading blanks belong to the next field** (use `-b`).
- **Several keys** break ties: `sort -t, -k2,2nr -k1,1` ordered by salary descending, then name ascending (`carol,35`, `alice,30`, `erin,30`).
- **`-u`** keeps the **first** of each run of lines that compare equal **under the keys**: `sort -u -k1,1` over `b 1`, `a 2`, `b 3`, `a 4` printed `a 2` and `b 1`. **`-s`** makes the sort **stable** (equal keys keep the input order): `sort -k1,1 -s` gave `a 2`, `a 4`, `b 1`, `b 3`. Without `-s`, `sort` falls back to comparing the whole line as a last resort.
- **`-c`** checks whether the input is sorted; **`-m`** merges sorted files; **`-o FILE`** writes to a file and is **safe when the output is also the input** (section 10); **`-S SIZE`** sets the memory buffer; **`-T DIR`** the temporary directory; **`--parallel=N`** the threads.

### 6.3 `uniq`

`uniq` removes **adjacent** duplicates, so it needs sorted input. Measured on `a a b a`: `uniq` printed `a b a` (the last `a` is not adjacent to the first two), while `sort | uniq -c` printed `3 a` and `1 b`. `-c` prefixes counts, **`-d` prints only the lines that repeat** (`a`), **`-u` only the lines that occur once** (`b`), `-i` ignores case. `sort | uniq -c | sort -rn` is the **frequency table**, the most common shell idiom in this tutorial; the count appears in a right-aligned column of width 7, which is why some outputs below start with spaces.

### 6.4 `join`, `comm`, `paste`, `column`

- **`join FILE1 FILE2`** joins lines that have the **same first field** (change with `-1 N -2 M`), like a database inner join. **Both files must be sorted on the join field, with the same collation `join` uses**, otherwise rows are silently lost. Measured: `join l.txt r.txt` printed `b 2 x` and `c 3 y`; `-a1` added the unpaired line `a 1` (left outer join); `-v1` printed **only** the unpaired `a 1` (anti-join); `-o 1.1,2.2` chose the output fields (`b x`, `c y`). With an **unsorted** left file `join` printed one row, then `join: uns:2: is not sorted: a 2` and `join: input is not in sorted order`, and `--check-order` makes that an error from the start.
- **`comm FILE1 FILE2`** compares two **sorted** files and prints three tab-indented columns: lines only in the first file (column 1), only in the second (column 2) and in both (column 3). The usual use suppresses columns: **`comm -12`** prints lines in both (`b c`), **`comm -23`** lines only in the first (`a`), **`comm -13`** lines only in the second (`d`). With process substitution, `comm -12 <(sort a) <(sort b)` is the set intersection of two lists, and bank item 21 uses it.
- **`paste`** glues files side by side: `paste -d, <(echo a; echo b) <(echo 1; echo 2)` printed `a,1` and `b,2`; **`paste -sd,`** (serial) joins the lines of one file into one line (`a,b,c`), and `paste -sd+ <(seq 1 5) | bc` computed the sum **15** (a way to add numbers without `awk`).
- **`column -t`** aligns columns (`column -t -s, p.csv` printed an aligned table), **`tee FILE`** copies the stream to files and to stdout (`tee o1 o2` wrote two copies), and with process substitution `tee >(cmd)` sends a copy to another command.
- **Also**: `wc -l -c -w`, `head -n N` and `head -n -3` (all but the last 3 lines), `tail -n 2` and `tail -n +4` (from line 4 on), `tac` (reverse lines), `rev` (reverse characters), `nl` (number lines), `expand` (tabs to spaces).

---

## 7. `find` and `xargs`

`find PATH TESTS ACTION` walks a directory tree and evaluates an expression on each entry. The transcript builds a small tree (`src/`, `build/`, `.git/`, files named `two words.c` and `new<newline>line.c`) and runs the usual forms.

```bash
#!/usr/bin/env bash
# e5_find.sh: find, -exec, xargs, and the cost of one process per file
. "$(dirname "$0")/h.sh"
d=$(mktemp -d) && cd "$d" || exit 1
mkdir -p src/lib src/old build .git
for i in 1 2 3; do echo "int f$i;" > src/a$i.c; done
echo "int x;" > src/lib/util.c; echo "// old" > src/old/dead.c; echo '#include "a.h"' > src/a1.h
echo big > build/out.o; head -c 5000 /dev/zero > build/blob.bin; echo ref > ref.txt
touch -d '2026-01-01' src/old/dead.c; touch -d '2026-06-01' src/a1.c
touch 'src/two words.c' 'src/new
line.c'
sleep 0.1; touch src/a2.c
show "find . -name '*.c' -not -path './.git/*' | sort | head -3; find . -type f -name '*.c' | wc -l"
show "find src -maxdepth 1 -type f -name '*.c' | wc -l; find . -type d | sort | tr '\\n' ' '"
show "find . -name '*.c' -newer ref.txt; find . -name '*.c' -mtime +30 | tr '\\n' ' '; find . -size +4k -type f; find . -empty -type f | wc -l"
show "find . \\( -name .git -o -name build \\) -prune -o -type f -name '*.c' -print | sort | head -2"
show "find src -name '*.c' -exec grep -l 'int' {} \; | sort | tr '\\n' ' '"
show "find src -name '*.c' -print0 | xargs -0 grep -c int | sort | head -2"
show "find src -name '*.c' | xargs wc -l 2>&1 | grep -c 'No such file'; find src -name '*.c' -print0 | xargs -0 wc -l 2>&1 | grep -c 'No such file'"
show "find src -name '*.c' -print0 | xargs -0 wc -l | tail -1; find src -name '*.c' -exec wc -l {} + | tail -1"
show "find src -name '*.c' -printf '%s %p\\n' | sort -n | head -2; find src -name '*.c' -printf '%TY-%Tm-%Td %f\\n' | sort | head -2"
show "find . -name '*.c' -delete -print | wc -l; find . -name '*.c' | wc -l"
echo
echo '--- empty input and -P'
show "printf '' | xargs echo run; printf '' | xargs -r echo run; echo status \$?"
show "echo 'a b c' | xargs -n1 echo; echo 'a b c' | xargs -d ' ' -n2 echo; printf 'x y\\n' | xargs -I{} echo '[{}]'"
show "seq 1 4 | xargs -P4 -I{} sh -c 'sleep 0.3; echo done {}' | sort | tr '\\n' ' '"
start=$EPOCHREALTIME; seq 1 8 | xargs -P1 -I{} sleep 0.2; e1=$EPOCHREALTIME; seq 1 8 | xargs -P4 -I{} sleep 0.2; e2=$EPOCHREALTIME
printf 'eight 0.2 s sleeps: -P1 %d ms, -P4 %d ms (2 CPUs, but the work is waiting)\n' $(( (${e1/./} - ${start/./}) / 1000 )) $(( (${e2/./} - ${e1/./}) / 1000 ))
cd / && rm -rf "$d"
```

```text
$ find . -name '*.c' -not -path './.git/*' | sort | head -3; find . -type f -name '*.c' | wc -l
./src/a1.c
./src/a2.c
./src/a3.c
8
$ find src -maxdepth 1 -type f -name '*.c' | wc -l; find . -type d | sort | tr '\n' ' '
6
. ./.git ./build ./src ./src/lib ./src/old $ find . -name '*.c' -newer ref.txt; find . -name '*.c' -mtime +30 | tr '\n' ' '; find . -size +4k -type f; find . -empty -type f | wc -l
./src/a2.c
./src/old/dead.c ./src/a1.c ./build/blob.bin
3
$ find . \( -name .git -o -name build \) -prune -o -type f -name '*.c' -print | sort | head -2
./src/a1.c
./src/a2.c
$ find src -name '*.c' -exec grep -l 'int' {} \; | sort | tr '\n' ' '
src/a1.c src/a2.c src/a3.c src/lib/util.c $ find src -name '*.c' -print0 | xargs -0 grep -c int | sort | head -2
line.c:0
src/a1.c:1
$ find src -name '*.c' | xargs wc -l 2>&1 | grep -c 'No such file'; find src -name '*.c' -print0 | xargs -0 wc -l 2>&1 | grep -c 'No such file'
4
0
$ find src -name '*.c' -print0 | xargs -0 wc -l | tail -1; find src -name '*.c' -exec wc -l {} + | tail -1
 5 total
 5 total
$ find src -name '*.c' -printf '%s %p\n' | sort -n | head -2; find src -name '*.c' -printf '%TY-%Tm-%Td %f\n' | sort | head -2
0 src/new
0 src/two words.c
2026-01-01 dead.c
2026-06-01 a1.c
$ find . -name '*.c' -delete -print | wc -l; find . -name '*.c' | wc -l
8
0

--- empty input and -P
$ printf '' | xargs echo run; printf '' | xargs -r echo run; echo status $?
run
status 0
$ echo 'a b c' | xargs -n1 echo; echo 'a b c' | xargs -d ' ' -n2 echo; printf 'x y\n' | xargs -I{} echo '[{}]'
a
b
c
a b
c

[x y]
$ seq 1 4 | xargs -P4 -I{} sh -c 'sleep 0.3; echo done {}' | sort | tr '\n' ' '
done 1 done 2 done 3 done 4 eight 0.2 s sleeps: -P1 1617 ms, -P4 407 ms (2 CPUs, but the work is waiting)
```

**Tests**: `-name 'GLOB'` (quote it so the shell does not expand it, [LX 01](01_shell_and_scripting.md) section 3; `-iname` ignores case), `-type f|d|l`, `-size +4k` (more than 4 KiB; suffixes `c k M G`), `-mtime +30` (modified **more than** 30 days ago; `-mtime -1` is within a day), `-newer FILE` (modified after FILE), `-empty`, `-perm`, `-user`, `-maxdepth N`. Measured: `-newer ref.txt` listed the three files touched after `ref.txt` was created, `-mtime +30` listed the two files given old dates (`dead.c` and `a1.c`), `-size +4k` found `blob.bin` and `-maxdepth 1 -type f -name '*.c'` printed 6 lines and the whole tree 8, but those are **lines, not files**: the file with a newline in its name takes two lines, so they are 5 and 7 files (`wc -l` on file names miscounts, which is the point of `-print0`). **Combine** tests with `-a` (the default), `-o` and `!` or `-not`, and group with `\( \)`.

**Skip a directory with `-prune`**: `find . \( -name .git -o -name build \) -prune -o -type f -name '*.c' -print` skipped both directories, and the final `-print` is **needed**: without an explicit action `find` adds an implicit `-print` to the **whole** expression, which would also print the pruned directories (from the `find` manual, not run here). A simpler form for one directory is `-not -path './.git/*'`, which still **descends** into it (slower).

**Actions**:

- **`-print`** (default), **`-print0`** (NUL-terminated, for `xargs -0`), **`-printf 'FORMAT'`** (GNU: `%s` size, `%p` path, `%f` name, `%TY-%Tm-%Td` modification date; the transcript printed `2026-01-01 dead.c` and `0 src/new` for the newline name's size), `-ls`.
- **`-exec CMD {} \;`** runs `CMD` once **per file**. **`-exec CMD {} +`** passes **many files per run**, like `xargs`: `-exec wc -l {} +` printed the single total `5 total`. The `\;` form started one process per file; section 9's measurement of a fork-and-exec makes the difference concrete.
- **`-delete`** removes the matches; **put it last**: `find . -name '*.c' -delete -print` printed 8 lines (7 names) and then the second `find` found 0, but `-delete` before the test would delete everything. Run the command **without** `-delete` first.

**`xargs`** reads items from standard input and runs a command with them as arguments, **in batches** (as many as fit the limit on the command line). The things to know:

- **Names with spaces and newlines.** `find ... | xargs wc -l` split `two words.c` at the space and `new<newline>line.c` at the newline, and **4 error lines** (`No such file`) appeared; `find ... -print0 | xargs -0 wc -l` had **0**. The rule: **`-print0` with `-0`, always**, or `-d '\n'` (GNU) when you know the names have no newlines. The default `xargs` also processes quotes and backslashes in its input, which is another way for odd names to go wrong.
- **Empty input.** `printf '' | xargs echo run` **ran `echo` once with no arguments** (printing `run`); **`-r`** (`--no-run-if-empty`, GNU) prevented it. A script that ends in `| xargs rm -rf` on an empty list is a classic accident, and `-r` is the guard.
- **`-n N`** limits the arguments per run (`xargs -n1` ran once per word, 3 lines for `a b c`; `-d ' ' -n2` gave `a b` and `c`), **`-I{}`** puts each item where `{}` appears (`[x y]`; implies one item per run, with the whole line as the item), **`-P N`** runs N processes at once.
- **`-P` and parallel speed.** Eight `sleep 0.2` commands took **1,617 ms** with `-P1` and **408 ms** with `-P4`, on a machine with two CPUs: the commands are waiting, not computing, so the speed-up is not limited by the CPU count. For CPU-bound work the limit is the number of cores; see section 9 for the measured effect of one process per item. Output from parallel jobs can interleave, which is why the transcript sorts it.

---

## 8. `jq` for JSON

Text tools cannot reliably parse JSON (nested braces, quoted commas, escapes, numbers in several forms). **`jq`** is a small functional language over a **stream of JSON values**. It reads JSON from files or standard input (one value per line is the "JSON lines" form that log systems emit) and writes JSON, or raw text with `-r`.

```bash
#!/usr/bin/env bash
# e6_jq.sh: jq on JSON lines: select, map, group, reduce, output formats
. "$(dirname "$0")/h.sh"
cd "$(mktemp -d)" || exit 1
cat > j.jsonl <<'J'
{"svc":"auth","level":"info","ms":12,"user":"u1","tags":["a","b"]}
{"svc":"auth","level":"error","ms":340,"user":"u2","tags":[]}
{"svc":"billing","level":"info","ms":55,"user":"u1","tags":["a"]}
{"svc":"search","level":"warn","ms":900,"user":"u3","tags":["c"],"extra":{"k":1}}
{"svc":"billing","level":"error","ms":70,"user":"u2","tags":["b","c"]}
J
show "jq -c 'select(.level==\"error\")' j.jsonl"
show "jq -r '.svc' j.jsonl | sort | uniq -c | tr -s ' ' | tr '\\n' ';'"
show "jq -r '[.svc,.ms] | @tsv' j.jsonl | head -2; jq -r '[.svc,.ms] | @csv' j.jsonl | head -2"
show "jq -s 'group_by(.svc) | map({svc: .[0].svc, n: length, avg: (map(.ms)|add/length)})' -c j.jsonl"
show "jq -s 'map(.ms) | add, (add/length), max, min' j.jsonl | tr '\\n' ' '"
show "jq -s '[.[].user] | unique' -c j.jsonl; jq -s 'map(select(.level==\"error\").user) | unique' -c j.jsonl"
show "jq -c '{svc, ms}' j.jsonl | head -2; jq -c '.tags | length' j.jsonl | tr '\\n' ' '"
show "jq -c 'select(.tags | index(\"c\")) | .svc' j.jsonl; jq -c 'select(.extra != null) | .extra.k' j.jsonl"
show "jq -n --arg u u2 '\$u'; jq -c --arg u u2 'select(.user==\$u) | .svc' j.jsonl; jq -c --argjson m 100 'select(.ms > \$m) | .ms' j.jsonl"
show "jq -c '.ms |= . + 1 | del(.tags)' j.jsonl | head -1; jq -c 'to_entries | map(.key) | join(\",\")' j.jsonl | head -1"
show "jq -s 'reduce .[] as \$r ({}; .[\$r.svc] += \$r.ms)' -c j.jsonl"
show "echo '{\"a\":1' | jq .; echo \"status \$?\""
show "echo '{\"big\":12345678901234567890,\"f\":1.10}' | jq -c '.'; echo '{\"big\":12345678901234567890}' | jq '.big + 1'"
show "echo '[1,2,3]' | jq 'map(.*2)' -c; echo '\"x\"' | jq -r .; echo '\"x\"' | jq ."
show "echo '{\"a\":null}' | jq '.a, .b, (.a // \"dflt\")' -c"
show "jq -e '.svc==\"auth\"' <<< '{\"svc\":\"auth\"}'; echo \"status \$?\"; jq -e '.svc==\"auth\"' <<< '{\"svc\":\"x\"}'; echo \"status \$?\""
```

```text
$ jq -c 'select(.level=="error")' j.jsonl
{"svc":"auth","level":"error","ms":340,"user":"u2","tags":[]}
{"svc":"billing","level":"error","ms":70,"user":"u2","tags":["b","c"]}
$ jq -r '.svc' j.jsonl | sort | uniq -c | tr -s ' ' | tr '\n' ';'
 2 auth; 2 billing; 1 search;$ jq -r '[.svc,.ms] | @tsv' j.jsonl | head -2; jq -r '[.svc,.ms] | @csv' j.jsonl | head -2
auth    12
auth    340
"auth",12
"auth",340
$ jq -s 'group_by(.svc) | map({svc: .[0].svc, n: length, avg: (map(.ms)|add/length)})' -c j.jsonl
[{"svc":"auth","n":2,"avg":176},{"svc":"billing","n":2,"avg":62.5},{"svc":"search","n":1,"avg":900}]
$ jq -s 'map(.ms) | add, (add/length), max, min' j.jsonl | tr '\n' ' '
1377 275.4 900 12 $ jq -s '[.[].user] | unique' -c j.jsonl; jq -s 'map(select(.level=="error").user) | unique' -c j.jsonl
["u1","u2","u3"]
["u2"]
$ jq -c '{svc, ms}' j.jsonl | head -2; jq -c '.tags | length' j.jsonl | tr '\n' ' '
{"svc":"auth","ms":12}
{"svc":"auth","ms":340}
2 0 1 1 2 $ jq -c 'select(.tags | index("c")) | .svc' j.jsonl; jq -c 'select(.extra != null) | .extra.k' j.jsonl
"search"
"billing"
1
$ jq -n --arg u u2 '$u'; jq -c --arg u u2 'select(.user==$u) | .svc' j.jsonl; jq -c --argjson m 100 'select(.ms > $m) | .ms' j.jsonl
"u2"
"auth"
"billing"
340
900
$ jq -c '.ms |= . + 1 | del(.tags)' j.jsonl | head -1; jq -c 'to_entries | map(.key) | join(",")' j.jsonl | head -1
{"svc":"auth","level":"info","ms":13,"user":"u1"}
"svc,level,ms,user,tags"
$ jq -s 'reduce .[] as $r ({}; .[$r.svc] += $r.ms)' -c j.jsonl
{"auth":352,"billing":125,"search":900}
$ echo '{"a":1' | jq .; echo "status $?"
jq: parse error: Unfinished JSON term at EOF at line 2, column 0
status 5
$ echo '{"big":12345678901234567890,"f":1.10}' | jq -c '.'; echo '{"big":12345678901234567890}' | jq '.big + 1'
{"big":12345678901234567890,"f":1.10}
12345678901234567000
$ echo '[1,2,3]' | jq 'map(.*2)' -c; echo '"x"' | jq -r .; echo '"x"' | jq .
[2,4,6]
x
"x"
$ echo '{"a":null}' | jq '.a, .b, (.a // "dflt")' -c
null
null
"dflt"
$ jq -e '.svc=="auth"' <<< '{"svc":"auth"}'; echo "status $?"; jq -e '.svc=="auth"' <<< '{"svc":"x"}'; echo "status $?"
true
status 0
false
status 1
```

- **Paths and filters**: `.svc` is a field, `.a.b` nests, `.[]` iterates an array, `.tags | length` counts, `select(COND)` keeps values for which the condition is true (`select(.level=="error")` printed the two error records), `{svc, ms}` builds an object (shorthand for `{svc: .svc, ms: .ms}`), `.ms |= . + 1` updates, `del(.tags)` removes a key, `to_entries` turns an object into key/value pairs.
- **Output options**: `-c` compact (one value per line), **`-r`** raw strings (without quotes), **`@csv`** and **`@tsv`** format an array as a row (`"auth",12`), `-e` sets the exit status from the **last output value** (`jq -e` returned 0 for `true` and 1 for `false`), and a **parse error returns 5** (`jq: parse error: Unfinished JSON term`).
- **Slurp and group**: `-s` (**slurp**) reads the **whole input into one array**, which is what `group_by`, `unique`, `add` and `max` need. `jq -s 'group_by(.svc) | map({svc: .[0].svc, n: length, avg: (map(.ms)|add/length)})'` printed `auth` with `n=2`, `avg=176`, `billing` 62.5, `search` 900. **`reduce`** builds a result by folding (`reduce .[] as $r ({}; .[$r.svc] += $r.ms)` gave `{"auth":352,"billing":125,"search":900}`). Slurping a huge file needs memory for the whole file; **streaming** with `-n 'inputs'` or `--stream` avoids that.
- **Passing shell values**: **`--arg name value`** (a string) and **`--argjson name json`** (a number or object) define variables, `$u`: `jq --arg u u2 'select(.user==$u)'`. Never build the filter text from shell variables, for the same reason as `awk -v`.
- **Missing values**: `.b` of an object without `b` is `null`; **`//`** is "default" (`.a // "dflt"` gave `"dflt"` for `null`).
- **Numbers**: `jq` 1.7 **preserves the text** of a number it does not compute with (`12345678901234567890` was printed unchanged by `.`), but a computation converts it to a double: `.big + 1` printed `12345678901234567000`. JSON has no 64-bit integer guarantee, and IDs above 2^53 survive only as strings or untouched.
- **When not to use it**: for a stream of simple `key=value` log lines use `awk`; for deeply restructured output use Python; `jq` is for slicing JSON in a pipeline.

---

## 9. Locale, speed and what each choice costs

Two performance facts decide most real-world slowness in these pipelines: **which locale the tools run in**, and **how many times the data is copied and sorted**. The experiment runs the same jobs over the 600,000-line, 69 MiB `access_big.log` (warm cache, fastest of three runs).

```bash
#!/usr/bin/env bash
# e7_speed.sh: how long the same job takes with different tools and locales (600,000-line web log, 73 MB, warm page cache)
export LOCPATH=/tmp/loc
cd "$(dirname "$0")/../data" || exit 1
B=access_big.log
best() { # best LABEL COMMAND...: run three times, print the fastest wall time in ms
  local label=$1 i s e m=999999999 sink=/tmp/e7_sink.$$; shift
  for i in 1 2 3; do
    s=$EPOCHREALTIME; "$@" > "$sink" 2>&1; e=$EPOCHREALTIME
    m=$(( (${e/./} - ${s/./}) / 1000 < m ? (${e/./} - ${s/./}) / 1000 : m ))
  done
  rm -f "$sink"; printf '%-52s %6d ms\n' "$label" "$m"
}
pipe() { bash -c "$1"; }
echo "file: $(wc -l < $B) lines, $(( $(stat -c %s $B) / 1048576 )) MiB"
echo
echo '--- count the 500 responses (pattern: quote, space, 500, space)'
best 'grep -c  (LC_ALL=C)'              env LC_ALL=C grep -c '" 500 ' $B
best 'grep -c  (en_US.UTF-8)'           env LC_ALL=en_US.UTF-8 grep -c '" 500 ' $B
best 'grep -c  (C.UTF-8)'               env LC_ALL=C.UTF-8 grep -c '" 500 ' $B
best 'grep -F -c (LC_ALL=C)'            env LC_ALL=C grep -F -c '" 500 ' $B
best 'grep -E -c (LC_ALL=C)'            env LC_ALL=C grep -E -c '" 500 ' $B
best 'grep -P -c (LC_ALL=C)'            env LC_ALL=C grep -P -c '" 500 ' $B
best 'grep -c with a leading .* (LC_ALL=C)' env LC_ALL=C grep -c '.*" 500 ' $B
best 'rg -c'                            rg -c '" 500 ' $B
best 'gawk (field 9 == 500)'            gawk '$9 == 500 {n++} END {print n}' $B
best 'mawk (field 9 == 500)'            mawk '$9 == 500 {n++} END {print n}' $B
best 'gawk /regex/'                     gawk '/" 500 / {n++} END {print n}' $B
best 'sed -n /regex/p | wc -l'          pipe "LC_ALL=C sed -n '/\" 500 /p' $B | wc -l"
best 'python3 loop'                     python3 -c "print(sum(1 for l in open('$B') if '\" 500 ' in l))"
best 'wc -l (baseline: read the file)'  wc -l $B
echo
echo '--- the top 10 IPs by request count'
best 'cut|sort|uniq -c|sort -rn (LC_ALL=C)'      pipe "cut -d' ' -f1 $B | LC_ALL=C sort | uniq -c | sort -rn | head"
best 'cut|sort|uniq -c|sort -rn (en_US.UTF-8)'   pipe "cut -d' ' -f1 $B | LC_ALL=en_US.UTF-8 sort | LC_ALL=en_US.UTF-8 uniq -c | sort -rn | head"
best 'same, sort -S 1G --parallel=2 (LC_ALL=C)'  pipe "cut -d' ' -f1 $B | LC_ALL=C sort -S 1G --parallel=2 | uniq -c | sort -rn | head"
best 'gawk array, sort the 3000 keys'            pipe "gawk '{c[\$1]++} END{for(i in c) print c[i], i}' $B | sort -rn | head"
best 'mawk array, sort the 3000 keys'            pipe "mawk '{c[\$1]++} END{for(i in c) print c[i], i}' $B | sort -rn | head"
echo
echo '--- print the first field of every line (output to a file)'
best 'cut -d" " -f1'          cut -d' ' -f1 $B
best 'awk (gawk) print $1'    gawk '{print $1}' $B
best 'mawk print $1'          mawk '{print $1}' $B
best "sed 's/ .*//'"          sed 's/ .*//' $B
best "LC_ALL=C sed 's/ .*//'" env LC_ALL=C sed 's/ .*//' $B
best "grep -o '^[^ ]*'"       grep -o '^[^ ]*' $B
best 'python3'                python3 -c "import sys
for l in open('$B'): sys.stdout.write(l.split(' ',1)[0]+'\n')"
echo
echo '--- sorting 600,000 lines'
best 'sort (LC_ALL=C)'                 env LC_ALL=C sort $B
best 'sort (en_US.UTF-8)'              env LC_ALL=en_US.UTF-8 sort $B
best 'sort -k10,10n (LC_ALL=C)'        env LC_ALL=C sort -k10,10n $B
best 'sort -u -k1,1 (LC_ALL=C)'        env LC_ALL=C sort -u -k1,1 $B
best 'sort | uniq | wc -l vs awk !seen' pipe "LC_ALL=C sort $B | uniq | wc -l"
best 'awk !seen[$0]++'                 gawk '!seen[$0]++' $B
echo
echo '--- a benchmarking trap: GNU grep with stdout on /dev/null'
t() { local s e; s=$EPOCHREALTIME; "$@"; e=$EPOCHREALTIME; echo $(( (${e/./} - ${s/./}) / 1000 )); }
printf 'grep PATTERN big > /dev/null (stdout is /dev/null)      %4d ms\n' "$(t sh -c "grep '\" 500 ' $B > /dev/null")"
printf 'the same command, stdout to a file                      %4d ms\n' "$(t sh -c "grep '\" 500 ' $B > /tmp/e7_x.$$; rm -f /tmp/e7_x.$$")"
printf 'the same command, stdout to a pipe (| wc -l)            %4d ms\n' "$(t sh -c "grep '\" 500 ' $B | wc -l > /dev/null")"
```

```text
file: 600000 lines, 69 MiB

--- count the 500 responses (pattern: quote, space, 500, space)
grep -c  (LC_ALL=C)                                      88 ms
grep -c  (en_US.UTF-8)                                   79 ms
grep -c  (C.UTF-8)                                       79 ms
grep -F -c (LC_ALL=C)                                    86 ms
grep -E -c (LC_ALL=C)                                    77 ms
grep -P -c (LC_ALL=C)                                    64 ms
grep -c with a leading .* (LC_ALL=C)                     83 ms
rg -c                                                    14 ms
gawk (field 9 == 500)                                   209 ms
mawk (field 9 == 500)                                   194 ms
gawk /regex/                                            156 ms
sed -n /regex/p | wc -l                                 120 ms
python3 loop                                            112 ms
wc -l (baseline: read the file)                          11 ms

--- the top 10 IPs by request count
cut|sort|uniq -c|sort -rn (LC_ALL=C)                    258 ms
cut|sort|uniq -c|sort -rn (en_US.UTF-8)                 838 ms
same, sort -S 1G --parallel=2 (LC_ALL=C)                301 ms
gawk array, sort the 3000 keys                          152 ms
mawk array, sort the 3000 keys                          207 ms

--- print the first field of every line (output to a file)
cut -d" " -f1                                           144 ms
awk (gawk) print $1                                     116 ms
mawk print $1                                           169 ms
sed 's/ .*//'                                           355 ms
LC_ALL=C sed 's/ .*//'                                  356 ms
grep -o '^[^ ]*'                                       1194 ms
python3                                                 407 ms

--- sorting 600,000 lines
sort (LC_ALL=C)                                         266 ms
sort (en_US.UTF-8)                                     1300 ms
sort -k10,10n (LC_ALL=C)                                439 ms
sort -u -k1,1 (LC_ALL=C)                                236 ms
sort | uniq | wc -l vs awk !seen                        371 ms
awk !seen[$0]++                                         739 ms

--- a benchmarking trap: GNU grep with stdout on /dev/null
grep PATTERN big > /dev/null (stdout is /dev/null)         2 ms
the same command, stdout to a file                        92 ms
the same command, stdout to a pipe (| wc -l)              96 ms
```

### 9.1 The locale: correctness and cost

The environment variable `LC_ALL` (or `LC_COLLATE`, `LC_CTYPE`, `LANG`, in decreasing order of priority: `LC_ALL` overrides everything) selects how text is **ordered and classified**. In the **C locale** (`LC_ALL=C`) every byte is a character and strings compare by byte value. In a UTF-8 locale such as `en_US.UTF-8`, `sort` uses the **language's collation rules**:

- **The order differs.** `sort` of `b A a B` gave **`A B a b`** in `C` and **`a A b B`** in `en_US.UTF-8`; and for `a b`, `ab`, `a c` the C order was `a b;a c;ab` while `en_US.UTF-8` gave `a b;ab;a c` (it ignores spaces and punctuation at the first comparison level). `join` and `comm` need both files sorted **in the collation they will use**, which is why mixing locales silently drops rows.
- **The cost.** `sort` of the 600,000 lines took **266 ms** in `C` and **1,300 ms** in `en_US.UTF-8` (about 5 times); the top-IPs pipeline took **258 ms** against **838 ms** (about 3 times). Collation compares multi-level keys with locale tables. `grep` for a fixed pattern showed **no difference** between the locales (79 to 88 ms) in this test.
- **The rule.** For **machine-processed** logs, put `LC_ALL=C` in front of `sort`, `uniq`, `join`, `comm` and `grep`: faster, deterministic, and byte-exact on any machine (the bank in section 11 sets it once at the top for that reason). For **human-readable** sorted output of names in a language, use the user's locale. In `C`, `grep`, `sed` and `awk` treat a multi-byte UTF-8 character as several characters (`length("héllo")` was 6 in `gawk` under `C` and 5 under `C.utf8`).

### 9.2 What each pipeline costs

All numbers are the fastest of three runs; read them as ranges (10 to 30 percent between runs).

| Job (600,000 lines, 69 MiB) | Tool | Time |
| --- | --- | --- |
| read the file (baseline) | `wc -l` | 11 ms |
| count lines containing `" 500 ` | `rg -c` | 14 ms |
| | `grep -P -c` | 64 ms |
| | `grep -c`, `-F -c`, `-E -c` (C or UTF-8 locale) | 77 to 88 ms |
| | `python3` loop | 112 ms |
| | `sed -n '/re/p' \| wc -l` | 120 ms |
| | `gawk '/re/'` | 156 ms |
| | `mawk '$9 == 500'`, `gawk '$9 == 500'` | 194, 209 ms |
| top 10 IPs | `gawk` array, then `sort` the 3,000 keys | 152 ms |
| | `mawk` array, then `sort` | 207 ms |
| | `cut \| sort \| uniq -c \| sort -rn` (C locale) | 258 ms |
| | the same with `sort -S 1G --parallel=2` | 301 ms |
| | the same in `en_US.UTF-8` | 838 ms |
| first field of every line | `gawk '{print $1}'` | 116 ms |
| | `cut -d' ' -f1` | 144 ms |
| | `mawk '{print $1}'` | 169 ms |
| | `sed 's/ .*//'` | 355 ms |
| | `python3` | 407 ms |
| | `grep -o '^[^ ]*'` | **1,194 ms** |
| sort 600,000 lines | `sort` (C) | 266 ms |
| | `sort -u -k1,1` (C) | 236 ms |
| | `sort -k10,10n` (C) | 439 ms |
| | `sort` (`en_US.UTF-8`) | 1,300 ms |
| remove duplicates | `sort \| uniq \| wc -l` | 371 ms |
| | `awk '!seen[$0]++'` | 739 ms |

What the table says:

- **A dedicated search tool beats a general one by a lot.** `grep -c` is about 2 times faster than `gawk '$9 == 500'`, `rg` about 6 times faster than `grep`, and **`grep -o` as a column extractor is slow** (1,194 ms for `-o '^[^ ]*'`, because it reports each match separately); use `cut` or `awk` to take a column (`gawk` was fastest here, 116 ms, ahead of `cut` at 144 ms).
- **A hash table beats sort-then-count when the keys are few.** The `awk` array took 152 ms against 258 ms for `sort | uniq -c` because it avoids sorting 600,000 lines (it only sorts 3,000 keys afterwards). With **millions of distinct keys** the array needs memory proportional to the keys, and `sort` (which spills to disk) wins on memory; the break-even is memory, not time.
- **`sort -u` and the pipeline were faster than the `awk` set for removing duplicates** (371 ms against 739 ms), but `awk '!seen[$0]++'` **preserves the first-seen order** and starts printing at once; `sort` cannot print until it has read everything.
- **Extra processes cost.** A pipeline of five tools starts five processes, but each streams, so the time is dominated by the **slowest stage**, not the sum; the cost shows when a stage has to wait for all input (`sort`).
- **`sort --parallel=2 -S 1G` did not help here** (301 ms against 258 ms: within the run-to-run spread, or slightly worse), so do not assume more threads or memory make a sort on two CPUs faster; measure.
- **Benchmark traps.** The most important one in this whole experiment: **GNU `grep` with standard output on `/dev/null` stops at the first match** (the transcript's last block: **2 ms** for `/dev/null`, **92 ms** into a file and **96 ms** through a pipe). A benchmark that redirects to `/dev/null` measures nothing. Redirect to a file or a pipe, warm the cache with a first run, and take the fastest of several.

---

## 10. The mistakes that cost an afternoon

Each item below was run; the transcript is the evidence.

```bash
#!/usr/bin/env bash
# e8_pitfalls.sh: the mistakes that cost people an afternoon
. "$(dirname "$0")/h.sh"
export LOCPATH=/tmp/loc
cd "$(mktemp -d)" || exit 1
show "printf 'b\\na\\nb\\na\\n' | uniq -c | tr -s ' ' | tr '\\n' ';'; echo; printf 'b\\na\\nb\\na\\n' | sort | uniq -c | tr -s ' ' | tr '\\n' ';'"
show "printf '10\\n9\\nx\\n2\\n' | sort -n | tr '\\n' ' '; printf '1.5\\n1,5\\n' | sort -n | tr '\\n' ' '"
printf 'a  b   c\n' > sp.txt
show "cut -d' ' -f2 sp.txt | od -c | head -1; awk '{print \$2}' sp.txt"
printf 'x\r\ny\r\n' > crlf.txt
show "grep -c 'x\$' crlf.txt; grep -c \$'x\\r\$' crlf.txt; awk '\$1 == \"x\"' crlf.txt | wc -l; tr -d '\\r' < crlf.txt | grep -c 'x\$'"
show "echo 'a.b' | grep 'a.b'; echo 'axb' | grep 'a.b'; echo 'axb' | grep -F 'a.b'; echo 'axb' | grep 'a\\.b'; echo \"status \$?\""
show "echo 'file[1].txt' | grep 'file[1].txt'; echo 'file[1].txt' | grep -F 'file[1].txt'; echo '3+4' | grep -E '3+4'; echo '3+4' | grep -F '3+4'"
show "printf 'one\\ntwo\\n' | sed -n '/one/,/nomatch/p' | wc -l; printf 'one\\ntwo\\n' | sed -n '/two/,/one/p' | wc -l"
show "echo 'a/b' | sed 's/a/b/' ; v='a/b'; echo 'xay' | sed \"s/a/\$v/\" 2>&1 | head -1"
show "x='1 2'; echo \"\$x\" | awk '{print \$2}'; x='\"; system(\"echo INJECTED\"); \"'; echo hi | awk \"{print \\\"\$x\\\"}\" 2>&1 | head -2"
show "echo hi | awk -v p='\"; system(\"echo SAFE\"); \"' '{print p}'"
show "awk 'BEGIN{print length()}' </dev/null; echo '5 apples' | awk '{if (\$1 > 10) print \"big\"; else print \"small\"}'; echo '5 apples' | awk '{if (\$1 > \"10\") print \"string compare says big\"}'"
show "printf 'a b\\n' | awk '{ print \$1 \$2 }'; printf 'a b\\n' | awk '{ print \$1, \$2 }'; printf '1 2\\n' | awk '{ print \$1 + \$2, \$1 \$2, \$1 -\$2 }'"
printf 'k1 v1\nk2 v2\n' > m.txt; printf 'k1 w1\nk2 w2\n' > m2.txt
show "grep -rn 'k1' . --include='*.txt' | sort | head -3; grep -c k1 m.txt m2.txt | tr '\\n' ' '"
show "ls | head -2; echo > '-n'; ls -l -- -n | sed 's/.* -n/-n/'; grep k1 -- m.txt | head -1"
echo
echo '--- pipelines under set -e and pipefail: grep returns 1 when nothing matches'
show "bash -c 'set -e; echo a | grep b; echo reached'; echo \"status \$?\""
show "bash -c 'set -eo pipefail; echo a | grep b | wc -l; echo reached'; echo \"status \$?\""
show "bash -c 'set -eo pipefail; n=\$(echo a | grep -c b); echo \"count \$n\"'; echo \"status \$?\""
show "bash -c 'set -eo pipefail; echo a | grep b || true; echo reached'; echo \"status \$?\""
show "bash -c 'set -eo pipefail; yes | head -3 | wc -l; echo reached'; echo \"status \$?\""
echo
echo '--- in-place editing a file that is also being read'
printf 'a\nb\n' > f.txt
show "sort f.txt > f.txt; wc -c < f.txt"
printf 'a\nb\n' > f.txt
show "sort -o f.txt f.txt; wc -c < f.txt"
printf 'a\nb\n' > f.txt
show "sort f.txt > f.tmp && mv f.tmp f.txt; wc -c < f.txt"
```

```text
$ printf 'b\na\nb\na\n' | uniq -c | tr -s ' ' | tr '\n' ';'; echo; printf 'b\na\nb\na\n' | sort | uniq -c | tr -s ' ' | tr '\n' ';'
 1 b; 1 a; 1 b; 1 a;
 2 a; 2 b;$ printf '10\n9\nx\n2\n' | sort -n | tr '\n' ' '; printf '1.5\n1,5\n' | sort -n | tr '\n' ' '
x 2 9 10 1,5 1.5 $ cut -d' ' -f2 sp.txt | od -c | head -1; awk '{print $2}' sp.txt
0000000  \n
b
$ grep -c 'x$' crlf.txt; grep -c $'x\r$' crlf.txt; awk '$1 == "x"' crlf.txt | wc -l; tr -d '\r' < crlf.txt | grep -c 'x$'
0
1
0
1
$ echo 'a.b' | grep 'a.b'; echo 'axb' | grep 'a.b'; echo 'axb' | grep -F 'a.b'; echo 'axb' | grep 'a\.b'; echo "status $?"
a.b
axb
status 1
$ echo 'file[1].txt' | grep 'file[1].txt'; echo 'file[1].txt' | grep -F 'file[1].txt'; echo '3+4' | grep -E '3+4'; echo '3+4' | grep -F '3+4'
file[1].txt
3+4
$ printf 'one\ntwo\n' | sed -n '/one/,/nomatch/p' | wc -l; printf 'one\ntwo\n' | sed -n '/two/,/one/p' | wc -l
2
1
$ echo 'a/b' | sed 's/a/b/' ; v='a/b'; echo 'xay' | sed "s/a/$v/" 2>&1 | head -1
b/b
sed: -e expression #1, char 7: unknown option to `s'
$ x='1 2'; echo "$x" | awk '{print $2}'; x='"; system("echo INJECTED"); "'; echo hi | awk "{print \"$x\"}" 2>&1 | head -2
2

INJECTED
$ echo hi | awk -v p='"; system("echo SAFE"); "' '{print p}'
"; system("echo SAFE"); "
$ awk 'BEGIN{print length()}' </dev/null; echo '5 apples' | awk '{if ($1 > 10) print "big"; else print "small"}'; echo '5 apples' | awk '{if ($1 > "10") print "string compare says big"}'
0
small
string compare says big
$ printf 'a b\n' | awk '{ print $1 $2 }'; printf 'a b\n' | awk '{ print $1, $2 }'; printf '1 2\n' | awk '{ print $1 + $2, $1 $2, $1 -$2 }'
ab
a b
3 12 -1
$ grep -rn 'k1' . --include='*.txt' | sort | head -3; grep -c k1 m.txt m2.txt | tr '\n' ' '
./m.txt:1:k1 v1
./m2.txt:1:k1 w1
m.txt:1 m2.txt:1 $ ls | head -2; echo > '-n'; ls -l -- -n | sed 's/.* -n/-n/'; grep k1 -- m.txt | head -1
crlf.txt
m.txt
-n
k1 v1

--- pipelines under set -e and pipefail: grep returns 1 when nothing matches
$ bash -c 'set -e; echo a | grep b; echo reached'; echo "status $?"
status 1
$ bash -c 'set -eo pipefail; echo a | grep b | wc -l; echo reached'; echo "status $?"
0
status 1
$ bash -c 'set -eo pipefail; n=$(echo a | grep -c b); echo "count $n"'; echo "status $?"
status 1
$ bash -c 'set -eo pipefail; echo a | grep b || true; echo reached'; echo "status $?"
reached
status 0
$ bash -c 'set -eo pipefail; yes | head -3 | wc -l; echo reached'; echo "status $?"
3
status 141

--- in-place editing a file that is also being read
$ sort f.txt > f.txt; wc -c < f.txt
0
$ sort -o f.txt f.txt; wc -c < f.txt
4
$ sort f.txt > f.tmp && mv f.tmp f.txt; wc -c < f.txt
4
```

- **`uniq` without `sort`.** `b a b a` through `uniq -c` gave four counts of 1; through `sort | uniq -c` gave `2 a` and `2 b`.
- **`sort -n` on non-numbers.** A line that does not start with a number counts as 0: `x` sorted first (`x 2 9 10`). With a decimal comma (`1,5`) the C locale reads `1` and stops at the comma, so `1,5` sorted before `1.5`.
- **`cut -d' '` on runs of spaces.** `a  b   c` (two, then three spaces) printed an empty second field; `awk '{print $2}'` printed `b`.
- **Carriage returns.** A file with Windows line ends (CRLF): `grep -c 'x$'` found **0** of the two `x` lines, because each line ends in `\r` before the newline; `grep -c $'x\r$'` found 1, `awk '$1 == "x"'` matched **0** lines, and `tr -d '\r'` first made everything work. Suspect this whenever "the text looks right and nothing matches" and the file came from Windows.
- **Unescaped metacharacters.** `grep 'a.b'` matched `axb`; `grep 'file[1].txt'` did **not match its own text** (the brackets are a class), while `-F` matched; `grep -E '3+4'` did not match `3+4` (it means "one or more 3, then 4"), while `-F` did. Use **`-F`** for literal text.
- **A `sed` range with no end.** `sed -n '/one/,/nomatch/p'` printed **both** lines (the range ran to the end of input); `/two/,/one/p` printed only `two` (the end pattern `one` had already passed). The manual (fetched) says the end address is first checked on the line **after** the start.
- **A `/` in a variable inside `s///`.** `v='a/b'; sed "s/a/$v/"` failed with `unknown option to 's'`. Use another delimiter (`s#a#$v#`) and check the variable cannot contain it.
- **Code injection through `awk`.** With `x='"; system("echo INJECTED"); "'` and `awk "{print \"$x\"}"` the shell built a program that **ran `echo INJECTED`**; with `awk -v p="$x" '{print p}'` the same text was printed as data. **Pass values with `-v` or `ENVIRON`, never by splicing.**
- **awk's number or string choice.** `5 apples`: `$1 > 10` was false (numeric) and `$1 > "10"` was true (string). `$1 $2` concatenated and `$1 -$2` subtracted (section 5).
- **File names that look like options.** A file named `-n`: `ls -l -- -n` and `grep k1 -- m.txt` use `--` to end the options ([LX 01](01_shell_and_scripting.md) section 3).
- **`grep` under `set -e` and `pipefail`.** `grep` exits **1 when nothing matches**, and that is not an error. `bash -c 'set -e; echo a | grep b; echo reached'` stopped with status **1** and no output. `set -eo pipefail; echo a | grep b | wc -l` printed `0` and then stopped with status **1**. `n=$(echo a | grep -c b)` **killed the script silently** (`grep -c` printed `0` and returned 1). The fixes: `grep ... || true`, or `if grep -q ...`, or `{ grep b || true; }` in the middle of a pipeline. Separately, `yes | head -3 | wc -l` under `pipefail` printed 3 and **stopped with status 141** (`yes` was killed by `SIGPIPE`, 128 + 13): `head` closing early is normal, and `pipefail` reports it ([LX 01](01_shell_and_scripting.md) section 5).
- **Reading and writing the same file.** `sort f.txt > f.txt` left a **0-byte** file, because the shell truncates `f.txt` before `sort` starts reading. `sort -o f.txt f.txt` is safe (4 bytes); so is writing to a temporary file and `mv` (the atomic pattern from [LX 01](01_shell_and_scripting.md) section 9). `sponge` from the `moreutils` package does the same (not installed here).

---

## 11. A bank of 30 one-liners

These are the questions an interviewer or an on-call engineer asks of a log. The bank runs each command over the generated logs and **compares the output with an independent computation in Python**, byte for byte. The commands are written for `LC_ALL=C` (set once at the top) so that `sort` and `uniq` give the same answer everywhere, and ties are broken explicitly (`sort -k1,1nr -k2,2`) so the result is deterministic. The output shows the command, the first four lines of its result and whether it matched the Python result.

```bash
#!/usr/bin/env bash
# bank.sh: thirty one-liners over a web log, an auth log, a syslog and JSON lines.
# Each one is run and its output is compared byte for byte with a Python computation of the same question.
export LC_ALL=C                      # byte order, so sort and uniq give the same answer on every machine
cd "$(dirname "$0")/.." || exit 1
cd data || exit 1
ok=0; bad=0
q() { # q NUMBER 'question' 'command'
  local n; n=$(printf %02d "$1") ; local got; got=$(bash -c "$3" 2>&1)
  printf '\n[%s] %s\n$ %s\n' "$n" "$2" "$3"
  printf '%s\n' "$got" | head -4
  if [[ $got == "$(cat ../exp/$n.txt)" ]]; then ok=$((ok + 1)); echo "  (matches the Python result, $(printf '%s\n' "$got" | wc -l) lines)"; else bad=$((bad + 1)); echo '  *** DIFFERS from the Python result'; diff <(printf '%s\n' "$got") ../exp/$n.txt | head -4; fi
}
echo "access.log: $(wc -l < access.log) lines   auth.log: $(wc -l < auth.log)   syslog: $(wc -l < syslog)   app.jsonl: $(wc -l < app.jsonl)"
echo
echo '=== web access log (combined format plus the request time as the last field)'
q 1 'top 5 client IPs by requests' "awk '{print \$1}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5"
q 2 'requests per status code' "awk '{c[\$9]++} END {for (s in c) print s, c[s]}' access.log | sort"
q 3 'share of 5xx responses' "awk '\$9 ~ /^5/ {e++} END {printf \"%.2f%%\\n\", 100 * e / NR}' access.log"
q 4 'the 5 busiest minutes' "awk '{print substr(\$4, 2, 17)}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5"
q 5 'the 5 slowest endpoints by mean request time (ids and queries folded)' "awk '{p = \$7; sub(/\\?.*/, \"\", p); gsub(/\\/[0-9]+/, \"/N\", p); s[p] += \$NF; n[p]++} END {for (p in s) printf \"%.3f %d %s\\n\", s[p] / n[p], n[p], p}' access.log | sort -k1,1nr -k3,3 | head -5"
q 6 '95th percentile request time (nearest rank)' "awk '{print \$NF}' access.log | sort -n | awk '{a[NR] = \$1} END {i = int(0.95 * NR); if (i < 0.95 * NR) i++; print a[i]}'"
q 7 'the 3 clients that received the most bytes' "awk '{b[\$1] += \$10} END {for (i in b) print b[i], i}' access.log | sort -k1,1nr -k2,2 | head -3"
q 8 'distinct client IPs' "cut -d' ' -f1 access.log | sort -u | wc -l"
q 9 'the 5 paths that return 404 most often' "awk '\$9 == 404 {p = \$7; sub(/\\?.*/, \"\", p); gsub(/\\/[0-9]+/, \"/N\", p); print p}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5"
q 10 'requests per hour' "awk '{print substr(\$4, 14, 2)}' access.log | sort | uniq -c"
q 11 'the 3 most common user agents' "awk -F'\"' '{print \$6}' access.log | sort | uniq -c | sort -k1,1nr -k2 | head -3"
q 12 'POST requests' "grep -c '\"POST ' access.log"
q 13 'requests per HTTP method' "awk '{print substr(\$6, 2)}' access.log | sort | uniq -c"
q 14 'the 3 clients with the most requests slower than 1 s' "awk '\$NF > 1 {print \$1}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -3"
q 15 'the largest response: client, path, bytes' "sort -k10,10nr access.log | head -1 | cut -d' ' -f1,7,10"
echo
echo '=== authentication log'
q 16 'top 5 source IPs of failed passwords' "grep 'Failed password' auth.log | grep -oE 'from [0-9.]+' | cut -d' ' -f2 | sort | uniq -c | sort -k1,1nr -k2,2 | head -5"
q 17 'top 5 invalid user names tried' "grep -oP 'invalid user \\K\\S+' auth.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5"
q 18 'successful logins per user' "awk '/Accepted/ {c[\$9]++} END {for (u in c) print u, c[u]}' auth.log | sort"
q 19 'failed passwords per hour' "awk '/Failed password/ {print substr(\$3, 1, 2)}' auth.log | sort | uniq -c"
q 20 'sessions: count, mean and longest duration (pair open and close by process id)' "awk 'function t(s,  a) {split(s, a, \":\"); return a[1] * 3600 + a[2] * 60 + a[3]} /session opened/ {o[\$5] = t(\$3)} /session closed/ {d = t(\$3) - o[\$5]; s += d; n++; if (d > m) m = d} END {printf \"%d sessions, mean %.1f s, longest %d s\\n\", n, s / n, m}' auth.log"
q 21 'IPs with 10 or more failures that also logged in successfully' "comm -12 <(grep 'Failed password' auth.log | grep -oE 'from [0-9.]+' | cut -d' ' -f2 | sort | uniq -c | awk '\$1 >= 10 {print \$2}' | sort) <(grep Accepted auth.log | grep -oE 'from [0-9.]+' | cut -d' ' -f2 | sort -u)"
echo
echo '=== syslog'
q 22 'messages per program' "awk '{p = \$5; sub(/\\[.*/, \"\", p); sub(/:\$/, \"\", p); c[p]++} END {for (p in c) print c[p], p}' syslog | sort -k1,1nr -k2,2"
q 23 'lines mentioning error, per hour (first 3 hours)' "grep -i error syslog | awk '{print substr(\$3, 1, 2)}' | sort | uniq -c | head -3"
q 24 'job times from the messages "done in N ms"' "awk '/done in/ {n++; s += \$(NF - 1); if (\$(NF - 1) > m) m = \$(NF - 1)} END {printf \"%d jobs, mean %.2f ms, max %d ms\\n\", n, s / n, m}' syslog"
q 25 'the busiest minute of the day' "awk '{print substr(\$3, 1, 5)}' syslog | sort | uniq -c | sort -k1,1nr -k2,2 | head -1"
echo
echo '=== JSON lines'
q 26 'records per level' "jq -r .level app.jsonl | sort | uniq -c"
q 27 'mean ms per service, to 2 decimals' "jq -s -r 'group_by(.svc)[] | \"\\(.[0].svc) \\(((map(.ms) | add) / length * 100 | round) / 100)\"' app.jsonl"
q 28 'distinct users that have an error record' "jq -r 'select(.level == \"error\") | .user' app.jsonl | sort -u | wc -l"
echo
echo '=== joins'
q 29 'top 5 IPs with their country (join against a sorted table)' "awk '{print \$1}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5 | awk '{print \$2, \$1}' | sort | join - country.txt | sort -k2,2nr -k1,1"
q 30 'requests per country' "awk '{c[\$1]++} END {for (i in c) print i, c[i]}' access.log | sort | join - country.txt | awk '{t[\$3] += \$2} END {for (k in t) print k, t[k]}' | sort"
echo
echo "checked against Python: $ok match, $bad differ"
```

```text
access.log: 200000 lines   auth.log: 9900   syslog: 50000   app.jsonl: 5000

=== web access log (combined format plus the request time as the last field)

[01] top 5 client IPs by requests
$ awk '{print $1}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5
  15579 10.0.24.99
   8329 10.0.60.7
   5980 10.0.75.170
   4400 10.1.11.251
  (matches the Python result, 5 lines)

[02] requests per status code
$ awk '{c[$9]++} END {for (s in c) print s, c[s]}' access.log | sort
200 174115
301 4671
403 3008
404 10721
  (matches the Python result, 7 lines)

[03] share of 5xx responses
$ awk '$9 ~ /^5/ {e++} END {printf "%.2f%%\n", 100 * e / NR}' access.log
3.74%
  (matches the Python result, 1 lines)

[04] the 5 busiest minutes
$ awk '{print substr($4, 2, 17)}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5
    630 14/Nov/2026:21:17
    624 14/Nov/2026:23:36
    619 14/Nov/2026:20:59
    619 14/Nov/2026:22:15
  (matches the Python result, 5 lines)

[05] the 5 slowest endpoints by mean request time (ids and queries folded)
$ awk '{p = $7; sub(/\?.*/, "", p); gsub(/\/[0-9]+/, "/N", p); s[p] += $NF; n[p]++} END {for (p in s) printf "%.3f %d %s\n", s[p] / n[p], n[p], p}' access.log | sort -k1,1nr -k3,3 | head -5
1.402 3254 /api/reports/N
0.575 9406 /api/search
0.384 6352 /checkout
0.252 16057 /api/orders/N/items
  (matches the Python result, 5 lines)

[06] 95th percentile request time (nearest rank)
$ awk '{print $NF}' access.log | sort -n | awk '{a[NR] = $1} END {i = int(0.95 * NR); if (i < 0.95 * NR) i++; print a[i]}'
0.504
  (matches the Python result, 1 lines)

[07] the 3 clients that received the most bytes
$ awk '{b[$1] += $10} END {for (i in b) print b[i], i}' access.log | sort -k1,1nr -k2,2 | head -3
22470757 10.0.24.99
11834122 10.0.60.7
8403075 10.0.75.170
  (matches the Python result, 3 lines)

[08] distinct client IPs
$ cut -d' ' -f1 access.log | sort -u | wc -l
3000
  (matches the Python result, 1 lines)

[09] the 5 paths that return 404 most often
$ awk '$9 == 404 {p = $7; sub(/\?.*/, "", p); gsub(/\/[0-9]+/, "/N", p); print p}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5
   2331 /api/users/N
   2058 /static/app.js
   1576 /
   1397 /static/logo.png
  (matches the Python result, 5 lines)

[10] requests per hour
$ awk '{print substr($4, 14, 2)}' access.log | sort | uniq -c
  33287 18
  33312 19
  33472 20
  33590 21
  (matches the Python result, 6 lines)

[11] the 3 most common user agents
$ awk -F'"' '{print $6}' access.log | sort | uniq -c | sort -k1,1nr -k2 | head -3
  40264 python-requests/2.31
  40124 Googlebot/2.1
  40084 Mozilla/5.0 (Windows NT 10.0) Chrome/119.0
  (matches the Python result, 3 lines)

[12] POST requests
$ grep -c '"POST ' access.log
24122
  (matches the Python result, 1 lines)

[13] requests per HTTP method
$ awk '{print substr($6, 2)}' access.log | sort | uniq -c
 175878 GET
  24122 POST
  (matches the Python result, 2 lines)

[14] the 3 clients with the most requests slower than 1 s
$ awk '$NF > 1 {print $1}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -3
    241 10.0.24.99
    133 10.0.60.7
     87 10.0.75.170
  (matches the Python result, 3 lines)

[15] the largest response: client, path, bytes
$ sort -k10,10nr access.log | head -1 | cut -d' ' -f1,7,10
10.0.75.170 /static/app.js 159264
  (matches the Python result, 1 lines)

=== authentication log

[16] top 5 source IPs of failed passwords
$ grep 'Failed password' auth.log | grep -oE 'from [0-9.]+' | cut -d' ' -f2 | sort | uniq -c | sort -k1,1nr -k2,2 | head -5
    653 10.103.81.192
    636 10.103.159.161
    614 10.102.252.92
    607 10.108.14.65
  (matches the Python result, 5 lines)

[17] top 5 invalid user names tried
$ grep -oP 'invalid user \K\S+' auth.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5
    834 oracle
    827 pi
    812 root
    790 admin
  (matches the Python result, 5 lines)

[18] successful logins per user
$ awk '/Accepted/ {c[$9]++} END {for (u in c) print u, c[u]}' auth.log | sort
alice 60
bob 51
carol 55
dave 66
  (matches the Python result, 5 lines)

[19] failed passwords per hour
$ awk '/Failed password/ {print substr($3, 1, 2)}' auth.log | sort | uniq -c
    358 00
    379 01
    358 02
    387 03
  (matches the Python result, 24 lines)

[20] sessions: count, mean and longest duration (pair open and close by process id)
$ awk 'function t(s,  a) {split(s, a, ":"); return a[1] * 3600 + a[2] * 60 + a[3]} /session opened/ {o[$5] = t($3)} /session closed/ {d = t($3) - o[$5]; s += d; n++; if (d > m) m = d} END {printf "%d sessions, mean %.1f s, longest %d s\n", n, s / n, m}' auth.log
300 sessions, mean 3678.4 s, longest 7172 s
  (matches the Python result, 1 lines)

[21] IPs with 10 or more failures that also logged in successfully
$ comm -12 <(grep 'Failed password' auth.log | grep -oE 'from [0-9.]+' | cut -d' ' -f2 | sort | uniq -c | awk '$1 >= 10 {print $2}' | sort) <(grep Accepted auth.log | grep -oE 'from [0-9.]+' | cut -d' ' -f2 | sort -u)
10.0.166.193
10.0.5.138
10.0.64.60
10.101.224.64
  (matches the Python result, 15 lines)

=== syslog

[22] messages per program
$ awk '{p = $5; sub(/\[.*/, "", p); sub(/:$/, "", p); c[p]++} END {for (p in c) print c[p], p}' syslog | sort -k1,1nr -k2,2
14850 systemd
10173 nginx
9914 app-worker
7454 cron
  (matches the Python result, 6 lines)

[23] lines mentioning error, per hour (first 3 hours)
$ grep -i error syslog | awk '{print substr($3, 1, 2)}' | sort | uniq -c | head -3
    324 00
    324 01
    287 02
  (matches the Python result, 3 lines)

[24] job times from the messages "done in N ms"
$ awk '/done in/ {n++; s += $(NF - 1); if ($(NF - 1) > m) m = $(NF - 1)} END {printf "%d jobs, mean %.2f ms, max %d ms\n", n, s / n, m}' syslog
4899 jobs, mean 67.73 ms, max 120 ms
  (matches the Python result, 1 lines)

[25] the busiest minute of the day
$ awk '{print substr($3, 1, 5)}' syslog | sort | uniq -c | sort -k1,1nr -k2,2 | head -1
     56 03:14
  (matches the Python result, 1 lines)

=== JSON lines

[26] records per level
$ jq -r .level app.jsonl | sort | uniq -c
    246 error
   4019 info
    735 warn
  (matches the Python result, 3 lines)

[27] mean ms per service, to 2 decimals
$ jq -s -r 'group_by(.svc)[] | "\(.[0].svc) \(((map(.ms) | add) / length * 100 | round) / 100)"' app.jsonl
auth 439.46
billing 463.07
search 454.87
  (matches the Python result, 3 lines)

[28] distinct users that have an error record
$ jq -r 'select(.level == "error") | .user' app.jsonl | sort -u | wc -l
40
  (matches the Python result, 1 lines)

=== joins

[29] top 5 IPs with their country (join against a sorted table)
$ awk '{print $1}' access.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -5 | awk '{print $2, $1}' | sort | join - country.txt | sort -k2,2nr -k1,1
10.0.24.99 15579 JP
10.0.60.7 8329 DE
10.0.75.170 5980 SG
10.1.11.251 4400 DE
  (matches the Python result, 5 lines)

[30] requests per country
$ awk '{c[$1]++} END {for (i in c) print i, c[i]}' access.log | sort | join - country.txt | awk '{t[$3] += $2} END {for (k in t) print k, t[k]}' | sort
DE 38531
IN 27395
JP 39419
NL 26662
  (matches the Python result, 6 lines)

checked against Python: 30 match, 0 differ
```

**All 30 matched.** The idioms they illustrate, which cover most of what is asked:

| Idiom | Items | Pattern |
| --- | --- | --- |
| **frequency table** | 1, 4, 9, 11, 14, 16, 17, 19, 23, 25, 26 | `extract | sort | uniq -c | sort -k1,1nr -k2,2 | head -N` |
| **group and aggregate** | 2, 5, 7, 18, 22, 27, 30 | `awk '{k[$1] += $2} END {for (i in k) print ...}' | sort` |
| **ratio** | 3 | `awk '$9 ~ /^5/ {e++} END {printf "%.2f%%", 100*e/NR}'` |
| **bucket by time** | 4, 10, 19, 23, 25 | `substr($4, 2, 17)` for the minute, `substr($4, 14, 2)` for the hour |
| **normalise before grouping** | 5, 9 | `sub(/\?.*/, "", p); gsub(/\/[0-9]+/, "/N", p)` so that `/api/users/42` and `/api/users/7` are one endpoint |
| **percentile** | 6 | `sort -n` then awk picks element `ceil(0.95 * NR)` (nearest rank) |
| **distinct count** | 8, 28 | `sort -u | wc -l` |
| **extract a value from a line** | 16, 17 | `grep -oE 'from [0-9.]+'` or `grep -oP 'invalid user \K\S+'` |
| **pair two events** | 20 | `o[$5] = time` on "opened", subtract on "closed" |
| **set intersection** | 21 | `comm -12 <(sort A) <(sort B)` |
| **join with a table** | 29, 30 | `sort | join - table.txt` (both sorted, same locale) |
| **JSON** | 26 to 28 | `jq -r .field | sort | uniq -c`, `jq -s 'group_by(.k)[]'` |

Four details in the bank are the kind that fail an interview answer:

- **Quoted fields change the column count.** The user agent (item 11) contains spaces, so `$6` is not the user agent; `awk -F'"'` splits on the quotes and the agent becomes field 6. The request time is `$NF` (last field), which stays correct whatever the agent contains.
- **Percentiles need a definition.** Item 6 uses the **nearest-rank** method (the smallest value whose rank is at least `0.95 * N`): 0.504 s for this data. Interpolating methods give slightly different values; say which one you use.
- **A mean per endpoint hides the tail.** Item 5 ranks `/api/reports/N` first by mean (1.402 s), but a service level is usually written in percentiles, and the mean of a skewed distribution is pulled up by a few slow requests.
- **Ties.** If two keys have the same count, `sort -rn` alone leaves their order **unspecified** on some systems; always add a second key (`-k2,2`) when the output is compared or tested.

---

## 12. What it costs: the numbers worth remembering

Measured on one VM with GNU tools; quote them as ranges (10 to 30 percent run-to-run).

| Quantity | Value |
| --- | --- |
| `wc -l` of a 69 MiB file (warm cache) | 11 ms |
| count lines matching a fixed text: `rg`, `grep -P`, `grep`, `python3` | 14, 64, 77 to 88, 112 ms |
| the same with GNU `grep` and stdout on `/dev/null` | **2 ms** (stops at the first match) |
| top IPs: `awk` array against `sort \| uniq -c` | 152 against 258 ms |
| `sort` 600,000 lines, C against `en_US.UTF-8` | 266 against 1,300 ms |
| first field of every line: `gawk`, `cut`, `sed`, `python3`, `grep -o` | 116, 144, 355, 407, 1,194 ms |
| remove duplicates: `sort \| uniq` against `awk '!seen[$0]++'` | 371 against 739 ms |
| `PCRE` catastrophic pattern at 28 characters | error after 30 ms (`grep -P`); Python about 10 s by extrapolation |
| 8 sleeps of 0.2 s: `xargs -P1` against `-P4` | 1,617 against 408 ms |
| 30 one-liners over 200,000 lines against a Python check | 30 of 30 match |

---

## 13. Common mistakes

1. **`uniq` without `sort`** (section 10).
2. **`sort` without `-n` on numbers**, and `-k2` instead of `-k2,2` (section 6).
3. **Parsing `ls` or using `find | xargs` without `-print0 -0`** ([LX 01](01_shell_and_scripting.md) section 3; section 7).
4. **Forgetting `-r` on `xargs`**, so that an empty list still runs the command once (section 7).
5. **`sed -i` with no backup, or on a symlink** (section 4.5).
6. **BRE against ERE**: `+`, `?`, `|` and `()` need backslashes in `grep` and `sed` unless `-E` is given (section 2).
7. **`\d` in `grep -E`** (matches the letter `d`); use `[[:digit:]]` or `-P` (section 2).
8. **A greedy `.*` where `[^x]*` is meant** (sections 2 and 4).
9. **Splicing shell text into `awk`, `sed` or `jq` programs**; use `-v`, `ENVIRON`, `--arg` (section 10).
10. **Different locales for `sort` and `join`**, and `LC_ALL` unset on a server whose locale differs from yours (section 9).
11. **CRLF input** (section 10).
12. **Treating `grep`'s status 1 as an error** under `set -e` or `pipefail` (section 10).
13. **Benchmarking with `> /dev/null`** (section 9).
14. **`sort file > file`** (section 10).
15. **Trusting a column number in a format with quoted spaces**; check one line first (section 1).
16. **Using awk's `for (k in a)` order**; it is unspecified, so sort the output (section 5).
17. **Big integers through `awk` or `jq`**: doubles lose precision above 2^53 (sections 5 and 8).
18. **Using `grep -P` on untrusted patterns or input**; a backtracking engine can be made to run for a very long time (section 2).

---

## 14. Questions out loud

Say each answer in 30 to 60 seconds, with one command or one measured number.

1. **What is the difference between BRE, ERE and PCRE?** BRE (`grep`, `sed` default) needs backslashes for `+ ? | ( ) { }`; ERE (`-E`, `awk`) does not; PCRE (`-P`, Perl, `rg`) adds `\d \w`, lazy quantifiers, lookaround and `\K`, and it backtracks, so a bad pattern is slow or errors (`grep -P` stopped with "exceeded PCRE's backtracking limit" after 30 ms on a pattern that `-E` answered in 2 ms).
2. **How do you count the requests per IP and show the top 10?** `awk '{print $1}' access.log | sort | uniq -c | sort -k1,1nr | head -10`; or an awk array with a final `sort`, which is faster when the keys are few (152 against 258 ms on 600,000 lines).
3. **Why does `uniq -c` need `sort`?** It only compares adjacent lines.
4. **How do you find the 95th percentile of a column?** Sort it numerically and take element `ceil(0.95 * N)` (nearest rank): `sort -n | awk '{a[NR]=$1} END{...}'`.
5. **How do you process file names with spaces and newlines?** `find -print0 | xargs -0 cmd` (or `while IFS= read -r -d ''`); never parse `ls`.
6. **What does `xargs` do on empty input, and how do you stop it?** GNU `xargs` runs the command once with no arguments; `-r` prevents that.
7. **`find -exec cmd {} \;` against `{} +`?** One process per file against many files per process (like `xargs`); `+` is faster for long lists.
8. **`sed -i` has a danger; which?** It replaces the file (new inode), has no undo without a suffix, and on a symlink replaces the link with a regular file unless `--follow-symlinks`.
9. **How do you join two files on a key?** Sort both on the key in the same locale, then `join`; or load one into an awk array (`NR==FNR{a[$1]=$2; next}`), which needs no sort.
10. **Why is `sort` slow on one machine and fast on another?** The locale: `en_US.UTF-8` collation took 1,300 ms against 266 ms in `C` on 600,000 lines. Use `LC_ALL=C` for machine processing.
11. **How do you deduplicate and keep the first occurrence?** `awk '!seen[$0]++'`; `sort -u` loses the order.
12. **What happens to `grep` in a `set -e` script when nothing matches?** It exits 1 and the script stops; use `|| true` or `if grep -q`.
13. **How do you pass a shell variable into awk safely?** `awk -v n="$x"` or `ENVIRON`, not by quoting it into the program text (injection ran a `system()` call in the experiment).
14. **When do you stop using shell tools?** When you need nested data, real data structures, floating-point exactness, or more than about 20 lines of logic: use Python, and keep the pipeline for glue.

---

## Problems

**Where to solve them.** In a scratch directory on any Linux machine with GNU `bash`, `grep`, `sed`, `awk` and `sort` (macOS needs the GNU versions: `brew install coreutils gnu-sed gawk grep findutils`). The inputs are given in the statements; type them into files with a here-document. **Run your command and compare with the stated result** before reading the answer. For more practice, the same ideas apply to your own `/var/log` files and to `ps`, `ss -tan` and `journalctl` output.

**How to approach them.** Name the stages first (extract, filter, group, order), write the one-liner **one stage at a time** and look at the output after each pipe, then add `LC_ALL=C` and a tie-breaking key, then think about empty input and odd characters.

### Problem A (E, entry level): top clients

File `a.log`, one request per line (`IP METHOD PATH STATUS`):

```text
10.0.0.2 GET /a 200
10.0.0.1 GET /b 404
10.0.0.2 GET /c 200
10.0.0.3 POST /a 500
10.0.0.2 GET /a 200
10.0.0.1 GET /a 200
10.0.0.3 GET /b 200
10.0.0.2 GET /d 404
```

Print the two clients with the most requests, with their counts (ties broken by IP), and the number of distinct clients.

### Problem B (E, entry level): clean a configuration file

File `app.conf`:

```text
# main settings
port = 80
  host=example.org

; old
debug = false   # turn on to trace
path = /var/www
```

(a) Print only the active settings as `key=value` with no spaces and no comments, changing `port=80` to `port=8080`. (b) Then edit the file **in place**, keeping a backup, to delete the lines that are entirely a comment or blank.

### Problem C (M, medium): a department summary

File `emp.csv` (with a header line):

```text
name,dept,salary
ann,eng,120
bob,eng,100
cy,ops,80
di,ops,95
ed,eng,130
fay,hr,70
```

For each department print `dept headcount total_salary best_paid_name`, sorted by department.

### Problem D (M, medium): code size with awkward names

A tree `src/` contains `one.c` (2 lines), `two words.c` (3 lines), `sub/-d.c` (1 line) and a file whose name contains a newline (4 lines). Count the **total lines** of all `.c` files safely, and explain what goes wrong with `find src -name '*.c' | xargs cat | wc -l`.

### Problem E (H, hard): per-team p90

`req.txt` lists `USER MILLISECONDS`, and `team.txt` maps users to teams:

```text
req.txt                team.txt
alice 120              alice eng
bob 80                 bob eng
alice 300              carol ops
carol 45               dave ops
bob 95
alice 60
dave 500
carol 70
bob 110
alice 90
dave 20
carol 55
```

Print, for each team, `team count p90`, where p90 is the nearest-rank 90th percentile of the request times.

---

## Answers

The answers were run, and the last one was **checked against a Python computation**.

```bash
#!/usr/bin/env bash
# ans_prob.sh: the five practice problems, each with its input and a tested answer
export LC_ALL=C
cd "$(mktemp -d)" || exit 1

echo '=== Problem A: top clients'
cat > a.log <<'L'
10.0.0.2 GET /a 200
10.0.0.1 GET /b 404
10.0.0.2 GET /c 200
10.0.0.3 POST /a 500
10.0.0.2 GET /a 200
10.0.0.1 GET /a 200
10.0.0.3 GET /b 200
10.0.0.2 GET /d 404
L
cut -d' ' -f1 a.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -2
echo "distinct: $(cut -d' ' -f1 a.log | sort -u | wc -l)"

echo
echo '=== Problem B: clean a configuration file'
cat > app.conf <<'L'
# main settings
port = 80
  host=example.org

; old
debug = false   # turn on to trace
path = /var/www
L
sed -e 's/[#;].*//' -e 's/[[:space:]]*=[[:space:]]*/=/' -e 's/^[[:space:]]*//;s/[[:space:]]*$//' -e '/^$/d' -e 's/^port=80$/port=8080/' app.conf
sed -i.bak -e '/^[[:space:]]*[#;]/d;/^[[:space:]]*$/d' app.conf; ls; wc -l < app.conf

echo
echo '=== Problem C: per-department summary'
cat > emp.csv <<'L'
name,dept,salary
ann,eng,120
bob,eng,100
cy,ops,80
di,ops,95
ed,eng,130
fay,hr,70
L
awk -F, 'NR > 1 {n[$2]++; s[$2] += $3; if ($3 > m[$2]) {m[$2] = $3; w[$2] = $1}} END {for (d in n) printf "%s %d %d %s\n", d, n[d], s[d], w[d]}' emp.csv | sort

echo
echo '=== Problem D: lines of C code in a tree with awkward names'
mkdir -p src/sub; printf 'a\nb\n' > 'src/one.c'; printf 'a\nb\nc\n' > 'src/two words.c'; printf 'x\n' > 'src/sub/-d.c'; printf 'a\nb\nc\nd\n' > $'src/new\nline.c'
echo "naive:  $(find src -name '*.c' | xargs cat 2>&1 | wc -l) lines, $(find src -name '*.c' | xargs cat 2>&1 >/dev/null | wc -l) error lines"
echo "safe:   $(find src -name '*.c' -print0 | xargs -0 cat | wc -l) lines"
echo "awk:    $(find src -name '*.c' -exec awk 'END {print NR}' {} + | paste -sd+ | bc) lines"

echo
echo '=== Problem E: the p90 request time of each team'
cat > req.txt <<'L'
alice 120
bob 80
alice 300
carol 45
bob 95
alice 60
dave 500
carol 70
bob 110
alice 90
dave 20
carol 55
L
printf 'alice eng\nbob eng\ncarol ops\ndave ops\n' | sort > team.txt
sort -k1,1 req.txt | join - team.txt | awk '{print $3, $2}' | sort -k1,1 -k2,2n |
  awk '{v[$1, ++n[$1]] = $2} END {for (t in n) {i = int(0.9 * n[t]); if (i < 0.9 * n[t]) i++; print t, n[t], v[t, i]}}' | sort
echo "(check)"
python3 - <<'P'
import collections, math
team = dict(l.split() for l in open('team.txt'))
d = collections.defaultdict(list)
for l in open('req.txt'):
    u, ms = l.split(); d[team[u]].append(int(ms))
for t in sorted(d):
    v = sorted(d[t]); print(t, len(v), v[math.ceil(0.9 * len(v)) - 1])
P
```

```text
=== Problem A: top clients
      4 10.0.0.2
      2 10.0.0.1
distinct: 3

=== Problem B: clean a configuration file
port=8080
host=example.org
debug=false
path=/var/www
a.log
app.conf
app.conf.bak
4

=== Problem C: per-department summary
eng 3 350 ed
hr 1 70 fay
ops 2 175 di

=== Problem D: lines of C code in a tree with awkward names
naive:  7 lines, 4 error lines
safe:   10 lines
awk:    10 lines

=== Problem E: the p90 request time of each team
eng 7 300
ops 5 500
(check)
eng 7 300
ops 5 500
```

**Problem A.** `cut -d' ' -f1 a.log | sort | uniq -c | sort -k1,1nr -k2,2 | head -2` printed `4 10.0.0.2` and `2 10.0.0.1` (the clients `10.0.0.1` and `10.0.0.3` both have 2 requests, and the second key `-k2,2` picks `10.0.0.1`), and `sort -u | wc -l` gave **3** distinct clients.

**Problem B.** (a) The sed script has five steps: `s/[#;].*//` removes comments (to the end of line), `s/[[:space:]]*=[[:space:]]*/=/` removes the blanks around `=`, `s/^[[:space:]]*//;s/[[:space:]]*$//` trims the line, `/^$/d` deletes lines that are now empty, and `s/^port=80$/port=8080/` changes the port. The result is `port=8080`, `host=example.org`, `debug=false`, `path=/var/www`. (b) `sed -i.bak -e '/^[[:space:]]*[#;]/d;/^[[:space:]]*$/d' app.conf` deletes only the **whole-line** comments and blank lines (the trailing comment on the `debug` line stays, which is what "entirely a comment" asks for); the transcript shows the backup `app.conf.bak` and **4 lines** left in `app.conf`. The order of the steps in (a) matters: removing comments before trimming and before the empty-line test is what turns a comment-only line into an empty line that `/^$/d` can delete.

**Problem C.** `awk -F, 'NR > 1 {n[$2]++; s[$2] += $3; if ($3 > m[$2]) {m[$2] = $3; w[$2] = $1}} END {...}'` skips the header (`NR > 1`), counts and sums per department, and remembers the largest salary and its owner in two more arrays; `sort` makes the order deterministic. Output: `eng 3 350 ed`, `hr 1 70 fay`, `ops 2 175 di`. The comparison `$3 > m[$2]` is numeric because `$3` is a strnum and `m[$2]` starts as an uninitialised variable (0).

**Problem D.** The naive pipeline printed `7 lines` and **4 error lines**: `xargs` splits its input at whitespace, so `two words.c` becomes the two arguments `two` and `words.c` (both missing) and the newline name splits the same way, giving 4 `No such file` messages; the 7 lines are those 4 error messages plus only 3 real lines (`one.c` and `sub/-d.c`), and the 7 lines of text are **not** the answer. The correct total is **10**, computed two ways: `find src -name '*.c' -print0 | xargs -0 cat | wc -l`, and `find src -name '*.c' -exec awk 'END {print NR}' {} + | paste -sd+ | bc`, which counts per batch and sums. The file `sub/-d.c` is harmless here only because `find` prints it as `src/sub/-d.c`, a path that does not start with a dash; in a command such as `cd src/sub; cat *` the name would be read as an option and need `--` or `./`. Rule: **NUL separators between `find` and `xargs`, and a path that cannot be mistaken for an option.**

**Problem E.** Join the two files (both sorted on the user), keep `team` and `ms`, sort by team then numeric ms, and let awk pick the element at index `ceil(0.9 * n)` of each team's sorted list using the key `v[team, i]`:

`sort -k1,1 req.txt | join - team.txt | awk '{print $3, $2}' | sort -k1,1 -k2,2n | awk '{v[$1, ++n[$1]] = $2} END {for (t in n) {i = int(0.9 * n[t]); if (i < 0.9 * n[t]) i++; print t, n[t], v[t, i]}}' | sort`

It printed `eng 7 300` and `ops 5 500`, and the Python check printed the same lines: `eng` has 7 requests and the 7th value (ceil of 6.3) is 300; `ops` has 5 and `ceil(4.5)` is 5, the largest, 500. The two details that go wrong are the **join needing sorted input** (here sorted by user in the C locale) and the **float comparison** `i < 0.9 * n[t]`, which implements the ceiling without a `ceil` function.

---

## Cheat sheet

```text
Dialects        BRE: \+ \? \| \( \)     ERE (-E, awk): + ? | ( )     PCRE (-P): \d \w .*? (?<=..) \K     literal text: -F
grep            -c count  -v invert  -o only match  -w word  -x line  -n  -l/-L  -r --include  -q  -A/-B/-C  -m N  -a binary
grep status     0 match, 1 no match (not an error!), 2 error     never benchmark with > /dev/null (stops at first match)
sed             sed -n 'N,Mp'  '$='  '/a/,/b/'  '2~2'  's/re/new/gI'  -E  -i.bak  --follow-symlinks  -s  -z  y///  a\ i\ c\
sed hold space  tac: 1!G;h;$!d     join: :a;N;$!ba;s/\n/,/g     line before match: /re/{x;p;x};h
awk             pattern{action}  BEGIN END  $NF NR FNR FS OFS  -F -v  c[$1]++  !seen[$0]++  NR==FNR{a[$1]=$2;next}  printf  substr 1-based
awk traps       strnum compare, uninitialised = 0 and "", for-in order unspecified, doubles above 2^53, never splice shell text
cut tr          cut -d, -f1,3  -c  (single-char delimiter)     tr -d -s -c '[:class:]'     squeeze blanks before cut
sort            -n -h -V -r -u -s -t, -k2,2nr -k1,1 -o  -S  --parallel      uniq: adjacent only; sort|uniq -c|sort -k1,1nr
join comm       sorted input, same locale     join -a1 -v1 -o     comm -12 both  -23 first only  -13 second only
find xargs      find -name -type -size -mtime -newer -prune -print0 -exec +      xargs -0 -r -n -P -I{}      find|xargs: NUL always
jq              -c -r -s -e  select()  .a.b  group_by  map  add  @csv  --arg  //  numbers above 2^53 become doubles
locale          LC_ALL=C for machine processing: faster (266 vs 1,300 ms sort) and byte-exact; join and comm need matching collation
set -e + grep   grep || true   or   if grep -q ...     pipefail turns SIGPIPE (141) into a failure
safe edits      sort -o f f     or  tmp + mv      sed -i.bak
```

---

## Sources

Fetched in this session (what each confirmed):

- [Addresses (GNU sed manual)](https://www.gnu.org/software/sed/manual/html_node/Addresses.html): when the second address of a range is a regular expression, checking starts on the line **after** the line that matched the first address, so such a range spans at least two lines unless the input ends first; `first~step` selects every step-th line starting at `first`; `!` after an address inverts the selection. It does not say what happens when the end pattern never matches (the measurement above shows the range runs to the end of the input).
- [Exit Status (GNU grep manual)](https://www.gnu.org/software/grep/manual/html_node/Exit-Status.html): 0 means at least one line was selected, 1 means none, 2 means an error; with `-q`, a selected line gives 0 even if an error also occurred.

Not retrieved: the GNU awk manual page on string functions (the site refused the fetch), so the `substr` behaviour for a start below 1 and the gawk-only extensions are stated **from the measurements only** (gawk 5.2.1 and mawk, which disagree for `substr`).

Run in this session: GNU grep 3.11, GNU sed 4.9, GNU Awk 5.2.1 and mawk, GNU coreutils 9.4, findutils, jq 1.7, ripgrep 14.1, bash 5.2.21 and Python 3 (the oracle), on Linux 6.18.44 (2 vCPUs, about 7 GiB). The `en_US.UTF-8` locale was compiled for this session.

Cross-references: [LX 01](01_shell_and_scripting.md) (quoting, pipes, `pipefail`, the `find | xargs` and `read` patterns, atomic writes).

From memory, not fetched or not tested in this session: BSD and macOS differences (`sed -i`, `grep -P`, `\L` and `\u`); that `sed -i` preserves permissions but not hard links; that older `grep` versions printed `Binary file ... matches` on stdout; that the `find` `-prune -o` without an explicit action prints pruned directories; and the behaviour of other locales, `mawk` versions and BusyBox tools.
