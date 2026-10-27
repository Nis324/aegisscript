# AegisScript — language reference

How someone writes an NPC in AegisScript, and every construct the language has.

---

## The workflow

There is no special editor. You write a plain text file with a `.aegis`
extension in whatever you already use — VS Code, Vim, Notepad.

```
 1. write            guard.aegis          ← you write this, ~60 lines
 2. compile          ./aegisc guard.aegis -o out
 3. compiler checks  rejects logic faults, or...
 4. compiler emits   out/GuardAI.h, out/GuardAI.cpp
 5. you build        g++ your_game.cpp out/GuardAI.cpp
 6. your game calls  npc.update();  npc.dispatch(Event::PlayerSpotted);
```

Step 3 is the point of the tool. If the behaviour has a logical fault, you get
a message with a line number and **no C++ is produced**, so a broken machine
cannot reach the build.

The generated files have no dependency on the compiler — `aegisc` is a
build-time tool, not something your game ships with.

---

## File structure

One file, one machine:

```
machine <Name> {
    context { ... }     // the agent's variables       (optional)
    events  { ... }     // what the world can signal   (optional)
    actions { ... }     // what the engine can be told (optional)

    initial state <Name> { ... }
    state <Name> { ... }
    parallel state <Name> { ... }
    final state <Name> { ... }
}
```

Exactly one state at the top level must be marked `initial`. Order does not
matter — you can reference a state before it is declared.

---

## 1. Context — the agent's memory

Variables the NPC carries. Three types.

```
context {
    int   alertLevel = 0;
    float stamina    = 100.0;
    bool  hasWeapon  = true;
    int   searchTime;              // no initialiser: defaults to 0
}
```

- `int` → `int32_t`, `float` → `float`, `bool` → `bool` in the generated C++
- An uninitialised `int`/`float` starts at 0, a `bool` at `false`
- These become a plain struct your game can read and write:
  `npc.ctx.stamina`

## 2. Events — what the world can tell the NPC

```
events {
    PlayerSpotted,
    PlayerLost,
    Attacked,
    Killed
}
```

Just names, comma-separated. They become an enum. Your game calls
`npc.dispatch(Event::PlayerSpotted)` when something happens.

## 3. States

```
state Patrol {
    ...
}
```

With optional modifiers:

| Written | Meaning |
|---|---|
| `state X` | An ordinary state |
| `initial state X` | Where the NPC starts. Exactly one at the top level |
| `final state X` | A designed end state. Exempt from the sinkhole check |
| `parallel state X` | Holds orthogonal regions — see section 7 |

`initial` and `final` cannot be combined (AEG-E024). `initial parallel state X`
is fine: a parallel state can be where the machine starts.

## 4. Actions — `on enter`, `on update`, `on exit`

```
state Chase {
    on enter  { alertLevel = 2; }
    on update { stamina = stamina - 1.5; }
    on exit   { searchTime = 0; }
}
```

| Block | Runs when |
|---|---|
| `on enter` | The NPC arrives in this state |
| `on update` | Every frame, while in this state (your game calls `npc.update()`) |
| `on exit` | The NPC leaves this state |

A block holds zero or more statements, and there are exactly two kinds:
**assignment** and **an engine action call** (section 8). No loops, no
conditionals, no expressions with side effects. That is a deliberate
restriction: control flow belongs in the state graph, which is what keeps the
behaviour analysable.

## 5. Transitions

```
on <Event> -> <TargetState>;
on <Event> -> <TargetState> when <condition>;
```

Examples:

```
on Attacked      -> Combat;                          // unconditional
on PlayerSpotted -> Chase  when stamina > 20.0;      // guarded
on PlayerSpotted -> Rest   when stamina <= 20.0;     // the other branch
```

Rules:

- Multiple guarded transitions on the same event are fine — they are tried in
  the order written, first true guard wins
- **Two unconditional transitions on the same event is an error** (AEG-E011).
  The behaviour would depend on declaration order, not on design
- If every transition on an event is guarded and all guards are false, the
  event is ignored. The compiler warns about this (AEG-W002)
- An event with no transition in the current state is simply ignored

## 6. Nested states

A state can contain states.

```
state Combat {
    on enter { rage = 1; }
    on exit  { rage = 0; }

    on Staggered -> Recovering;      // every phase below inherits this
    on Killed    -> Dead;

    initial state Windup {
        on Swing -> Strike when stamina > 10.0;
    }
    state Strike {
        on Connect   -> Recovering;
        on Staggered -> Idle;        // overrides the inherited one
    }
    state Recovering {
        on Recover -> Windup;
    }
}
```

