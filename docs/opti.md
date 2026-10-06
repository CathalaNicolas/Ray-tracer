# Possible optimizations

Nothing is queued. The costs that were listed here are now skipped when the inputs have not changed.

- Asset write times are checked at most four times a second.
- A settings key row reads the keyboard once per frame.
- Outliner labels are kept until the name or kind changes.
- Prefab sync skips the field copy when the source fingerprint matches.
- A new mesh stamp drops older geometry-cache entries for that path.
- Each image path is statted once per render. Data textures skip the upload when a 64-bit mix and the texel count match. Material rows and shadow maps stay when only the camera changed.

Undo already shares mesh triangles. `Scene::clone` copies each object, and `Mesh::clone` copies the `shared_ptr` to `MeshGeometry`. The snapshot still copies the object list and materials once, when an edit starts.
