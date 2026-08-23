#!/usr/bin/env python3
"""
Phoenix SDR Serial Diagnostics Tool

Monitors diagnostic output while allowing CAT control of the radio.
Supports USB audio for transmitting test tones and receiving audio.

Supports two modes:
  1. Single-port mode (serial+midi+audio USB): One port for both diagnostics and CAT
     python serial_diag.py --port /dev/ttyACM0

  2. Dual-port mode (Dual Serial USB): Separate ports for diagnostics and CAT
     python serial_diag.py --diag /dev/ttyACM0 --cat /dev/ttyACM1

Commands (type at the prompt):
    tx          - Key transmitter and start 1kHz tone via USB audio
    rx          - Unkey transmitter and stop tone
    tone <Hz>   - Set tone frequency (default 1000 Hz)
    monitor     - Start/stop RX audio monitoring (shows RMS level)
    devices     - List available audio devices
    audio <n>   - Select audio device by index
    usb         - Switch to USB audio mode (PC audio input)
    ssb         - Switch to SSB/microphone mode
    lsb         - Switch to LSB sideband
    usbsb       - Switch to USB sideband
    cw          - Switch to CW mode
    freq 14074  - Set frequency in kHz
    id          - Query radio ID
    if          - Query radio status
    stats       - Show USB_RX statistics
    cat <cmd>   - Send raw CAT command (e.g., 'cat FA;')
    help        - Show this help
    quit        - Exit program

Requirements for audio:
    sudo apt install python3-numpy python3-sounddevice
    # or: pip install numpy sounddevice
"""

import argparse
import serial
import threading
import sys
import time
import re
from datetime import datetime

# Optional audio support
try:
    import numpy as np
    import sounddevice as sd
    AUDIO_AVAILABLE = True
except ImportError:
    AUDIO_AVAILABLE = False
    np = None
    sd = None