What this buys you:

- A transition on the composite is **inherited** by every substate
- A substate can **shadow** it by declaring its own on the same event
- Entering `Combat` from outside lands in `Windup`, its `initial` child
- Moving `Windup → Strike` runs Windup's exit and Strike's entry, but **not**
  Combat's — you never left Combat
- Leaving Combat entirely runs the phase's exit, then Combat's exit

A composite must nominate exactly one `initial` child (AEG-E016 / AEG-E017).

Nesting can go arbitrarily deep, though only two levels have been exercised.

---

## 7. Parallel regions

Sometimes an agent is doing two independent things at once: moving in some
mode, *and* holding a weapon in some state. Drawing a weapon does not change
how fast you are moving, and sprinting does not holster anything. Writing that
as one flat machine means one state per combination, and every transition
repeated in every combination.

A state marked `parallel` holds **orthogonal regions** — the agent is in one
leaf of *each* region simultaneously:

```
parallel state Active {
    on enter { alert = 1; }
    on exit  { alert = 0; }
    on Destroy -> Wrecked;               // inherited by every combination

    state Movement {
        initial state Walking { on enter { gear = 1; } on Sprint -> Running; }
        state Running { on enter { gear = 2; } on Tire -> Walking; }
    }
    state Weapon {
        initial state Holstered { on enter { armed = false; } on Draw -> Drawn; }
        state Drawn { on enter { armed = true; } on Holster -> Holstered; }
    }
}
```

Each direct child of a `parallel` state is a region, and each region needs its
own `initial` child. The compiler multiplies the regions out — two regions of
two states become four **product states** named `Active_Walking_Holstered`,
`Active_Walking_Drawn`, and so on.

What the multiplication guarantees:

- `Draw` moves only the Weapon component; the Movement component is not
  re-entered and its entry action does not re-run
- `Sprint` likewise moves only the Movement component
- `Active`'s own entry action runs once, on entering the parallel, and does
  **not** re-run while you move around inside it
- `Destroy`, declared once on the parallel, exits every component of every
  product state and lands in `Wrecked`

**The cost is stated, not hidden.** The state space multiplies rather than
adds: k regions of n states each produce n^k product states. The compiler
reports the multiplication in `--metrics` and warns (AEG-W009) past 64 states.

Two restrictions, both diagnosed rather than silently mishandled:

- Regions cannot themselves be `parallel` (AEG-E023)
- One event cannot be handled by two regions of the same parallel state
  (AEG-E025) — firing both at once would require conjoining their guards

See `examples/mech.aegis`.

---

## 8. Engine actions

Behaviour is only useful if it can reach the game. An `actions` block declares
the functions the host engine will supply:

```
actions {
    PlayAnim(int clip);
    Shout(int volume, float pitch);
}
```

They are then called like ordinary statements, anywhere a statement is allowed:

```
state Chase {
    on enter { alert = 2; PlayAnim(2); Shout(80, 1.2); }
}
```

Every call site is checked against the declaration — the action must exist
(AEG-E019), the argument count must match (AEG-E020) and each argument's type
must match (AEG-E021), with `int` promoting to `float` but not the reverse. An
action declared and never called is a warning (AEG-W007).

The generated header declares what you must provide:

```cpp
namespace hooks {
    void PlayAnim(TurretContext& ctx, int32_t clip);
    void Shout(TurretContext& ctx, int32_t volume, float pitch);
}
```

You write those as plain free functions and link them in. They receive the
agent's context by reference, so an action can read and write the agent's
variables. There is no virtual dispatch, no function-pointer table and no
allocation — the call is a direct call, and the linker resolves it.

---

## Expressions

Used in guards (`when ...`) and on the right of assignments.

**Values:** integer literals (`42`), float literals (`1.5`), `true`, `false`,
and context variable names.

**Operators**, loosest binding first:

| Precedence | Operators | Notes |
|---|---|---|
| 1 (loosest) | `\|\|` | both sides must be `bool` |
| 2 | `&&` | both sides must be `bool` |
| 3 | `==` `!=` | can't compare a `bool` with a number |
| 4 | `<` `>` `<=` `>=` | `bool` not allowed |
| 5 | `+` `-` | `bool` not allowed |
| 6 | `*` `/` | `bool` not allowed |
| 7 (tightest) | unary `-`, `!` | `!` needs `bool`, `-` doesn't allow it |

