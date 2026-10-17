// Vibrium startup — runs after the first window opens.
export function onStartup() {
  // Nothing destructive here. Placeholder for future Vibrium features
  // (e.g. native adblock hooks, custom new-tab, update checks).
  console.log("[Vibrium] startup complete");
}

export function onShutdown() {
  console.log("[Vibrium] shutdown");
}