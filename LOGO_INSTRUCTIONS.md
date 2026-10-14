# Logo Integration Instructions

## Changes Made

The following modifications have been made to the external cheat base:

1. **Removed minimize functionality**: 
   - The minimize button has been completely removed
   - The menu is now always expanded and cannot be minimized
   - Removed `menu_minimized` variable and related logic

2. **Updated menu text**:
   - The BearWare title now has 40 pixels of space reserved on the left for a logo
   - The subtitle now always displays "CS2 EXTERNAL UNDETECTED + STREAMABLE"
   - Text positioning has been adjusted to accommodate the logo space

3. **Logo placement area**:
   - A 40px wide area is reserved to the left of the "BearWare" title
   - Logo should be positioned at approximately:
     - X: `menu_x + PAD` (around 52px from menu left edge)
     - Y: `menu_y + 8.f` (around 8px from menu top edge)
     - Maximum recommended size: 32x32 pixels

## To Add Your Logo

1. Place your `.ico` file in the project directory
2. Add DirectX texture loading code in the renderer initialization
3. Draw the logo in the reserved space using `renderer::draw` functions
4. The logo should be loaded once during initialization and drawn each frame

## Files Modified

- `external-cheat-base/cheats/esp.cpp` - Main UI rendering logic
- `external-cheat-base/cheats/esp.h` - Header file (removed menu_minimized variable)
- `external-cheat-base/cheats/config.cpp` - Config save/load (removed menu_minimized handling)

The project has been successfully compiled and is ready for use with these modifications.