class PhoenixDiagnostics:
    def __init__(self, diag_port=None, cat_port=None, shared_port=None,
                 diag_baud=115200, cat_baud=38400, shared_baud=115200):
        self.diag_port = diag_port
        self.cat_port = cat_port
        self.shared_port = shared_port
        self.diag_baud = diag_baud
        self.cat_baud = cat_baud
        self.shared_baud = shared_baud
        self.diag_serial = None
        self.cat_serial = None
        self.shared_serial = None
        self.running = False
        self.diag_thread = None
        self.tx_active = False

        # For single-port mode: coordinate CAT commands with reader thread
        self.serial_lock = threading.Lock()
        self.cat_response_queue = []
        self.waiting_for_cat_response = False

        # Statistics
        self.last_usb_rx_stats = {}
        self.stats_count = 0
        self.last_usb_tx_stats = {}
        self.tx_stats_count = 0

        # Audio settings
        self.audio_device = None  # None = default, or device index
        self.tone_freq = 1000  # Hz
        self.sample_rate = 48000  # Must match Teensy USB audio
        self.audio_stream = None
        self.audio_phase = 0.0
        self.tone_active = False
        self.monitor_active = False
        self.monitor_thread = None
        self.rx_rms_history = []

    def connect(self):
        """Connect to serial port(s)."""
        try:
            # Single-port mode
            if self.shared_port:
                print(f"Connecting to {self.shared_port} at {self.shared_baud} baud (single-port mode)...")
                self.shared_serial = serial.Serial(
                    self.shared_port,
                    self.shared_baud,
                    timeout=0.1
                )
                print(f"  Connected (diagnostics and CAT on same port)")
                return True

            # Dual-port mode
            if self.diag_port:
                print(f"Connecting to diagnostic port {self.diag_port} at {self.diag_baud} baud...")
                self.diag_serial = serial.Serial(
                    self.diag_port,
                    self.diag_baud,
                    timeout=0.1
                )
                print(f"  Connected to diagnostic port")

            if self.cat_port:
                print(f"Connecting to CAT port {self.cat_port} at {self.cat_baud} baud...")
                self.cat_serial = serial.Serial(
                    self.cat_port,
                    self.cat_baud,
                    timeout=0.5
                )
                print(f"  Connected to CAT port")

            return True

        except serial.SerialException as e:
            print(f"Error connecting to serial port: {e}")
            return False

    def disconnect(self):
        """Disconnect from serial port(s)."""
        self.running = False
        if self.diag_thread:
            self.diag_thread.join(timeout=1.0)
        if self.shared_serial:
            self.shared_serial.close()
        if self.diag_serial:
            self.diag_serial.close()
        if self.cat_serial:
            self.cat_serial.close()

    def start_diag_monitor(self):
        """Start the diagnostic monitoring thread."""
        self.running = True
        self.diag_thread = threading.Thread(target=self._diag_reader, daemon=True)
        self.diag_thread.start()

    def _diag_reader(self):
        """Background thread to read diagnostic output."""
        buffer = ""
        ser = self.shared_serial if self.shared_serial else self.diag_serial

        while self.running:
            try:
                if ser and ser.in_waiting:
                    with self.serial_lock:
                        data = ser.read(ser.in_waiting)
                    buffer += data.decode('utf-8', errors='replace')

                    # Process complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self._process_diag_line(line)

                    # In single-port mode, check for CAT responses (end with ;)
                    # CAT responses don't have newlines, so check buffer for semicolons
                    # Drain them whether or not a command is outstanding: anything
                    # arriving unsolicited (AI mode, a late reply) would otherwise sit
                    # in the buffer and be handed back as the response to the *next*
                    # command sent.
                    if self.shared_serial:
                        while ';' in buffer:
                            # Extract CAT response up to and including semicolon
                            idx = buffer.index(';')
                            response = buffer[:idx + 1]
                            buffer = buffer[idx + 1:]
                            # Check if this looks like a CAT response (not diagnostic)
                            if not response.startswith(('USB_', '[', 'DEBUG', 'INFO', 'WARN', 'ERR')):
                                if self.waiting_for_cat_response:
                                    self.cat_response_queue.append(response)
                else:
                    time.sleep(0.01)
            except Exception as e:
                if self.running:
                    print(f"\nDiagnostic read error: {e}")
                time.sleep(0.1)

    def _process_diag_line(self, line):
        """Process and display a diagnostic line."""
        if not line:
            return

        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]

        # Parse USB_RX diagnostic lines (ASRC consumer side)
        if line.startswith("USB_RX:"):
            self._parse_usb_rx_stats(line)
            # Color code based on content
            if "underruns=" in line:
                match = re.search(r'underruns=(\d+)', line)
                if match and int(match.group(1)) > 0:
                    # Highlight underruns in red
                    print(f"\033[91m[{timestamp}] {line}\033[0m")
                    return
            if "WARMUP" in line:
                # Highlight warmup in yellow
                print(f"\033[93m[{timestamp}] {line}\033[0m")
                return
            # Normal RX line in cyan
            print(f"\033[96m[{timestamp}] {line}\033[0m")
            return

        # Parse USB_TX diagnostic lines (USB receive callback side)
        if line.startswith("USB_TX:"):
            self._parse_usb_tx_stats(line)
            # Color code based on content
            # Highlight high zero percentage in red (problem indicator)
            match = re.search(r'zeros=\d+\(([\d.]+)%\)', line)
            if match:
                zero_pct = float(match.group(1))
                if zero_pct > 10.0:  # More than 10% zeros is concerning
                    print(f"\033[91m[{timestamp}] {line}\033[0m")
                    return
            # Highlight overruns in red
            match = re.search(r'overruns=(\d+)', line)
            if match and int(match.group(1)) > 0:
                print(f"\033[91m[{timestamp}] {line}\033[0m")
                return
            # Normal TX line in green
            print(f"\033[92m[{timestamp}] {line}\033[0m")
            return

        # Default: print with timestamp
        print(f"[{timestamp}] {line}")

    def _parse_usb_rx_stats(self, line):
        """Parse USB_RX statistics from diagnostic line."""
        # Format: USB_RX: RUNNING reads=X underruns=Y zeros=Z | buf=min/avg/max | ratio=min/avg/max | rms=X
        try:
            stats = {}

            # Extract state
            if "RUNNING" in line:
                stats['state'] = 'RUNNING'
            elif "WARMUP" in line:
                stats['state'] = 'WARMUP'

            # Extract numeric values
            patterns = [
                (r'reads=(\d+)', 'reads'),
                (r'underruns=(\d+)', 'underruns'),
                (r'zeros=(\d+)', 'zeros'),
                (r'buf=(\d+)/(\d+)/(\d+)', 'buf_min', 'buf_avg', 'buf_max'),
                (r'ratio=([\d.]+)/([\d.]+)/([\d.]+)', 'ratio_min', 'ratio_avg', 'ratio_max'),
                (r'rms=([\d.]+)', 'rms'),
            ]

            for pattern in patterns:
                if len(pattern) == 2:
                    match = re.search(pattern[0], line)
                    if match:
                        stats[pattern[1]] = float(match.group(1)) if '.' in match.group(1) else int(match.group(1))
                else:
                    match = re.search(pattern[0], line)
                    if match:
                        for i, name in enumerate(pattern[1:]):
                            val = match.group(i + 1)
                            stats[name] = float(val) if '.' in val else int(val)

            self.last_usb_rx_stats = stats
            self.stats_count += 1

        except Exception as e:
            pass  # Ignore parsing errors

    def _parse_usb_tx_stats(self, line):
        """Parse USB_TX statistics from diagnostic line."""
        # Format: USB_TX: cb=N samples=N zeros=N(X%) overruns=N | buf=min/avg/max | samp/cb=min/max | rms=X.XXXX
        try:
            stats = {}

            # Extract numeric values
            patterns = [
                (r'cb=(\d+)', 'callbacks'),
                (r'samples=(\d+)', 'samples'),
                (r'zeros=(\d+)', 'zeros'),
                (r'zeros=\d+\(([\d.]+)%\)', 'zeros_pct'),
                (r'overruns=(\d+)', 'overruns'),
                (r'buf=(\d+)/(\d+)/(\d+)', 'buf_min', 'buf_avg', 'buf_max'),
                (r'samp/cb=(\d+)/(\d+)', 'samp_cb_min', 'samp_cb_max'),
                (r'rms=([\d.]+)', 'rms'),
            ]

            for pattern in patterns:
                if len(pattern) == 2:
                    match = re.search(pattern[0], line)
                    if match:
                        val = match.group(1)
                        stats[pattern[1]] = float(val) if '.' in val else int(val)
                else:
                    match = re.search(pattern[0], line)
                    if match:
                        for i, name in enumerate(pattern[1:]):
                            val = match.group(i + 1)
                            stats[name] = float(val) if '.' in val else int(val)

            self.last_usb_tx_stats = stats
            self.tx_stats_count += 1

        except Exception as e:
            pass  # Ignore parsing errors

    @staticmethod
    def rejected(response):
        """True if the radio rejected a CAT command.

        command_parser() in CAT.cpp answers "?;" for an unrecognised keyword or
        a wrong-length parameter block. Note that an *accepted* write often
        answers nothing at all - TX_write and RX_write both return an empty
        string - so an empty response means success, not failure, and the
        absence of "?;" is the only positive signal available.
        """
        return response is not None and response.strip().startswith('?')

    def send_cat(self, command):
        """Send a CAT command and return the response."""
        # Determine which serial port to use
        if self.shared_serial:
            ser = self.shared_serial
        elif self.cat_serial:
            ser = self.cat_serial
        else:
            print("No CAT port connected")
            return None

        # Ensure command ends with semicolon
        if not command.endswith(';'):
            command += ';'

        try:
            if self.shared_serial:
                # Single-port mode: coordinate with reader thread
                self.cat_response_queue.clear()
                self.waiting_for_cat_response = True

                with self.serial_lock:
                    ser.write(command.encode('ascii'))
                    ser.flush()

                # Wait for response from queue
                start_time = time.time()
                while time.time() - start_time < 0.5:
                    if self.cat_response_queue:
                        response = self.cat_response_queue.pop(0)
                        self.waiting_for_cat_response = False
                        return response.strip() if response else None
                    time.sleep(0.01)

                self.waiting_for_cat_response = False
                return None
            else:
                # Dual-port mode: read directly
                ser.reset_input_buffer()
                ser.write(command.encode('ascii'))
                ser.flush()

                # Read response (wait for semicolon or timeout)
                response = ""
                start_time = time.time()
                while time.time() - start_time < 0.5:
                    if ser.in_waiting:
                        char = ser.read(1).decode('ascii', errors='replace')
                        response += char
                        if char == ';':
                            break
                    else:
                        time.sleep(0.01)

                return response.strip() if response else None

        except Exception as e:
            print(f"CAT command error: {e}")
            return None

    # ==================== Audio Methods ====================

    def find_teensy_audio_device(self):
        """Find the Teensy USB audio device."""
        if not AUDIO_AVAILABLE:
            return None

        devices = sd.query_devices()
        for i, dev in enumerate(devices):
            name = dev['name'].lower()
            if 'teensy' in name and dev['max_output_channels'] >= 2:
                return i
        return None

    def list_audio_devices(self):
        """List available audio devices."""
        if not AUDIO_AVAILABLE:
            print("Audio not available. Install: pip install numpy sounddevice")
            return

        print("\nAvailable audio devices:")
        devices = sd.query_devices()
        for i, dev in enumerate(devices):
            marker = ""
            if self.audio_device == i:
                marker = " [SELECTED]"
            elif self.audio_device is None and i == sd.default.device[1]:
                marker = " [DEFAULT OUTPUT]"

            out_ch = dev['max_output_channels']
            in_ch = dev['max_input_channels']
            if out_ch > 0 or in_ch > 0:
                print(f"  {i}: {dev['name']} (out={out_ch}, in={in_ch}){marker}")

        teensy = self.find_teensy_audio_device()
        if teensy is not None:
            print(f"\nTeensy detected at device {teensy}")
        print()

    def set_audio_device(self, device_idx):
        """Set the audio device by index."""
        if not AUDIO_AVAILABLE:
            print("Audio not available. Install: pip install numpy sounddevice")
            return False

        try:
            device_idx = int(device_idx)
            dev = sd.query_devices(device_idx)
            self.audio_device = device_idx
            print(f"Audio device set to {device_idx}: {dev['name']}")
            return True
        except Exception as e:
            print(f"Invalid device index: {e}")
            return False

    def _tone_callback(self, outdata, frames, time_info, status):
        """Callback for audio output stream - generates sine wave."""
        if status:
            print(f"Audio status: {status}")

        if not self.tone_active:
            outdata.fill(0)
            return

        # Generate sine wave
        t = (np.arange(frames) + self.audio_phase) / self.sample_rate
        tone = np.sin(2 * np.pi * self.tone_freq * t).astype(np.float32)

        # Stereo: same tone on both channels (I and Q)
        # For USB mode, this produces a carrier at the tone frequency offset
        outdata[:, 0] = tone * 0.1  # Left channel (I) - 10% amplitude
        outdata[:, 1] = tone * 0.1  # Right channel (Q) - 10% amplitude

        self.audio_phase += frames

    def start_tone(self):
        """Start outputting a tone via USB audio."""
        if not AUDIO_AVAILABLE:
            print("Audio not available. Install: pip install numpy sounddevice")
            return False

        if self.audio_stream is not None:
            self.stop_tone()

        try:
            device = self.audio_device
            if device is None:
                device = self.find_teensy_audio_device()
                if device is not None:
                    print(f"Auto-selected Teensy audio device {device}")

            self.audio_phase = 0.0
            self.tone_active = True
            self.audio_stream = sd.OutputStream(
                device=device,
                samplerate=self.sample_rate,
                channels=2,
                dtype='float32',
                callback=self._tone_callback,
                blocksize=1024
            )
            self.audio_stream.start()
            print(f"Tone started: {self.tone_freq} Hz at {self.sample_rate} Hz sample rate")
            return True
        except Exception as e:
            print(f"Failed to start tone: {e}")
            self.tone_active = False
            return False

    def stop_tone(self):
        """Stop the audio tone."""
        self.tone_active = False
        if self.audio_stream is not None:
            try:
                self.audio_stream.stop()
                self.audio_stream.close()
            except Exception:
                pass
            self.audio_stream = None
            print("Tone stopped")

    def _monitor_callback(self, indata, frames, time_info, status):
        """Callback for audio input stream - monitors RX audio."""
        if status:
            print(f"Audio input status: {status}")

        # Calculate RMS of input
        rms_l = np.sqrt(np.mean(indata[:, 0] ** 2))
        rms_r = np.sqrt(np.mean(indata[:, 1] ** 2))
        rms = (rms_l + rms_r) / 2

        self.rx_rms_history.append(rms)
        if len(self.rx_rms_history) > 50:
            self.rx_rms_history.pop(0)

    def _monitor_display_thread(self):
        """Thread to display RX audio levels."""
        while self.monitor_active:
            if self.rx_rms_history:
                rms = self.rx_rms_history[-1]
                avg_rms = sum(self.rx_rms_history) / len(self.rx_rms_history)

                # Create a simple bar graph
                bar_len = int(rms * 50)
                bar = '=' * min(bar_len, 50)

                # Print on same line
                sys.stdout.write(f"\rRX RMS: {rms:.4f} (avg: {avg_rms:.4f}) [{bar:<50}]")
                sys.stdout.flush()

            time.sleep(0.1)
        print()  # New line when stopping

    def start_monitor(self):
        """Start monitoring RX audio from USB."""
        if not AUDIO_AVAILABLE:
            print("Audio not available. Install: pip install numpy sounddevice")
            return False

        if self.monitor_active:
            self.stop_monitor()
            return True

        try:
            device = self.audio_device
            if device is None:
                device = self.find_teensy_audio_device()
                if device is not None:
                    print(f"Auto-selected Teensy audio device {device}")

            self.rx_rms_history = []
            self.monitor_active = True

            self.input_stream = sd.InputStream(
                device=device,
                samplerate=self.sample_rate,
                channels=2,
                dtype='float32',
                callback=self._monitor_callback,
                blocksize=1024
            )
            self.input_stream.start()

            self.monitor_thread = threading.Thread(target=self._monitor_display_thread, daemon=True)
            self.monitor_thread.start()

            print("RX monitor started (press Enter to stop)")
            return True
        except Exception as e:
            print(f"Failed to start monitor: {e}")
            self.monitor_active = False
            return False

    def stop_monitor(self):
        """Stop monitoring RX audio."""
        self.monitor_active = False
        if hasattr(self, 'input_stream') and self.input_stream is not None:
            try:
                self.input_stream.stop()
                self.input_stream.close()
            except Exception:
                pass
            self.input_stream = None
        if self.monitor_thread:
            self.monitor_thread.join(timeout=0.5)
            self.monitor_thread = None
        print("RX monitor stopped")

    # ==================== Command Handlers ====================

    def cmd_tx(self):
        """Key the transmitter and start tone."""
        # "TX;", not "TX1;". CAT.cpp declares TX with set_len 3, so the parser
        # only accepts a semicolon in position 2; "TX1;" is answered "?;" and
        # the radio stays in receive.
        response = self.send_cat("TX")
        if self.rejected(response):
            print(f"TX refused by radio ({response}) - still in receive")
            return
        self.tx_active = True
        print("TX ON")
        if AUDIO_AVAILABLE:
            self.start_tone()

    def cmd_rx(self):
        """Unkey the transmitter and stop tone."""
        if AUDIO_AVAILABLE and self.tone_active:
            self.stop_tone()
        response = self.send_cat("RX")
        if self.rejected(response):
            # Do not clear tx_active: the radio may still be keyed, and the
            # quit path relies on this flag to unkey it.
            print(f"RX refused by radio ({response}) - may still be transmitting")
            return
        self.tx_active = False
        print("TX OFF")

    def _set_mode(self, command, description):
        """Send a mode/audio-routing command and report honestly."""
        response = self.send_cat(command)
        if self.rejected(response):
            print(f"{description}: refused by radio ({response}) - "
                  f"'{command};' not supported by this firmware")
            return False
        print(description)
        return True

    def cmd_usb(self):
        """Switch to USB audio mode (PC audio input instead of microphone)."""
        self._set_mode("UM1", "Audio: USB (PC input)")

    def cmd_ssb(self):
        """Switch to SSB/microphone mode."""
        self._set_mode("UM0", "Audio: SSB (microphone)")

    def cmd_lsb(self):
        """Switch to LSB sideband."""
        self._set_mode("MD1", "Sideband: LSB")

    def cmd_usb_sideband(self):
        """Switch to USB sideband."""
        self._set_mode("MD2", "Sideband: USB")

    def cmd_cw(self):
        """Switch to CW mode."""
        self._set_mode("MD3", "Mode: CW")

    def cmd_freq(self, freq_khz):
        """Set frequency in kHz."""
        try:
            freq_hz = int(float(freq_khz) * 1000)
        except ValueError:
            print(f"Invalid frequency: {freq_khz}")
            return
        self._set_mode(f"FA{freq_hz:011d}", f"Frequency: {freq_khz} kHz")

    def cmd_id(self):
        """Query radio ID."""
        response = self.send_cat("ID")
        print(f"Radio ID: {response}")

    def cmd_if(self):
        """Query radio status."""
        response = self.send_cat("IF")
        if response:
            print(f"Status: {response}")
            # Parse IF response
            # IF[freq11][step5][rit5][rit_on][xit_on][mem2][rx_tx][mode][vr][scan][split][tone][tone_num][shift];
            if response.startswith("IF") and len(response) >= 38:
                freq = int(response[2:13])
                mode_num = response[29:30]
                modes = {'1': 'LSB', '2': 'USB', '3': 'CW', '4': 'FM', '5': 'AM'}
                mode = modes.get(mode_num, f'?({mode_num})')
                tx = response[28:29]
                print(f"  Frequency: {freq/1000:.3f} kHz")
                print(f"  Mode: {mode}")
                print(f"  TX: {'ON' if tx == '1' else 'OFF'}")

    def cmd_stats(self):
        """Show current USB statistics."""
        print("\n=== USB Audio Statistics ===")

        # TX stats (PC -> Radio for transmission)
        if self.last_usb_tx_stats:
            print("\nUSB_TX (PC -> Radio, for RF transmission):")
            for key, value in self.last_usb_tx_stats.items():
                print(f"  {key}: {value}")
            print(f"  Total reports: {self.tx_stats_count}")
        else:
            print("\nUSB_TX: No statistics received yet")

        # RX stats (ASRC consumer side)
        if self.last_usb_rx_stats:
            print("\nUSB_RX (ASRC consumer):")
            for key, value in self.last_usb_rx_stats.items():
                print(f"  {key}: {value}")
            print(f"  Total reports: {self.stats_count}")
        else:
            print("\nUSB_RX: No statistics received yet")
        print()

    def print_help(self):
        """Print command help."""
        audio_status = "available" if AUDIO_AVAILABLE else "NOT INSTALLED"
        print(f"""
Commands:
    tx          - Key transmitter (PTT on) and start 1kHz tone
    rx          - Unkey transmitter (PTT off) and stop tone
    tone <Hz>   - Set tone frequency (default 1000, current: {self.tone_freq})
    monitor     - Toggle RX audio monitoring (shows RMS level)
    devices     - List available audio devices
    audio <n>   - Select audio device by index

    usb         - Switch to USB audio mode (PC audio input)
    ssb         - Switch to SSB/microphone mode
    lsb         - Switch to LSB sideband
    usbsb       - Switch to USB sideband
    cw          - Switch to CW mode
    freq <kHz>  - Set frequency (e.g., 'freq 14074')
    id          - Query radio ID
    if          - Query radio status
    stats       - Show last USB_RX statistics
    cat <cmd>   - Send raw CAT command (e.g., 'cat FA;')
    help        - Show this help
    quit/exit   - Exit program

Audio support: {audio_status}
Diagnostic output is displayed continuously with timestamps.
Lines containing 'underruns' > 0 are highlighted in red.
Lines containing 'WARMUP' are highlighted in yellow.
""")

    def run_interactive(self):
        """Run interactive command loop."""
        self.print_help()
        print("\nReady. Type 'help' for commands.\n")

        try:
            while True:
                try:
                    cmd = input("\033[94mphoenix>\033[0m ").strip()
                except EOFError:
                    break

                if not cmd:
                    continue

                parts = cmd.lower().split(maxsplit=1)
                command = parts[0]
                args = parts[1] if len(parts) > 1 else ""

                if command in ('quit', 'exit', 'q'):
                    if self.tx_active:
                        self.cmd_rx()  # Safety: ensure TX off
                    if self.monitor_active:
                        self.stop_monitor()
                    break
                elif command == 'tx':
                    self.cmd_tx()
                elif command == 'rx':
                    self.cmd_rx()
                elif command == 'tone':
                    if args:
                        try:
                            self.tone_freq = int(args)
                            print(f"Tone frequency set to {self.tone_freq} Hz")
                            # If tone is active, restart it with new frequency
                            if self.tone_active:
                                self.stop_tone()
                                self.start_tone()
                        except ValueError:
                            print(f"Invalid frequency: {args}")
                    else:
                        print(f"Current tone frequency: {self.tone_freq} Hz")
                elif command == 'monitor':
                    if self.monitor_active:
                        self.stop_monitor()
                    else:
                        self.start_monitor()
                elif command == 'devices':
                    self.list_audio_devices()
                elif command == 'audio' and args:
                    self.set_audio_device(args)
                elif command == 'usb':
                    self.cmd_usb()
                elif command == 'ssb':
                    self.cmd_ssb()
                elif command == 'lsb':
                    self.cmd_lsb()
                elif command == 'usbsb':
                    self.cmd_usb_sideband()
                elif command == 'cw':
                    self.cmd_cw()
                elif command == 'freq' and args:
                    self.cmd_freq(args)
                elif command == 'id':
                    self.cmd_id()
                elif command == 'if':
                    self.cmd_if()
                elif command == 'stats':
                    self.cmd_stats()
                elif command == 'cat' and args:
                    response = self.send_cat(args.upper())
                    print(f"Response: {response}")
                elif command == 'help':
                    self.print_help()
                else:
                    print(f"Unknown command: {cmd}. Type 'help' for commands.")

        except KeyboardInterrupt:
            print("\n")
        finally:
            # Clean up audio
            if self.monitor_active:
                self.stop_monitor()
            if self.tone_active:
                self.stop_tone()
            if self.tx_active:
                print("Turning off TX...")
                self.cmd_rx()