Parentheses work as expected: `(a + b) * 2`.

**Types:** `int` widens to `float` automatically. A guard must evaluate to
`bool` (AEG-E010). Assigning a `float` to an `int` is an error (AEG-E009).

```
on Hunt -> Chase when stamina > 20.0 && hasWeapon && alertLevel >= 2;
```

## Comments

```
// line comment
/* block
   comment */
```

## Naming

Identifiers are letters, digits and underscore, not starting with a digit.
Case-sensitive. The reserved words are:

```
machine  context  events  state  initial  final
on  enter  update  exit  when
int  float  bool  true  false
```

Eleven keywords plus the type and literal words. One composite operator, `->`.

---

## A complete machine, start to finish

```
// A guard that patrols, chases, fights, tires, and can be killed.
machine GuardAI {

    context {
        int   alertLevel = 0;
        float stamina    = 100.0;
        bool  hasWeapon  = true;
    }

    events { PlayerSpotted, PlayerLost, Attacked, Exhausted, Recovered, Killed }

    initial state Patrol {
        on enter  { alertLevel = 0; }
        on update { stamina = stamina + 0.5; }

        on PlayerSpotted -> Chase when stamina > 20.0;
        on PlayerSpotted -> Rest  when stamina <= 20.0;
        on Attacked      -> Combat;
        on Killed        -> Dead;
    }

    state Chase {
        on enter  { alertLevel = 2; }
        on update { stamina = stamina - 1.5; }

        on PlayerLost -> Patrol;
        on Attacked   -> Combat;
        on Exhausted  -> Rest;
        on Killed     -> Dead;
    }

    state Combat {
        on enter  { alertLevel = 3; }
        on update { stamina = stamina - 2.0; }

        on PlayerLost -> Patrol;
        on Exhausted  -> Rest;
        on Killed     -> Dead;
    }

    state Rest {
        on update { stamina = stamina + 3.0; }

        on Recovered -> Patrol;
        on Attacked  -> Combat;
        on Killed    -> Dead;
    }

    final state Dead {
        on enter { alertLevel = 0; hasWeapon = false; }
    }
}
```

Compile it:

```bash
./aegisc guard.aegis -o out --metrics
```

Use it:

```cpp
#include "GuardAI.h"

aegis_gen::GuardAI npc;

// every frame
npc.update();

// when the world signals something
npc.dispatch(aegis_gen::GuardAIEvent::PlayerSpotted);

// read the agent's state
if (npc.state() == aegis_gen::GuardAIState::Combat) { playAttackAnim(); }
float s = npc.ctx.stamina;
const char* name = npc.stateName();     // "Combat"
npc.reset();                            // back to the initial state
```

---

## Mistakes the compiler catches

| You wrote | It says |
|---|---|
| A state with no way out, not marked `final` | AEG-E012 sinkhole — "the NPC would freeze here" |
| A state nothing transitions into | AEG-E013 unreachable |
| `-> Flee` where `Flee` doesn't exist | AEG-E006 |
| `on Heal ->` where `Heal` isn't in `events` | AEG-E007 |
| `when mana < 5` where `mana` isn't in `context` | AEG-E008 |
| `when stamina` (not a boolean) | AEG-E010 |
| Two unconditional transitions on one event | AEG-E011 |
| Forgot `initial` on any state | AEG-E001 |
| A composite with no `initial` child | AEG-E016 |
| `PlayAnim(2)` where `PlayAnim` isn't in `actions` | AEG-E019 |
| `Shout(80)` where `Shout` takes two arguments | AEG-E020 |
| `PlayAnim(1.5)` where the parameter is `int` | AEG-E021 |
| One event handled by two regions of one `parallel` | AEG-E025 |

Every message carries the file and line number. 35 diagnostics in total
(AEG-E000, AEG-E001…E025, AEG-W001…W009).

---

## What the language deliberately does not have

- **Loops and conditionals in actions.** Assignment and engine calls only.
  Control flow belongs in the state graph, which is what makes the graph
  analysable
- **Strings, arrays, structs.** Three scalar types only
- **History states.** Re-entering a composite always lands in its `initial`
  child; it cannot resume the substate last occupied
- **Simultaneous multi-region transitions.** One event moves one region
- **Runtime modification.** Everything is fixed at compile time, which is what
  gives the bounded footprint and zero allocation

Each of these is a restriction that buys something. The language is small on
purpose: a small language is one you can prove things about.
