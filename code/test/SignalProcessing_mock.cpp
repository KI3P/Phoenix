#include "../src/PhoenixSketch/SDT.h"
#include "SignalProcessing_mock.h"

#include "mock_R_data_int.c"
#include "mock_L_data_int.c"
#include "mock_L_data_int_1khz.c"
#include "mock_R_data_int_1khz.c"

extern float32_t *FFT_spec_old;
int64_t tstart;

void ZeroOldSpec(void){
    for (size_t i = i; i<SPECTRUM_RES; i++){
        FFT_spec_old[i] = 0.0;
    }
}

int AudioRecordQueue::available(void) {
    int blocks_available = 4*2048/BUFFER_SIZE;
    return (blocks_available-head+1)*BUFFER_SIZE;
}

void AudioRecordQueue::clear(void) {
    head = 0;
}

void AudioRecordQueue::setChannel(uint8_t chan) {
    channel = chan;
    if (chan == 1) {
        data = R_mock;
    }
    if (chan == 0) {
        data = L_mock;
    }

    if (chan == 3) {
        data = R_mock_1khz;
    }
    if (chan == 2) {
        data = L_mock_1khz;
    }

}

uint8_t AudioRecordQueue::getChannel(void) {
    return channel;
}

int16_t* AudioRecordQueue::readBuffer(void) {
    int16_t * ptr = &data[head*BUFFER_SIZE];
    head += 1;
    int blocks_available = 4*2048/BUFFER_SIZE;
    if (head == (blocks_available+1)) head = 0;
    return ptr;
}

void AudioRecordQueue::freeBuffer(void) {
    return;
}

void AudioRecordQueue::update(void) {
    return;
}

int16_t *AudioPlayQueue::getBuffer(void){
    return buf;
}

void AudioPlayQueue::setName(char *fn){
    if (fn != nullptr) fle = fopen(fn, "w");
    else fle = nullptr;
    fopened = (fle != nullptr);
}

void AudioPlayQueue::playBuffer(void){
    if (fopened){
        for (size_t k=0; k<128; k++){
            fprintf(fle,"%d\n",buf[k]);
        }
    }
    return;
}

void StartMillis(void){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    tstart = (int64_t)(1000 * tv.tv_sec) + (int64_t)((float32_t)tv.tv_usec/1000);
}

void AddMillisTime(uint64_t delta_ms){
    tstart -= delta_ms;
}

int64_t millis(void){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)(1000 * tv.tv_sec) + (int64_t)((float32_t)tv.tv_usec/1000) - tstart;
}

void SetMillisTime(uint64_t time_ms){
    tstart = millis()+tstart - time_ms;
}

void arm_float_to_q15(
  float32_t * pSrc,
  q15_t * pDst,
  uint32_t blockSize)
{
    for (size_t k = 0; k < blockSize; k++){
        pDst[k] = (q15_t)(pSrc[k]*32768);
    }
}

