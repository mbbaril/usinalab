// SPDX-License-Identifier: GPL-3.0-only
let plugin, core, last = -Infinity;
export async function tactilePulse() {
  if (!window.Capacitor?.isNativePlatform() || performance.now() - last < 35) return;
  last = performance.now();
  try {
    core ??= import('./capacitor-core.js');
    const {registerPlugin}=await core;
    // A Capacitor proxy must not become a Promise result: its "then" property is a native method proxy.
    plugin ??= registerPlugin('UsinaHaptics');
    await plugin.pulse();
  } catch { /* A device without haptics remains playable. No fallback vibration loop. */ }
}
