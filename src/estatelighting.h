#pragma once
// Relight the estate's legacy baked structural meshes at their atomic draw.
// Texture/material colours and vertex alpha are preserved; shared geometry is restored.
// Includes the game's current room/scene point-light producer, not only RpWorld lights.
namespace estatelighting { bool Install(); }
