#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: Not titled yet
# GNU Radio version: 3.10.1.1

from packaging.version import Version as StrictVersion

if __name__ == '__main__':
    import ctypes
    import sys
    if sys.platform.startswith('linux'):
        try:
            x11 = ctypes.cdll.LoadLibrary('libX11.so')
            x11.XInitThreads()
        except:
            print("Warning: failed to XInitThreads()")

from PyQt5 import Qt
from gnuradio import qtgui
from gnuradio.filter import firdes
import sip
from gnuradio import analog
from gnuradio import blocks
from gnuradio import filter
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
from gnuradio.qtgui import Range, RangeWidget
from PyQt5 import QtCore



from gnuradio import qtgui

class generate_audio_waveforms(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "Not titled yet", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("Not titled yet")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except:
            pass
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("GNU Radio", "generate_audio_waveforms")

        try:
            if StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
                self.restoreGeometry(self.settings.value("geometry").toByteArray())
            else:
                self.restoreGeometry(self.settings.value("geometry"))
        except:
            pass

        ##################################################
        # Variables
        ##################################################
        self.samp_rate = samp_rate = 192000*64
        self.freq_modulator_Hz = freq_modulator_Hz = 2e3
        self.freq_carrier_Hz = freq_carrier_Hz = 1e6
        self.delay = delay = 10

        ##################################################
        # Blocks
        ##################################################
        self._delay_range = Range(0, 1000, 10, 10, 200)
        self._delay_win = RangeWidget(self._delay_range, self.set_delay, "'delay'", "counter_slider", int, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._delay_win)
        self.qtgui_time_sink_x_0 = qtgui.time_sink_f(
            512, #size
            samp_rate/64, #samp_rate
            "", #name
            3, #number of inputs
            None # parent
        )
        self.qtgui_time_sink_x_0.set_update_time(0.10)
        self.qtgui_time_sink_x_0.set_y_axis(-1, 1)

        self.qtgui_time_sink_x_0.set_y_label('Amplitude', "")

        self.qtgui_time_sink_x_0.enable_tags(True)
        self.qtgui_time_sink_x_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, qtgui.TRIG_SLOPE_POS, 0.0, 0, 0, "")
        self.qtgui_time_sink_x_0.enable_autoscale(False)
        self.qtgui_time_sink_x_0.enable_grid(False)
        self.qtgui_time_sink_x_0.enable_axis_labels(True)
        self.qtgui_time_sink_x_0.enable_control_panel(False)
        self.qtgui_time_sink_x_0.enable_stem_plot(False)


        labels = ['Signal 1', 'Signal 2', 'Signal 3', 'Signal 4', 'Signal 5',
            'Signal 6', 'Signal 7', 'Signal 8', 'Signal 9', 'Signal 10']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ['blue', 'red', 'green', 'black', 'cyan',
            'magenta', 'yellow', 'dark red', 'dark green', 'dark blue']
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]
        styles = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        markers = [-1, -1, -1, -1, -1,
            -1, -1, -1, -1, -1]


        for i in range(3):
            if len(labels[i]) == 0:
                self.qtgui_time_sink_x_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_time_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_time_sink_x_0.set_line_width(i, widths[i])
            self.qtgui_time_sink_x_0.set_line_color(i, colors[i])
            self.qtgui_time_sink_x_0.set_line_style(i, styles[i])
            self.qtgui_time_sink_x_0.set_line_marker(i, markers[i])
            self.qtgui_time_sink_x_0.set_line_alpha(i, alphas[i])

        self._qtgui_time_sink_x_0_win = sip.wrapinstance(self.qtgui_time_sink_x_0.qwidget(), Qt.QWidget)
        self.top_layout.addWidget(self._qtgui_time_sink_x_0_win)
        self.qtgui_sink_x_0 = qtgui.sink_c(
            512, #fftsize
            window.WIN_BLACKMAN_hARRIS, #wintype
            0, #fc
            samp_rate/64, #bw
            "", #name
            True, #plotfreq
            True, #plotwaterfall
            True, #plottime
            True, #plotconst
            None # parent
        )
        self.qtgui_sink_x_0.set_update_time(1.0/10)
        self._qtgui_sink_x_0_win = sip.wrapinstance(self.qtgui_sink_x_0.qwidget(), Qt.QWidget)

        self.qtgui_sink_x_0.enable_rf_freq(False)

        self.top_layout.addWidget(self._qtgui_sink_x_0_win)
        self.low_pass_filter_0 = filter.fir_filter_ccf(
            64,
            firdes.low_pass(
                1,
                samp_rate,
                2*freq_modulator_Hz,
                10e3,
                window.WIN_HAMMING,
                6.76))
        self.blocks_multiply_xx_0_0_0 = blocks.multiply_vff(1)
        self.blocks_multiply_xx_0_0 = blocks.multiply_vff(1)
        self.blocks_multiply_xx_0 = blocks.multiply_vff(1)
        self.blocks_float_to_complex_0 = blocks.float_to_complex(1)
        self.blocks_delay_0_0 = blocks.delay(gr.sizeof_float*1, delay)
        self.blocks_delay_0 = blocks.delay(gr.sizeof_float*1, delay)
        self.blocks_complex_to_float_0 = blocks.complex_to_float(1)
        self.analog_sig_source_x_0_1 = analog.sig_source_f(samp_rate, analog.GR_SIN_WAVE, freq_carrier_Hz, 1, 0, 0)
        self.analog_sig_source_x_0_0 = analog.sig_source_f(samp_rate, analog.GR_COS_WAVE, freq_modulator_Hz, 0.25, 0.5, 0)
        self.analog_sig_source_x_0 = analog.sig_source_f(samp_rate, analog.GR_COS_WAVE, freq_carrier_Hz, 1, 0, 0)
        self.analog_am_demod_cf_0 = analog.am_demod_cf(
        	channel_rate=samp_rate/64,
        	audio_decim=1,
        	audio_pass=5000,
        	audio_stop=5500,
        )


        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_am_demod_cf_0, 0), (self.qtgui_time_sink_x_0, 2))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_delay_0, 0))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_multiply_xx_0, 0))
        self.connect((self.analog_sig_source_x_0_0, 0), (self.blocks_multiply_xx_0, 1))
        self.connect((self.analog_sig_source_x_0_1, 0), (self.blocks_delay_0_0, 0))
        self.connect((self.blocks_complex_to_float_0, 0), (self.qtgui_time_sink_x_0, 0))
        self.connect((self.blocks_complex_to_float_0, 1), (self.qtgui_time_sink_x_0, 1))
        self.connect((self.blocks_delay_0, 0), (self.blocks_multiply_xx_0_0_0, 1))
        self.connect((self.blocks_delay_0_0, 0), (self.blocks_multiply_xx_0_0, 1))
        self.connect((self.blocks_float_to_complex_0, 0), (self.low_pass_filter_0, 0))
        self.connect((self.blocks_multiply_xx_0, 0), (self.blocks_multiply_xx_0_0, 0))
        self.connect((self.blocks_multiply_xx_0, 0), (self.blocks_multiply_xx_0_0_0, 0))
        self.connect((self.blocks_multiply_xx_0_0, 0), (self.blocks_float_to_complex_0, 1))
        self.connect((self.blocks_multiply_xx_0_0_0, 0), (self.blocks_float_to_complex_0, 0))
        self.connect((self.low_pass_filter_0, 0), (self.analog_am_demod_cf_0, 0))
        self.connect((self.low_pass_filter_0, 0), (self.blocks_complex_to_float_0, 0))
        self.connect((self.low_pass_filter_0, 0), (self.qtgui_sink_x_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("GNU Radio", "generate_audio_waveforms")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.analog_sig_source_x_0.set_sampling_freq(self.samp_rate)
        self.analog_sig_source_x_0_0.set_sampling_freq(self.samp_rate)
        self.analog_sig_source_x_0_1.set_sampling_freq(self.samp_rate)
        self.low_pass_filter_0.set_taps(firdes.low_pass(1, self.samp_rate, 2*self.freq_modulator_Hz, 10e3, window.WIN_HAMMING, 6.76))
        self.qtgui_sink_x_0.set_frequency_range(0, self.samp_rate/64)
        self.qtgui_time_sink_x_0.set_samp_rate(self.samp_rate/64)

    def get_freq_modulator_Hz(self):
        return self.freq_modulator_Hz

    def set_freq_modulator_Hz(self, freq_modulator_Hz):
        self.freq_modulator_Hz = freq_modulator_Hz
        self.analog_sig_source_x_0_0.set_frequency(self.freq_modulator_Hz)
        self.low_pass_filter_0.set_taps(firdes.low_pass(1, self.samp_rate, 2*self.freq_modulator_Hz, 10e3, window.WIN_HAMMING, 6.76))

    def get_freq_carrier_Hz(self):
        return self.freq_carrier_Hz

    def set_freq_carrier_Hz(self, freq_carrier_Hz):
        self.freq_carrier_Hz = freq_carrier_Hz
        self.analog_sig_source_x_0.set_frequency(self.freq_carrier_Hz)
        self.analog_sig_source_x_0_1.set_frequency(self.freq_carrier_Hz)

    def get_delay(self):
        return self.delay

    def set_delay(self, delay):
        self.delay = delay
        self.blocks_delay_0.set_dly(self.delay)
        self.blocks_delay_0_0.set_dly(self.delay)




def main(top_block_cls=generate_audio_waveforms, options=None):

    if StrictVersion("4.5.0") <= StrictVersion(Qt.qVersion()) < StrictVersion("5.0.0"):
        style = gr.prefs().get_string('qtgui', 'style', 'raster')
        Qt.QApplication.setGraphicsSystem(style)
    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
