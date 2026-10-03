# i2s_audio (full duplex)

ESPHome's `i2s_audio` with full duplex support, so our ES8311 codec's DAC and
ADC can share one I2S port (common WS/BCLK/MCLK, separate DOUT/DIN) on Ball V2
and Ball V3.

Vendored from ApolloAutomation/CAST_PRO-1 (commit `550cbba`, 2026-09-30,
`Integrations/ESPHome/components/i2s_audio/`), excluding their SPDIF-specific
files (`speaker/i2s_audio_spdif.cpp/h`, `speaker/spdif_encoder.cpp/h`) which we
don't use (`spdif_mode` is never set in our device configs).

CAST_PRO-1's own README describes their work as based on
esphome/esphome#16882 (head `3db4594`), with these fixes on top:

- The speaker joins a TX channel the microphone already started without
  skewing its playback timestamps (Sendspin sync), and realigns its write
  position each session since TX is never reset.
- The DMA ring is fixed at the speaker's 5 x 10 ms, whichever side allocates
  first, so stall tolerance doesn't depend on start order.
- TX always auto-clears, so the DAC plays silence rather than looping the last
  buffer when the microphone allocated the channels first.
- Both channels get both data pins, whichever side initializes first.
- Mismatched speaker/microphone formats are rejected at config time and at
  runtime, instead of playing at the wrong speed.
- Plus several general (non-duplex-specific) speaker-task correctness fixes:
  a start/stop race that could silently drop a pending start, a duplicate
  speaker-task guard, a `weak_ptr` TOCTOU bug in `has_buffered_data()`, and
  on-the-fly bit-depth narrowing support.

Drop this override once full duplex lands upstream.
