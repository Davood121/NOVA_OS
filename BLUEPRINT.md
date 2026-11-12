# GRID OS — Design Blueprint

**Version 0.1 — Concept**
A general-purpose operating system where the entire machine is one live, reactive computation graph.

---

## 1. Vision

Every existing OS is built on the same three pillars: **files**, **processes**, and **implicit shared state** mutated by syscalls. GRID OS replaces all three with a single primitive:

> **The Cell** — a named, typed, versioned value that may be defined by a formula over other cells.

The whole OS — documents, apps, drivers, the UI, the network stack, even the scheduler's own configuration — is one global graph of cells. When an input cell changes, everything downstream re-computes automatically and incrementally. There is no "saving", no "running" a program as a separate act, and no stale data anywhere in the machine. Using the computer feels like editing one impossibly large, impossibly fast spreadsheet that *is* the computer.

### Design principles

1. **One primitive.** Cells are the only user-visible storage, computation, and communication mechanism. Everything else is sugar.
2. **Time is first-class.** Every change is a transaction. The system can be rewound, branched, and replayed like a git repo whose HEAD is "now".
3. **Determinism by default.** Computation is pure. All effects (I/O, network, clock) enter through explicit, capability-gated *source cells*.
4. **Incremental everything.** The kernel never recomputes what it can prove is unchanged. This is not an optimization; it is the correctness model.
5. **Local-first, graph-native networking.** A remote cell reference has the same shape as a local one. Distribution is a property of edges, not an API.

---

## 2. The Core Model

### 2.1 Cells

```
Cell {
  id        : ContentHash          // identity of definition, not of location
  name      : Path                 // e.g. /home/ada/notes/todo.index
  type      : Schema               // number | text | record | stream | graph | ...
  body      : Value | Formula      // literal value, or expression over other cells
  revision  : (txid, lamport)      // when this version became true
  deps      : Set<CellId>          // derived from the formula, static + dynamic
  caps      : Set<Capability>      // what this cell may observe or affect
}
```

- **Leaf cells** hold values (a typed character, a sensor reading, a pixel buffer, a chunk of a document).
- **Formula cells** hold expressions: `sum(cart.items.price)`, `parse(http.get(url))`, `window.focus == self`.
- **Effect cells** are the only impurity: their formula *commits* an action to the outside world (draw pixels, send packet, play sound). The kernel executes effects exactly once per transaction, in a declared order.

### 2.2 The transaction log

All state change is a **transaction**: an atomic set of cell updates with a new transaction id `txid`. The log is:

- **Append-only** and content-addressed (like git objects, but continuous).
- **The source of truth.** "Disk" is just the newest snapshot + log tail. There is no filesystem and no database underneath — the log *is* both.
- **Replayable.** Any state at any `txid` can be reconstructed by evaluating the graph against the log prefix. Undo = truncate/rebase; time travel = evaluate at old `txid`.

### 2.3 Evaluation

The kernel's heart is an **incremental evaluator** (a supercharged spreadsheet engine — think build systems' dirty-marking + FRP glitch-freedom):

1. Input changes arrive (user keystroke, packet, timer tick).
2. Kernel opens a transaction, marks dirty cells.
3. Topologically evaluates dirty subgraph in dependency order; a transaction is **glitch-free** (no cell is ever observed mid-update).
4. Effect cells execute after all values settle.
5. Transaction commits to the log.

Loops are legal and useful (iterative solvers, games) via **convergent cells**: cells declared as `fixpoint` that iterate until a declared stability predicate holds, with a kernel-enforced step budget.

---

## 3. Kernel Architecture

```
┌──────────────────────────────────────────────────────┐
│                     GRID Kernel                      │
│  ┌────────────┐  ┌─────────────┐  ┌───────────────┐  │
│  │ Source     │  │ Incremental │  │ Effect        │  │
│  │ Ingest     │→ │ Evaluator   │→ │ Executor      │  │
│  │ (irq/net/  │  │ (dirty      │  │ (gpu/disk/    │  │
│  │  clock/usb)│  │  propagation)│ │  net/audio)   │  │
│  └────────────┘  └─────────────┘  └───────────────┘  │
│  ┌──────────────────────────────────────────────┐    │
│  │ Transaction Log / Snapshot Store (persistent)│    │
│  └──────────────────────────────────────────────┘    │
│  ┌───────────────┐ ┌──────────────┐ ┌────────────┐   │
│  │ Cap Checker   │ │ Cell Cache   │ │ Scheduler  │   │
│  └───────────────┘ └──────────────┘ └────────────┘   │
└──────────────────────────────────────────────────────┘
```

- **Source Ingest** replaces drivers-as-code: a driver is a *source cell family* (`/dev/net/eth0.rx` is a stream cell the NIC interrupt feeds). Driver logic is formula logic, sandboxed by the evaluator rather than ring-0 trust.
- **Scheduler** = work scheduling for the evaluator: prioritize the subgraph the focused UI depends on (interaction latency < 8 ms budget), batch background recompute, and pause low-priority fixpoints. There is no "process"; there is only *graph priority*.
- **Memory model**: cells are immutable values; the kernel is a giant generational GC over the content-addressed store. Hot cells live in RAM, cold cells in the log store — the same GC moves values between them. No mmap, no page tables exposed to userland.

