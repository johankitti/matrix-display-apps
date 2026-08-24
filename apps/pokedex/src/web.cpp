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
    // Uses web-core's shared page chrome (webPageHead) so styling — inputs,
    // full-width buttons, labels — matches the other apps and stays aligned.
    String h = webPageHead("Pokedex Display");
    h += F("<h1>&#128994; Pokedex Display</h1>"
           "<div class=st>Settings are saved on the device.</div>"
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
    h += F("<div class=hint>Also adjustable with the knob.</div>");

    h += "<label>Animation speed (" + String(g_settings.speedPct) + "%)</label>";
    h += "<input type=range name=spd min=" + String(ANIM_SPEED_MIN) +
         " max=" + String(ANIM_SPEED_MAX) + " step=5 value=" + String(g_settings.speedPct) +
         " oninput=\"this.previousElementSibling.textContent='Animation speed ('+this.value+'%)'\">";
    h += F("<div class=hint>100% = the sprites&rsquo; original speed.</div>");

    h += F("<button type=submit>Save</button></form>");

    h += sleepWebSection(g_settings.night);   // night schedule + timezone (shared)
    h += webActionForms();                    // Restart + Reconfigure Wi-Fi (shared)
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
