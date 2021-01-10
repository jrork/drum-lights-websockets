enum drumID {
  Bass1,
  Bass2,
  Bass3,
  Bass4, 
  Bass5,
  Bass6,
  Bass7,
  Bass8,
  Snare1,
  Snare2,
  Snare3,
  Snare4,
  Snare5,
  Snare6,
  Snare7,
  Snare8,
  Snare9,
  Snare10,
  KitSnare,
  KitHighTom,
  KitLowTom,
  KitFloorTom,
  KitKick
};

struct drumLight {
  drumID drumId;
  uint32_t color;
  uint8_t brightness;
  uint32_t threshold;
  uint8_t delayValue;
  uint8_t triggerMode;
};
