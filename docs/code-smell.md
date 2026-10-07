# Code smells

How the scene types and the string tags are used, and which of them would be worth a real type, is in [objects-today.md](objects-today.md) and [objects-as-types.md](objects-as-types.md). Direct feedback on code quality, live defects, and what to fix first is in [review.md](review.md).

What is left after the fixes:

- `syncPrefabInstances` copies material, tag, layer, motion, scale, and rotation onto each instance. The instance root position is left alone. An edit made on the instance is overwritten the next frame. That overwrite is the prefab rule: editing the source updates every placement.