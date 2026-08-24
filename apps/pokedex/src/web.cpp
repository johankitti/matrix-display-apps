#include "web.h"
#include "settings.h"
#include "display.h"
#include "config.h"

#include <web_core.h>   // shared WebServer + mDNS + Restart/Wi-Fi handlers

// A <select> with the two given options; `sel` picks which is preselected.
static String selOne(const char* val, const char* label, bool sel) {
    String o = "<option value='"; o += val; o += "'";
    if (sel) o += " selected";
    o += ">"; o += label; o += "</option>";
    return o;
}

static String settingsPage() {
    String h = F(
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Pokedex Display</title><style>"
        "body{font-family:system-ui,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;color:#222}"
        "h2{margin-bottom:4px}label{display:block;margin:16px 0 4px;font-weight:600}"
        "input,select{width:100%;padding:9px;font-size:16px;box-sizing:border-box}"
        "button{margin-top:22px;padding:11px 18px;font-size:16px;border:0;border-radius:6px;"
        "background:#cc0000;color:#fff}small{color:#666}</style></head><body>"
        "<h2>Pokedex Display</h2><small>Settings are saved on the device.</small>"
        "<form method=POST action=/save>");

    h += F("<label>Seconds per slide</label>");
    h += "<input type=number name=dur min=" + String(DURATION_MIN_SEC) +
         " max=" + String(DURATION_MAX_SEC) + " value=" + String(g_settings.durationSec) + ">";

    h += F("<label>Order</label><select name=order>");
    h += selOne("random", "Random", g_settings.randomOrder);
    h += selOne("seq", "In order (1 &rarr; last)", !g_settings.randomOrder);
    h += F("</select>");

    h += F("<label>Animation</label><select name=anim>");
    h += selOne("full",   "Animated (full dex, static where none)", g_settings.animMode == ANIM_MODE_FULL);
    h += selOne("only",   "Animated only (Pok&eacute;mon with animation)", g_settings.animMode == ANIM_MODE_ONLY);
    h += selOne("static", "Static sprites only", g_settings.animMode == ANIM_MODE_STATIC);
    h += F("</select>");

    h += "<label>Brightness (" + String(g_settings.brightness) + ")</label>";
    h += "<input type=range name=bri min=" + String(BRIGHTNESS_MIN) +
         " max=" + String(BRIGHTNESS_MAX) + " value=" + String(g_settings.brightness) +
         " oninput=\"this.previousElementSibling.textContent='Brightness ('+this.value+')'\">";
    h += F("<small>Also adjustable with the knob.</small>");

    h += "<label>Animation speed (" + String(g_settings.speedPct) + "%)</label>";
    h += "<input type=range name=spd min=" + String(ANIM_SPEED_MIN) +
         " max=" + String(ANIM_SPEED_MAX) + " step=5 value=" + String(g_settings.speedPct) +
         " oninput=\"this.previousElementSibling.textContent='Animation speed ('+this.value+'%)'\">";
    h += F("<small>100% = the sprites' original speed.</small>");

    h += F("<button type=submit>Save</button></form>");

    // Night schedule + timezone (shared sleep-core section; posts to /night).
    h += sleepWebSection(g_settings.night);

    // Separate form so a mis-click can't wipe Wi-Fi while saving settings.
    // Posts to web-core's shared /wifi handler (erase creds + reboot into setup).
    h += F("<hr><form method=POST action=/wifi "
           "onsubmit=\"return confirm('Erase saved Wi-Fi and restart? You will need "
           "to rejoin the Pokedex-Setup network to reconfigure.')\">"
           "<button type=submit style='background:#555'>Reconfigure Wi-Fi</button>"
           "<small>Clears the saved network and reboots into setup.</small>"
           "</form></body></html>");
    return h;
}

static void handleRoot() { webServer().send(200, "text/html", settingsPage()); }

static void handleSave() {
    WebServer& server = webServer();
    if (server.hasArg("dur"))
        g_settings.durationSec = server.arg("dur").toInt();     // clamped in settingsSave()
    if (server.hasArg("order"))
        g_settings.randomOrder = (server.arg("order") != "seq");
    if (server.hasArg("anim")) {
        String a = server.arg("anim");
        g_settings.animMode = (a == "static") ? ANIM_MODE_STATIC
                            : (a == "only")   ? ANIM_MODE_ONLY
                                              : ANIM_MODE_FULL;
    }
    if (server.hasArg("bri"))
        g_settings.brightness = constrain(server.arg("bri").toInt(), BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    if (server.hasArg("spd"))
        g_settings.speedPct = constrain(server.arg("spd").toInt(), ANIM_SPEED_MIN, ANIM_SPEED_MAX);
    settingsSave();
    displaySetBrightness(g_settings.brightness);   // apply immediately
    displaySetSpeed(g_settings.speedPct);
    Serial.printf("[web] saved dur=%d order=%s anim=%u bri=%u spd=%u\n", g_settings.durationSec,
                  g_settings.randomOrder ? "random" : "seq", g_settings.animMode,
                  g_settings.brightness, g_settings.speedPct);
    server.sendHeader("Location", "/");
    server.send(303);   // See Other -> reload the form with fresh values
}

// The Wi-Fi reset ("Reconfigure Wi-Fi") action is web-core's shared /wifi
// handler now — it erases the saved creds and reboots into setup, same as the
// app's old /wifireset. The page's form (above) posts straight to it.

void webInit() {
    webServer().on("/", handleRoot);
    webServer().on("/save", HTTP_POST, handleSave);
    sleepWebRegister(&g_settings.night, settingsSave);   // POST /night (shared)
    webServer().onNotFound(handleRoot);   // captive-style: any path shows settings
    webCoreBegin(HOSTNAME);   // mDNS + /restart + /wifi + server.begin()
}

void webTick() { webCoreHandle(); }