def find_serial_ports():
    """List available serial ports."""
    import serial.tools.list_ports
    ports = serial.tools.list_ports.comports()
    return [(p.device, p.description) for p in ports]


def main():
    parser = argparse.ArgumentParser(
        description='Phoenix SDR Serial Diagnostics Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('--port', '-p',
                        help='Single serial port for both diagnostics and CAT (serial+midi+audio mode)')
    parser.add_argument('--diag', '-d',
                        help='Diagnostic serial port (dual-port mode)')
    parser.add_argument('--cat', '-c',
                        help='CAT control serial port (dual-port mode)')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                        help='Baud rate for single-port mode (default: 115200)')
    parser.add_argument('--diag-baud', type=int, default=115200,
                        help='Diagnostic port baud rate (default: 115200)')
    parser.add_argument('--cat-baud', type=int, default=38400,
                        help='CAT port baud rate (default: 38400)')
    parser.add_argument('--list', '-l', action='store_true',
                        help='List available serial ports and exit')

    args = parser.parse_args()

    if args.list:
        print("\nAvailable serial ports:")
        ports = find_serial_ports()
        if ports:
            for device, description in ports:
                print(f"  {device}: {description}")
        else:
            print("  No serial ports found")
        print()
        return 0

    if not args.port and not args.diag and not args.cat:
        print("Error: Specify --port for single-port mode, or --diag/--cat for dual-port mode")
        print("Use --list to see available ports")
        return 1

    if args.port and (args.diag or args.cat):
        print("Error: Cannot use --port with --diag or --cat")
        print("Use --port alone for single-port mode, or --diag/--cat for dual-port mode")
        return 1

    # Create diagnostics instance
    diag = PhoenixDiagnostics(
        diag_port=args.diag,
        cat_port=args.cat,
        shared_port=args.port,
        diag_baud=args.diag_baud,
        cat_baud=args.cat_baud,
        shared_baud=args.baud
    )

    # Connect to serial ports
    if not diag.connect():
        return 1

    try:
        # Start diagnostic monitoring (for single-port or dual-port with diag)
        if args.port or args.diag:
            diag.start_diag_monitor()

        # Run interactive command loop
        diag.run_interactive()

    finally:
        diag.disconnect()

    return 0


if __name__ == '__main__':
    sys.exit(main())
