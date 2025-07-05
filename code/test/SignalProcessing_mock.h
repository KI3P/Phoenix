#ifndef SIGNALPROCESSINGMOCK_H
#define SIGNALPROCESSINGMOCK_H

#include <stdint.h>
#include <sys/time.h>

typedef float float32_t;
#define AudioInterrupts()

int64_t millis(void);
void StartMillis(void);
void AddMillisTime(uint64_t delta_ms);
void SetMillisTime(uint64_t time_ms);

void ZeroOldSpec(void);
class AudioRecordQueue
{
    public:
        AudioRecordQueue(void){ }
        void begin(void) {
            clear();
            enabled = 1;
        }
        int available(void);
        void setChannel(uint8_t);
        uint8_t getChannel(void);
        void clear(void);
        int16_t * readBuffer(void);
        void freeBuffer(void);
        void end(void) {
            enabled = 0;
        }
        virtual void update(void);
    private:
        volatile uint8_t channel, enabled;
        volatile uint32_t head;
        int16_t *data;
};

class AudioPlayQueue
{
    public:
        void begin(void){
            fopened = false;
        }
        void end(void){
            fclose(fle);
        }
        int16_t *getBuffer(void);
        void playBuffer(void);
        void setName(char *fn);
    private:
        int16_t buf[128];
        FILE *fle;
        bool fopened;
};

#endif // SIGNALPROCESSINGMOCK_H