---

## 4. Apps Are Subgraphs

An **app** is a directory of cells with a manifest — not an executable.

```
/app/paint/
  manifest.cell      // name, version, caps requested, exported cells
  canvas.cells       // formula: raster of strokes
  stroke.cells       // input handling as formulas over pointer stream
  ui.cells           // view cells bound to canvas
```

- **Installing** an app = grafting its cells into your graph, granting its declared capabilities.
- **Running** an app = nothing special; its cells are simply live.
- **Two instances** = the same subgraph grafted at two paths (cells are pure, so copies are cheap).
- **Every app is inspectable**: click any pixel of the UI and drill down into the formula chain that produced it. Debugging is *browsing the graph*, with every intermediate value frozen at the current txid.

### 4.1 The UI

The display is a cell tree; widgets are formulas from state cells to view cells. There is no event loop and no callback: "on click" is a formula over the pointer stream cell. Because UI = data, the system compositor can offer universal features no app has to implement: per-pixel time scrubbing, per-subtree undo, live "what-if" editing of any app's state.

---

## 5. Security Model

No users, no root, no ambient authority.

- **Capabilities are graph edges.** A cell can only reference cells it was granted at creation (or that were explicitly shared to it). The evaluator enforces this by construction — an unauthorized reference simply doesn't resolve.
- **Capabilities are unforgeable, delegable, revocable** tokens (paths like `/caps/net/eth0` that resolve only inside the grantee's subtree).
- **Effects require capability cells**; a formula cannot draw, dial out, or touch disk unless an effect-cap edge exists.
- **Confidentiality**: cells can be marked *opaque* — the evaluator computes with them but their values are not readable by formulas outside a granted edge (enforced via kernel-mediated sealed values).
- Because all dependencies are static-ish and declared, **audit is trivial**: "what data can this app see?" is a graph query, answered exactly, not a heuristic.

---

## 6. Distribution

- A **remote cell** is named by `(site-id, cell-path)`; the kernel syncs it like a CRDT-backed replica: local reads are instant from the last synced revision; writes propagate as transactions.
- Offline edits are just local transactions; merge is CRDT resolution at the cell level, producing a merge transaction in the log.
- Collaboration is native: two people editing the same document cell-range *are* two writers to one graph — Google-Docs-style merge for free, for every app.

---

## 7. Compatibility

GRID does not pretend to be Unix, but it ships a **projection layer**: a userland service that exposes a POSIX view (files = projected cell trees, processes = fixpoint cells with a TTY stream, sockets = stream cells). Legacy apps run inside this projection on a bundled Linux compatibility kernel, and their file writes become transactions in the log — so even legacy work gets time travel and graph inspection.

---

## 8. Boot Sequence

1. Firmware → GRID bootloader (verifies log store integrity).
2. Kernel loads last snapshot + replays log tail into the evaluator.
3. Kernel mounts `/sys` (hardware source cells) and `/boot` (boot cells).
4. Evaluates `/boot/session` — the user's session graph. Desktop appears when its view cells settle. Boot time = evaluation time of the session subgraph, typically < 2 s from snapshot.

---

## 9. Implementation Roadmap

| Phase | Deliverable | Tech |
|---|---|---|
| **0. Simulator** | GRID-as-a-VM: the full cell/transaction/evaluator model as a REPL app on a host OS. Validates the semantics and the incremental evaluator. | Rust or Zig; runs anywhere |
| **1. Toy kernel** | Bootable x86_64 kernel: log store, evaluator, `sys` source cells (keyboard/timer/VGA), a shell that *is* a cell editor. | Rust + no_std,Limine bootloader, QEMU |
| **2. Persistence + time travel** | Snapshots, txid scrubbing, per-subtree undo. | extends Phase 1 |
| **3. Capabilities + apps** | Grafting, manifests, capability edges, a paint app and a notes app as proof. | extends Phase 1–2 |
| **4. UI compositor** | View-cell compositor, per-pixel graph inspection. | GPU effect cells |
| **5. Distribution + POSIX projection** | Remote cells, CRDT merge, legacy layer. | long-term |

**Phase 0 is the critical path**: the incremental evaluator with glitch-free transactions is the novel core; everything else is (large) engineering around it.

---

## 10. Risks & Mitigations

- **Recompute storms** (one keystroke invalidates a huge graph) → dependency-aware scheduling, memoized subgraphs, lazy streams with windowing.
- **Semantic impedance** (some things really are imperative: protocols, DMA) → effect cells + fixpoint cells cover the space; drivers may keep small imperative cores behind source-cell interfaces.
- **Memory pressure** from immutable values → structural sharing + generational GC over content-addressed store (same machinery as the log).
- **Ecosystem gravity** → the POSIX projection is the migration path; GRID must be *worth* native development, which the universal time-travel/inspection UX is designed to be.

---

## 11. Summary

GRID OS bets that the spreadsheet got computation right and the desktop got it wrong. One primitive (cells), one history (the transaction log), one security model (capability edges), and a machine that is always consistent, always inspectable, and always rewindable.
