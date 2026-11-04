# Physics Layers

Physics layers decide which 3D bodies collide with which. Bullets that pass through their shooter, a ghost that walks through walls but stands on the floor, debris that does not block the player: each is a layer that skips some others.

## Setting up layers

Open **Project Settings → Physics** and click **Add Physics Layer**. Each layer has:

- **Name** — how other layers refer to it.
- **Collides With Self** — whether two bodies on this layer collide with each other.
- **Collides With** — the other layers it collides with, as names separated by commas (for example `Default, Enemies`).

Two bodies on different layers collide when **either** layer lists the other. So to stop `Bullets` hitting `Player`, neither may list the other.

A project without layers has a single layer, **Default**, where everything collides. Once you add layers, the first one in the list plays that role.

## Putting a body on a layer

**Rigidbody** and **Character Controller** have a **Layer ID**: the layer's position in the list, starting at 0 for the first layer. A collider without a Rigidbody is on layer 0, as is a body whose Layer ID is past the end of the list.

Layers are read when physics starts, so changes take effect the next time you press **Play** or **Simulate**.

## Example

| Layer ID | Name | Collides With Self | Collides With |
|---|---|---|---|
| 0 | Default | on | Player, Enemies |
| 1 | Player | off | Enemies |
| 2 | Enemies | on | |
| 3 | Bullets | off | Default, Enemies |

The player collides with the world and with enemies; bullets hit the world and enemies but not the player or each other.

Layers apply to 3D physics. 2D bodies (Rigidbody 2D) always collide with each other.
