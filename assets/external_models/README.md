# External House Models

Drop external `.obj` files in this folder to replace the procedural fallback houses.

## Expected file names

The scene auto-detects these names:

- SpongeBob house: `spongebob_house.obj` or `SBH.obj` or `pineapple_house.obj`
- Squidward house: `squidward_house.obj` or `STH.obj` or `squid_house_3.obj`
- Patrick house: `patrick_house.obj` or `PSH.obj` or `patrick_rock.obj`

## Sources you can use

- CGTrader Conch Street (paid, all 3 houses in one scene)
- CGTrader/TurboSquid single-house assets (`OBJ` preferred)
- Cults3D house assets (`OBJ`) after login/download

## Notes

- Keep texture files (if any) next to the `.obj` and `.mtl`.
- The code normalizes size and centers meshes automatically.
- If a model is not found, the scene falls back to the procedural version.
