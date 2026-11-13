#pragma once

namespace storm::render
{
// Where the renderer's log lines go (effects, textures, relief, the shim's own): once a sink is set, to it alone
// (the engine's logs, which also write logcat), `error` for the lines that report a failure ("ERROR: ..."); before
// that, or with none (tools, tests), to stderr and, on Android, logcat. Any thread may log: the sink must be thread safe.
using LogSink = void (*)(bool error, const char *line);
void SetLogSink(LogSink sink);
} // namespace storm::render
