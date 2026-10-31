# Distant-cloud clarity investigation

Development findings, 2026-10-30. These controls have not established a visual
fix for every cloud type, weather add-on or graphics driver.

The owner's X-Plane 12.4.4 installation was already using Maximum cloud quality.
Its local rendering table maps both High and Maximum to a cloud resolution
scale of 0.50, with a 1920 resolution cap. Maximum increases shadow sampling,
but does not raise that configured resolution scale above High. This is a
verified configuration limit, not a measurement of the actual cloud texture
extent in a captured frame. No simulator rendering-table asset is distributed.

Laminar describes the high-detail cloud rendering introduced in 12.2 as being
[around the camera](https://www.x-plane.com/2025/04/whats-new-in-x-plane-12-2-0/).
It also explains that lowering the scene input resolution
[trades source detail for performance](https://developer.x-plane.com/2022/09/x-plane-12-early-access-is-here/).
Together with the local mapping, this suggests that distant blockiness may
persist at Maximum and become more obvious with reduced input resolution.
It does not establish that the mod is uninvolved: motion estimation, temporal
reconstruction, NR detail enhancement and third-party cloud changes also need
comparison in the same scene.

## In-sim controls

- **Cloud quality:** Keep current, Low, Medium, High or Maximum. Applies only the
  simulator's existing `renopt_clouds_02` preference, after normal exit, using the
  same backup and selective restore as other graphics settings. Unknown mappings
  refuse the edit before changing preferences. Ordinary launches do not force
  the cloud slider back to the saved value.
- **Cloud clarity — Ultra Quality source:** select Maximum clouds, FXAA Off and
  the simulator's Ultra Quality input scale. Retains the current FSR family,
  output, MSAA, FG and NR selections. The result still uses upscaling.
- **Cloud clarity — Native AA source:** select Maximum clouds, FXAA Off and full
  input resolution with the current FSR family's Native AA. This supplies more
  scene detail and can have a substantial GPU cost. It is not cloud-only
  supersampling and does not promise to remove all blockiness.

Choose **Apply graphics after I close X-Plane**, close normally and restart with
the usual launcher. These are pending graphics changes; they do not apply to the
current flight. Existing weather, cloud coverage and add-on settings are retained.
The core `Resources/settings.txt` table and private cloud datarefs are not edited.

## Remaining live comparison

Use a fixed view, weather, time, output resolution and exposure. Start with NR
and FG Off; compare simulator native rendering, FSR at the same input scale,
Ultra Quality and Native AA. Let temporal histories settle, record source-frame
timings, then test NR and FG separately. Compare distant cloud edges as well as
aircraft/terrain intersections and moving views. Preserve HDR metadata or use a
consistent SDR conversion for review. Do not use unmatched screenshots to claim
an improvement or present these controls as a replacement cloud engine.
