"""Generate the raw 24 kHz stereo PCM used for the UI button sound."""

from __future__ import annotations

import math
import struct
from pathlib import Path


SAMPLE_RATE = 24_000
CHANNELS = 2
DURATION_SECONDS = 0.12


def note(time_s: float, start_s: float, duration_s: float, frequency_hz: float) -> float:
    local_time = time_s - start_s
    if local_time < 0.0 or local_time >= duration_s:
        return 0.0

    attack = min(1.0, local_time / 0.004)
    decay = math.exp(-4.0 * local_time / duration_s)
    phase = 2.0 * math.pi * frequency_hz * local_time
    # A quiet second harmonic gives the chime a round, toy-like character.
    return attack * decay * (math.sin(phase) + 0.18 * math.sin(2.0 * phase))


def main() -> None:
    output = Path(__file__).resolve().parents[1] / "main" / "button_click.pcm"
    frame_count = round(SAMPLE_RATE * DURATION_SECONDS)
    pcm = bytearray()

    for frame in range(frame_count):
        time_s = frame / SAMPLE_RATE
        # A rising two-note "boop": C6 followed by G6.
        value = 0.58 * note(time_s, 0.000, 0.070, 1046.50)
        value += 0.72 * note(time_s, 0.043, 0.077, 1567.98)

        # Fade the final few milliseconds fully to zero to avoid a pop.
        remaining = DURATION_SECONDS - time_s
        if remaining < 0.010:
            value *= max(0.0, remaining / 0.010)

        sample = round(max(-1.0, min(1.0, value)) * 25_000)
        pcm.extend(struct.pack("<hh", sample, sample))

    output.write_bytes(pcm)
    print(f"Generated {output} ({len(pcm)} bytes, {DURATION_SECONDS * 1000:.0f} ms)")


if __name__ == "__main__":
    main()
