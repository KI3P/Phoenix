#ifndef DISPLAY_H
#define DISPLAY_H

struct dispSc {
  const char *dbText;
  float32_t dBScale;
  uint16_t pixelsPerDB;
  uint16_t baseOffset;
  float32_t offsetIncrement;
};
//extern struct dispSc displayScale[];

void DrawDisplay(void);

#endif
