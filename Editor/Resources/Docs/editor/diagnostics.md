# Inspecting CPU and GPU timings

Open **Profiler** or **Renderer Debugger** from **View → Panels → Diagnostics**, the command
palette, or the Debugging workspace. Both history charts use CPU in lime, GPU in orange and
milliseconds on the vertical axis. The horizontal axis counts samples recorded while the panel
is visible; it does not measure seconds.

Profiler's CPU series is **main-thread work**. Renderer Debugger's CPU series is the **scene CPU
pass sum**. Their GPU series is the latest published scene command-buffer timing. These are
different scopes, can arrive at different times, and are not interchangeable with whole-frame
duration or FPS. The chart caption identifies its source.

- **Pause history** freezes the recorded samples. Other panel metrics remain live.
- **Resume history** records again and follows the newest samples.
- **Follow live** moves the range with the incoming samples. Pan, wheel-zoom or box-select inside
  the chart to leave follow mode and inspect a range. Recording continues until you pause; the
  latest 240 samples are retained.
- **Fit** restores the full retained history and its scale. Use Follow live to resume automatic
  range updates.
- Click **CPU**, **GPU** or **Budget** in the legend to hide or show that series. Hover the chart
  to read the nearest sample's values.
- **More** chooses a 30, 60, 120 or 144 fps budget reference. The caption shows its value in ms.

GPU timing that is unavailable, pending, nonfinite or nonpositive appears as a gap, with an explicit
message for the latest sample. It is not drawn as a measured zero-cost frame. Dist builds may have
no GPU timer-query data.

Renderer Debugger's **Profiling** tab also shows GPU time by pass. **Freeze passes** retains a
separate snapshot, including its names and values, for inspection; **Resume passes** updates it
again. **Fit** resets that chart's range. Narrow panels show full pass names above horizontal value
bars. Pass charts include completed positive GPU measurements only. Closing either diagnostic
panel stops its history sampling; changing its renderer context clears the previous history.
