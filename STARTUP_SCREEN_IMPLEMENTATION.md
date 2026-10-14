# Startup Screen and Logo Implementation

## Features Implemented

### 1. Logo Integration
- **Logo Texture Loading**: The `bear_logo.ico` file is now loaded as a DirectX9 texture during renderer initialization
- **Menu Logo Display**: The bear logo is displayed to the left of the "BearWare" title in the main menu
- **Orange Accent Tinting**: The logo is tinted with the orange accent color (#B45F1E) to match the theme
- **Fallback System**: If the ICO file fails to load, a simple orange circle is displayed as a fallback

### 2. Startup Screen
- **Fade Animation**: 3-second startup sequence with smooth fade in/out effects:
  - 0.8 seconds: Fade in
  - 1.4 seconds: Hold at full opacity
  - 0.8 seconds: Fade out
- **Centered Logo**: Large (128x128) bear logo displayed in the center of the screen
- **Orange Tinting**: Logo is tinted with the orange accent color during startup
- **Title Display**: "BearWare" text appears below the logo
- **Background Overlay**: Semi-transparent dark background for better visibility

### 3. Technical Implementation

#### New Renderer Functions:
- `draw_texture()`: Renders DirectX9 textures with color tinting and alpha blending
- `draw_startup_screen()`: Handles the complete startup animation sequence

#### New Renderer Variables:
- `pLogoTexture`: DirectX9 texture for the bear logo
- `show_startup`: Boolean to control startup screen visibility
- `startup_begin`: Timestamp for animation timing
- `startup_alpha`: Current alpha value for fade effects

#### File Modifications:
- **renderer.h**: Added texture support and startup screen declarations
- **renderer.cpp**: Implemented texture loading, drawing functions, and startup animation
- **esp.cpp**: Added startup screen rendering and menu logo display

### 4. Logo Placement
- **Menu Position**: 32x32 pixels at coordinates (menu_x + 16, menu_y + 6)
- **Startup Position**: 128x128 pixels centered on screen
- **Color**: Orange accent color (#B45F1E) applied as tint
- **Alpha Blending**: Smooth transparency effects during fade animations

### 5. File Requirements
- **bear_logo.ico**: Must be placed in the same directory as the executable
- **DirectX9 Support**: Requires DirectX9 with D3DX extensions for texture loading

## Usage
1. Launch the executable
2. Bear logo appears with orange tint and fades in/out over 3 seconds
3. Main application loads with logo visible in menu header
4. Logo maintains orange accent color matching the current theme

## Error Handling
- If `bear_logo.ico` fails to load, fallback orange circles are displayed
- Startup screen automatically transitions to main application after 3 seconds
- All DirectX resources are properly cleaned up on exit

The implementation provides a professional startup experience while maintaining the orange branding theme throughout the application.