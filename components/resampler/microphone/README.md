# resampler microphone platform

New platform added by esphome/esphome#16882 (commit `3db4594`), not present in
mainline ESPHome. Lets a `microphone:` entry pull one or more channels out of
another microphone source and resample/downmix them — we use it to take
channel 0 off the duplex i2s_audio stereo capture and resample it to the
16 kHz mono `micro_wake_word` needs (`voice_microphone` in our device
configs).

Vendored unmodified at this commit. `resampler/__init__.py` and
`resampler/speaker/` next to this directory are unmodified current mainline
ESPHome (the pre-existing speaker-side resampler platform we already used for
`announcement_resampling_speaker`/`media_resampling_speaker`) — only this
`microphone/` subdirectory is new.

Drop this once full duplex (and this platform) lands upstream.
