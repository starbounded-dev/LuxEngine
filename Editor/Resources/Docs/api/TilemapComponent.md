# TilemapComponent

`public class TilemapComponent : Component`

A grid of tiles from a sprite sheet, in layers. A tile is the number of a frame of the tile set; -1 is an empty cell. Cell (0, 0) has its lower left corner at the entity's position. Changing tiles from a script also updates the solid-tile collision while the game plays.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `LayerCount` | `int` | get | How many layers the tilemap has (layers are numbered from 0, bottom first). |

## Methods

```
public Vector2 CellToWorld(int x, int y)
```

The center of a cell, in world space.

```
public void EraseTile(int x, int y, int layer = 0)
```

Empties a cell.

```
public int GetTile(int x, int y, int layer = 0)
```

The tile in a cell, or -1 when it is empty or the layer does not exist.

```
public void SetTile(int x, int y, int tile, int layer = 0)
```

Puts a tile in a cell. A negative tile empties the cell.

```
public void WorldToCell(Vector2 world, ref int x, ref int y)
```

The cell under a point in world space.

[All types](index)
