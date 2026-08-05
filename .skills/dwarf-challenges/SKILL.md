---
name: dwarf-challenges
description: >-
  Diagnose and reproduce hard SystemTap DWARF / loc2stap cases (DW_OP_entry_value,
  pretty-printed $$parms$/$$vars$, synthetic entry probes). Use when debugging
  unresolved __global_tvar_entry_value_*, @entry / GNU_entry_value failures,
  hunting probe sites with readelf --debug-dump=loc, or writing tests for
  location-list edge cases that GCC inlining makes hard to pin down.
---

# DWARF challenges (loc2stap / target vars)

GCC/Clang emit location lists that SystemTap must turn into script expressions
(`loc2stap.cxx`, `dwflpp.cxx`, `dwarf_var_expanding_visitor` in `tapsets.cxx`).
Some opcodes are rare at function entry and only appear mid-body after
registers are clobbered — so `probe …function("foo") { $$parms$ }` often never
exercises them. Prefer **concrete PC / statement** probes found from DWARF.

## Symptom: unresolved `__global_tvar_entry_value_*`

```text
semantic error: unresolved arity-1 global array __global_tvar_entry_value__…,
missing global declaration?: identifier '$$parms$'
```

`location_context::handle_GNU_entry_value` synthesizes a tid-indexed global
plus a synthetic **entry** probe that stores the value. Callers must merge
`ctx.globals` into `sess.globals` and `ctx.entry_probes` into the dwarf
visitor’s sink. The non-pretty `$var` path does this in
`visit_target_symbol`; the pretty-print path (`$var$`, `$$parms$`, `$$vars$`)
must do the same in `dwarf_pretty_print::deref`.

Trailing `$` means pretty-print (`target_symbol::check_pretty_print`).
`$$parms$` copies that pretty-print component onto each `$param` it expands.

## Finding `DW_OP_entry_value` sites (binutils readelf)

Prefer **binutils** `readelf` over `eu-readelf` — much faster on large CUs.

```bash
# Location ranges that use entry_value (DWARF5 .debug_loclists)
readelf --debug-dump=loc ./stap 2>/dev/null \
  | rg 'DW_OP_entry_value|DW_OP_GNU_entry_value' | head

# Begin addresses of those ranges (16 hex digits, leading spaces)
readelf --debug-dump=loc ./PATH 2>/dev/null \
  | rg -o '^\s+([0-9a-f]{16}) ([0-9a-f]{16}) \(DW_OP_entry_value' -r '$1' \
  | sort -u > /tmp/ev_begin.txt

# Map PC → function / source (many hits are libstdc++ inlined into stap)
addr2line -fe ./PATH 0x54db82 | c++filt

# What vars are live at that statement?
./stap -L 'process("./PATH").statement(0x54db82)'
```

Example loclist shape:

```text
000000000054db70 000000000054db82 (DW_OP_reg5 (rdi))
000000000054db82 000000000054db84 (DW_OP_entry_value: (DW_OP_reg5 (rdi)); DW_OP_stack_value)
```

Probe the **entry_value** PC (here `0x54db82`), not only the function entry.

## Reproducing with stap

Function-entry probes often miss entry_value (prologue still has the
register). Use a statement / absolute PC:

```bash
# Pretty-print path (historical bug): must register synthetic globals
./stap -p2 -e 'probe process("./stap").statement(0x54db82) { println($$vars$) }'
./stap -p2 -e 'probe process("./stap").statement(0x54db82) { println($$parms$) }'

# Non-pretty control (should already merge globals)
./stap -p2 -e 'probe process("./stap").statement(0x54db82) { println($$vars) }'
```

PCs are binary-specific (ASLR-irrelevant for non-PIE `./stap`, but rebuilds
shift addresses). Re-run the `readelf` hunt after each rebuild; do not hardcode
a PC from another machine into the permanent testsuite without a more stable
anchor.

## Testsuite reality

Stable dejagnu coverage is hard: `-O2` / LTO / libstdc++ headers decide where
`DW_OP_entry_value` appears, and named `kernel.function("…")` sites vary by
kernel debuginfo (e.g. RHEL9 `do_sys_open*` + `$$parms$`). When adding a test:

1. Prefer a **checked-in tiny C** fixture compiled with known flags that force
   an entry_value loc (if you find flags that stick across distros).
2. Or document a **manual / optional** repro against `./stap` via `readelf` as
   above — not as a hard FAIL on builders without matching DWARF.
3. Always verify both pretty (`$$vars$`) and non-pretty (`$$vars`) paths.

## Code map

| Piece | File |
|-------|------|
| Opcode translate / synthetic global | `loc2stap.cxx` (`handle_GNU_entry_value`) |
| Non-pretty merge of globals/entry probes | `tapsets.cxx` `dwarf_var_expanding_visitor::visit_target_symbol` |
| Pretty-print merge | `tapsets.cxx` `dwarf_pretty_print::deref` |
| Entry probes → synthetic `.function` | `dwarf_derived_probe` ctor (`query_addr` on `entry_probes`) |
