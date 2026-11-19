# Particles

A **Particle System** releases many small quads (smoke, sparks, dust, rain) from an entity. Add one with **Create > Create Effects > Particle System**, or **Add Component > Particle System**. Everything about it is on the component, so it is saved in the scene, copied with the entity and kept in prefabs.

A system plays when the scene plays. Turn on **Preview In Editor** to watch it without pressing Play.

## Emission

- **Looping** and **Duration**: a looping system starts over when its duration ends; a one-shot system stops releasing at its duration and its particles live out their lifetime. **Prewarm** starts a looping system as if one whole duration had already played.
- **Rate**: particles released per second. **Bursts** release a number of particles at once at a time, repeated for a number of cycles. Up to four.
- **Max Particles**: a release past this many live particles is dropped. No system goes past 20 000.

## Shape and start values

**Shape** picks where particles start and which way they go: a **Cone** (angle from straight up, +Y), a **Sphere**, or a **Box**. **Lifetime**, **Speed**, **Size**, **Rotation** and **Angular Velocity** are each a minimum and a maximum; every particle takes a random value in between. **Start Color** is multiplied by the color over life; values above 1 glow when Bloom is on.

## Motion

**Gravity** is in units per second squared (a positive Y rises, like smoke). **Drag** slows particles down. With **World Space** on, particles stay where they were released when the entity moves; off, they travel with it.

## Over life

**Size**, **Speed** and **Color** change from a particle's birth to its death. By default the color fades out. The Particle Editor panel draws these as curves and gradients with up to eight keys.

## Texture sheet

A texture with **Tiles X** by **Tiles Y** frames (frame 0 is the top left) plays over each particle's life (**Cycles** times) or picks a random frame per particle.

## Renderer

- **Render Mode**: **Billboard (3D)** draws camera-facing quads in the 3D scene; **Sprite (2D)** draws sprites in the 2D draw order, so **Sorting Layer** and **Order In Layer** apply.
- **Blend**: Alpha, Additive or Premultiplied. 2D sprites always blend with alpha.
- **Soft Distance** (3D): particles fade out over this distance where they meet geometry, so there is no hard line.
- **Seed**: the same seed gives the same particles every time.
- **Play On Awake**: off, nothing is released until a script plays the system.

3D particles are drawn after transparent meshes and do not sort against them: a particle behind a glass pane draws in front of it.
