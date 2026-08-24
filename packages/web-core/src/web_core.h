#pragma once
// =============================================================================
//  web-core — shared settings-server shell for the matrix-display apps
//
//  Every app serves a tiny settings page over Wi-Fi at http://<hostname>.local/.
//  The page contents differ (golfers vs. slideshow vs. bus stop), but the shell
//  is identical: one WebServer on :80, mDNS, an HTML head with shared styling,
//  and the two universal actions — Restart and Reconfigure Wi-Fi.
//
//  Usage: register your app routes on webServer(), then call webCoreBegin():
//      webServer().on("/",     HTTP_GET,  handleRoot);
//      webServer().on("/save", HTTP_POST, handleSave);
//      webCoreBegin(HOSTNAME);              // adds /restart + /wifi, starts mDNS
//  and pump webCoreHandle() from loop().
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>

// The shared HTTP server (port 80). Register app-specific routes on it before
// calling webCoreBegin().
WebServer& webServer();

// Start mDNS (http://<hostname>.local/), register the common /restart and /wifi
// handlers, and begin serving. Call once, after registering app routes.
void webCoreBegin(const char* hostname);

// Service pending requests — call often from loop().
void webCoreHandle();

// ---- HTML building blocks (shared look, so every app's page matches) ---------
// Full page head: doctype, viewport meta, <title>, and the shared <style>.
String webPageHead(const char* title);

// A status chrome line: "IP x.x.x.x · NN dBm · up MM min". Wrap/prepend your own
// app status around it as you like.
String webStatusChrome();

// The two universal action forms (Restart, Reconfigure Wi-Fi). Append to a page.
String webActionForms();

// HTML-escape a C string for safe interpolation into a page.
String webEsc(const char* s);
