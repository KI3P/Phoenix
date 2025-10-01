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
void InitializeDisplay(void);

void DrawVFOPanes(void);
void DrawFreqBandModPane(void);
void DrawSpectrumPane(void);
void DrawWaterfallPane(void);
void DrawStateOfHealthPane(void);
void DrawTimePane(void);
void DrawSWRPane(void);
void DrawTXRXStatusPane(void);
void DrawSMeterPane(void);
void DrawAudioSpectrumPane(void);
void DrawSettingsPane(void);
void DrawNameBadgePane(void);

#endif
