LANTERN GPU - MILESTONE 4: SLEEP MODE (includes milestone 3: faster drawing + smooth lighting) with game v0.33
=============================================================================================================

ON THE PC (the laptop with Quartus)
1. Unzip this folder anywhere, for example Downloads\LanternGPU_M3.
2. Double-click BUILD_LANTERN_CORE.bat (about 10 minutes).
3. Copy the Cores, Platforms and Assets folders from SD to the root of the Pocket's SD card
   and let them replace the old files. (Your heroes and settings are not touched.)

WHAT CHANGED IN THE CORE
- Sleep mode: press the power button during play to sleep; wake it and you are back exactly where you
  were (same depth, same monsters). Memories (save states) in the Pocket menu work too.
- The drawing engine reads its art faster (one read every 70 ns instead of every 120 ns),
  so big scenes - town, lots of trees - should hold 60 fps.
- Smooth lighting is done by the hardware: light fades across every floor tile and wall
  with no extra cost.

WHAT CHANGED IN THE GAME (v0.33)
- Trees and buildings: the empty space around them is skipped.
- Smooth lighting and blended ground edges are back (dirt spilling over flagstones,
  grass creeping onto the lanes).
- Effects HIGH: the hero's lantern, glowing embers, spells lighting up walls and floor
  as they fly past, flashes of light where blows land (bigger on criticals),
  trees swaying in the wind in town, drifting fog banks from depth 3 down.
- All of this can be switched in Start > System > Graphics.

PLEASE CHECK
- Sleep in the middle of a dungeon fight, wake up: you should be in the same spot.
- Start > System > Graphics > Show FPS: ON, then look at town (near trees) and the dungeon.
- The test screen still comes first (START to play): all nine tiles should look as before.
  If sprites look scrambled or sparkly after this update, tell Claude - the faster memory
  timing can be relaxed with a one-line change.
