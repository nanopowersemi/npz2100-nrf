#ifndef __NPZ2100_REGMAP
#define __NPZ2100_REGMAP

// nPZ2100 Register Map

static const unsigned char npz2100_regmap[] = {
  // ---- Global configuration registers ----
  10,         // Length
  0x05,       // Start address (IOCFG1 register)
  0x00, // IOCFG1 = 0x00
  0x00, // IOCFG2 = 0x00
  0xFF, // IOCFG3 = 0xFF
  0x00, // IOCFG4 = 0x00
  0x1D, // IOCFG5 = 0x1D
  0x00, // SYSCFG1 = 0x00
  0x00, // SYSCFG2 = 0x00
  0xFF, 0x01, // (TOUT_L/TOUT_H)
};

#endif // __NPZ2100_REGMAP
