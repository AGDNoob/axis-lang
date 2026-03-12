# Fields

Fields are custom data types that group related values. They're similar to structs in C, but without methods.

## Defining a Field

```axis
Vec2: field:
    x: i32 = 0
    y: i32 = 0
```

## Using Fields

```axis
pos: Vec2
pos.x = 100
pos.y = 200
writeln(pos.x)    # 100
```

Fields are zero-initialized by default (using the default values from the definition).

## Nested Fields

Fields can contain other fields:

```axis
Vec2: field:
    x: i32 = 0
    y: i32 = 0

Player: field:
    name: str = ""
    position: Vec2

p: Player
p.name = "Alice"
p.position.x = 50
p.position.y = 100
```

## Inline Anonymous Fields

You can define fields inline without creating a separate type:

```axis
Game: field:
    home: field:
        name: str = ""
        score: i32 = 0
    away: field:
        name: str = ""
        score: i32 = 0

g: Game
g.home.name = "Team A"
g.home.score = 3
g.away.name = "Team B"
g.away.score = 1
```

## Arrays of Fields

```axis
players: (Player; 5)
players[0].name = "Bob"
players[0].position.x = 10
players[1].name = "Charlie"
```

Inline fields in arrays:

```axis
team: field:
    members: (field; 11): [
        name: str = ""
        number: i32 = 0
    ]

t: team
t.members[0].name = "Alice"
t.members[0].number = 10
```

## Fields with `update`

Pass fields to functions with `update` to modify them:

```axis
func move(update pos: Vec2, dx: i32, dy: i32):
    pos.x = pos.x + dx
    pos.y = pos.y + dy

myPos: Vec2
move(myPos, 10, 20)
writeln(myPos.x)    # 10
writeln(myPos.y)    # 20
```

Without `update`, the function would only modify a local copy.

## Copying Fields

Use `copy` for explicit deep copies:

```axis
original: Vec2
original.x = 100
original.y = 200

duplicate: Vec2 = copy original
duplicate.x = 999
writeln(original.x)    # still 100
```

## Why No Methods?

AXIS is value-oriented, not object-oriented. Data and functions are separate. Instead of `player.move(10, 20)`, you write `move(player, 10, 20)` (or `move(update player, 10, 20)` if you want to modify it). This keeps the language simple and makes side effects explicit.

## Next

[Enums and Match](07-enums-and-match.md)
