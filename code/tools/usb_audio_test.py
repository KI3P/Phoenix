#!/usr/bin/env python3
"""
USB Audio Test - 1 kHz Sine Wave Generator

Outputs a 1 kHz sine wave to a USB audio device for testing the
PC to Teensy USB audio path.

Requirements:
    pip install sounddevice numpy

Usage:
    python usb_audio_test.py              # List devices and use default
    python usb_audio_test.py -d 5         # Use device index 5
    python usb_audio_test.py -l           # List devices only
    python usb_audio_test.py -f 800       # Use 800 Hz instead of 1000 Hz
    python usb_audio_test.py -a 0.5       # Set amplitude to 0.5 (50%)
"""

import argparse
import sys
import time
import numpy as np

try:
    import sounddevice as sd
except ImportError:
    print("Error: sounddevice not installed. Run: pip install sounddevice")
    sys.exit(1)


def list_devices():
    """List all available audio devices."""
    print("\nAvailable audio devices:")
    print("-" * 60)
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        # Show output devices (max_output_channels > 0)
        if dev['max_output_channels'] > 0:
            marker = " <-- default" if i == sd.default.device[1] else ""
            print(f"  [{i:2d}] {dev['name'][:45]:<45} (out: {dev['max_output_channels']}ch){marker}")
    print("-" * 60)


def generate_sine_wave(frequency, sample_rate, duration):
    """Generate a sine wave."""
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
    return np.sin(2 * np.pi * frequency * t).astype(np.float32)


def main():
    parser = argparse.ArgumentParser(
        description="Output a sine wave to USB audio device for testing"
    )
    parser.add_argument(
        "-d", "--device", type=int, default=None,
        help="Audio device index (use -l to list devices)"
    )
    parser.add_argument(
        "-l", "--list", action="store_true",
        help="List available audio devices and exit"
    )
    parser.add_argument(
        "-f", "--frequency", type=float, default=1000.0,
        help="Sine wave frequency in Hz (default: 1000)"
    )
    parser.add_argument(
        "-a", "--amplitude", type=float, default=0.3,
        help="Amplitude 0.0-1.0 (default: 0.3)"
    )
    parser.add_argument(
        "-r", "--rate", type=int, default=48000,
        help="Sample rate in Hz (default: 48000)"
    )
    parser.add_argument(
        "-t", "--duration", type=float, default=None,
        help="Duration in seconds (default: continuous until Ctrl+C)"
    )
    parser.add_argument(
        "-s", "--stereo", action="store_true",
        help="Output stereo (same signal on both channels)"
    )

    args = parser.parse_args()

    # List devices if requested
    if args.list:
        list_devices()
        return 0

    # Show available devices
    list_devices()

    # Select device
    device = args.device
    if device is not None:
        try:
            dev_info = sd.query_devices(device)
            print(f"\nUsing device [{device}]: {dev_info['name']}")
        except Exception as e:
            print(f"Error: Invalid device index {device}: {e}")
            return 1
    else:
        print(f"\nUsing default output device")

    # Parameters
    sample_rate = args.rate
    frequency = args.frequency
    amplitude = max(0.0, min(1.0, args.amplitude))
    channels = 2 if args.stereo else 1

    print(f"\nGenerating {frequency} Hz sine wave:")
    print(f"  Sample rate: {sample_rate} Hz")
    print(f"  Amplitude:   {amplitude:.1%}")
    print(f"  Channels:    {channels}")
    print(f"  Duration:    {'continuous' if args.duration is None else f'{args.duration}s'}")
    print("\nPress Ctrl+C to stop\n")

    # Generate one second of audio to loop
    chunk_duration = 1.0  # seconds
    samples = generate_sine_wave(frequency, sample_rate, chunk_duration)
    samples = samples * amplitude

    # Make stereo if requested
    if channels == 2:
        samples = np.column_stack((samples, samples))

    try:
        if args.duration is not None:
            # Play for specified duration
            full_samples = generate_sine_wave(frequency, sample_rate, args.duration)
            full_samples = full_samples * amplitude
            if channels == 2:
                full_samples = np.column_stack((full_samples, full_samples))

            sd.play(full_samples, samplerate=sample_rate, device=device)
            sd.wait()
        else:
            # Continuous playback using stream
            stream = sd.OutputStream(
                samplerate=sample_rate,
                channels=channels,
                device=device,
                dtype=np.float32
            )

            with stream:
                print("Playing... (Ctrl+C to stop)")
                sample_idx = 0
                chunk_size = 1024

                while True:
                    # Generate chunk
                    t = np.arange(sample_idx, sample_idx + chunk_size) / sample_rate
                    chunk = np.sin(2 * np.pi * frequency * t).astype(np.float32)
                    chunk = chunk * amplitude

                    if channels == 2:
                        chunk = np.column_stack((chunk, chunk))

                    stream.write(chunk)
                    sample_idx += chunk_size

    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception as e:
        print(f"\nError: {e}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
