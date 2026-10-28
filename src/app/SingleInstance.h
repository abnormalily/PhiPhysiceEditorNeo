#pragma once
// Prevents a second editor window from opening in the same user session.
// Some OpenGL drivers crash inside the GL extension loader when a second
// process creates another context, so the second launch exits cleanly instead.

namespace ppn {
// Returns false when another instance is already running (a message is shown).
bool ensureSingleInstance();
} // namespace ppn
