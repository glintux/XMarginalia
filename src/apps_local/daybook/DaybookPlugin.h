// Daybook's page in the CrossPoint web UI: a browser plugin that lives on the
// card.
//
// "Install" writes three small files to the plugin folder (manifest, readme,
// script). The script is the whole page; it reads and writes the same
// /.crosspoint/calendar files the app does. Nothing here runs a server.
#pragma once

#include <string>

namespace daybook::plugin {

enum class State { Missing, Outdated, Current };

// Whether the plugin folder on the card holds this build's copy.
State state();

// Writes (or rewrites) the plugin. Returns "" on success, or a short reason it
// failed.
std::string install();

// Settings > Connectivity > Plugins. The web UI only lists plugins while it is
// on.
bool pluginsEnabled();

}  // namespace daybook::plugin
