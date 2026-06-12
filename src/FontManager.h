#pragma once

class GfxRenderer;

// Get reference to global renderer (for font operations from other modules)
GfxRenderer& getGlobalRenderer();
