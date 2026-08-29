// Game Configuration Constants

const GameConfig = {
  // Display dimensions
  GAME_WIDTH: 640,
  GAME_HEIGHT: 840,

  // Gameplay constants
  GAME_SPEED: 4,
  PLAYER_SPEED: 3,  // Player car movement speed (slower than game speed for better control)
  LANES: 3,

  // Difficulty settings
  INITIAL_ENEMY_SPEED: 2,
  MAX_ENEMY_SPEED: 9,
  MAX_ENEMY_MS_TO_RELEASE: 250,
  INITIAL_MS_TO_RELEASE: 500,

  // Difficulty progression
  SPEED_INCREASE_INTERVAL: 15,  // Every 15 cars evaded
  SPAWN_RATE_INCREASE_INTERVAL: 30,  // Every 30 cars evaded
  SPAWN_RATE_DECREASE: 50,  // Decrease spawn time by 50ms

  // Colors
  BACKGROUND_COLOR: 0x404040,
  ROAD_COLOR: 0x8a8a8a,

  // Rendering
  RESOLUTION: 2,

  // Collision detection
  COLLISION_MARGIN: 10,

  // Near miss bonus
  NEAR_MISS_DISTANCE: 45,  // Distance threshold for near miss
  NEAR_MISS_BONUS: 5,  // Bonus points for near miss

  // Power-ups
  SHIELD_DURATION: 5000,  // Shield lasts 5 seconds (in milliseconds)
  SHIELD_SPAWN_CHANCE: 0.6,  // 60% chance to spawn shield when car spawns
  SLOWMO_DURATION: 8000,  // Slow motion lasts 8 seconds (in milliseconds)
  SLOWMO_SPAWN_CHANCE: 0.2,  // 20% chance to spawn slow-mo when car spawns
  SLOWMO_MULTIPLIER: 0.5,  // Enemies move at 50% speed during slow-mo
  POWERUP_START_THRESHOLD: 10,  // Power-ups start spawning after 10 cars evaded
};

// Key constants
const Keys = {
  ARROW_LEFT: 'ArrowLeft',
  ARROW_UP: 'ArrowUp',
  ARROW_RIGHT: 'ArrowRight',
  ARROW_DOWN: 'ArrowDown',
  SPACE: 'Space',
  KEY_B: 'KeyB',
  KEY_M: 'KeyM',
};
