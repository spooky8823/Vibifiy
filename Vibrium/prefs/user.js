// Vibrium: Low-End Device Optimizations
// Aggressive process/memory caps and service removal

// === PROCESS & MEMORY (Biggest wins on 2GB-4GB RAM) ===
user_pref("dom.ipc.processCount", 1);           // Force single content process
user_pref("dom.ipc.processCount.webIsolated", 1);
user_pref("browser.tabs.unloadOnLowMemory", true);
user_pref("browser.low_commit_space_threshold_mb", 200);
user_pref("browser.tabs.min_inactive_duration_before_unload", 300000); // 5 min

// === TELEMETRY & BACKGROUND SERVICES (Kill everything) ===
user_pref("datareporting.healthreport.uploadEnabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.enabled", false);
user_pref("toolkit.telemetry.unified", false);
user_pref("toolkit.telemetry.archive.enabled", false);
user_pref("browser.ping-centre.telemetry", false);
user_pref("app.normandy.enabled", false);
user_pref("app.shield.optoutstudies.enabled", false);
user_pref("browser.discovery.enabled", false);
user_pref("extensions.pocket.enabled", false);
user_pref("browser.newtabpage.activity-stream.feeds.telemetry", false);
user_pref("browser.newtabpage.activity-stream.telemetry", false);
user_pref("browser.newtabpage.activity-stream.showSponsored", false);
user_pref("browser.newtabpage.activity-stream.showSponsoredTopSites", false);

// === UI/ANIMATION KILLS (Perceived speed) ===
user_pref("browser.tabs.animate", false);
user_pref("browser.panorama.animate_zoom", false);
user_pref("toolkit.cosmeticAnimations.enabled", false);
user_pref("browser.fullscreen.animate", false);

// === NETWORK & DISK ===
user_pref("network.http.max-connections", 48);  // Lower than default 900
user_pref("network.http.max-persistent-connections-per-server", 4);
user_pref("browser.cache.disk.capacity", 51200); // 50MB (default is much higher)
user_pref("browser.sessionstore.interval", 30000); // Less frequent writes

// === MISC ===
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");