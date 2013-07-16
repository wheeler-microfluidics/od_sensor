"""
Copyright 2013 Ryan Fobel

This file is part of Microdrop.

Microdrop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Microdrop is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Microdrop.  If not, see <http://www.gnu.org/licenses/>.
"""

import time
import sys
import struct

import gtk
from pygtkhelpers.ui.objectlist import PropertyMapper
from pygtkhelpers.ui.form_view_dialog import FormViewDialog
from pygtkhelpers.ui.extra_widgets import Filepath

import protocol
from utility import *
from plugins.dmf_control_board import INPUT, OUTPUT, HIGH, LOW
from plugin_manager import IPlugin, IWaveformGenerator, Plugin, \
    implements, PluginGlobals, ScheduleRequest, emit_signal, ExtensionPoint, \
    ScheduleRequest
from plugin_helpers import AppDataController, get_plugin_info
from logger import logger
from app_context import get_app
from gui.plugin_manager_dialog import PluginManagerDialog
from flatland import Element, Dict, String, Integer, Boolean, Float, Form
from flatland.validation import ValueAtLeast, ValueAtMost
from ..site_scons.git_util import GitUtil


class ODSensorOptions():
    """
    This class stores the options for a single step in the protocol.
    """
    def __init__(self,
                 measure_od=None,
                 od_threshold=None,
                 over_threshold_subprotocol=None,
                 under_threshold_subprotocol=None):
        if measure_od:
            self.measure_od = measure_od
        else:
            self.measure_od = False
        if od_threshold:
            self.od_threshold = od_threshold
        else:
            self.od_threshold = 0
        if over_threshold_subprotocol:
            self.over_threshold_subprotocol = over_threshold_subprotocol
        else:
            self.over_threshold_subprotocol = None
        if under_threshold_subprotocol:
            self.under_threshold_subprotocol = under_threshold_subprotocol
        else:
            self.under_threshold_subprotocol = None


PluginGlobals.push_env('microdrop.managed')


class ODSensorPlugin(Plugin, AppDataController):
    """
    Plugin for the OD Sensor.
    """

    AppFields = Form.of(
        Integer.named('i2c_address').using(default=1, optional=True,
            validators=[ValueAtLeast(minimum=0), ]),
    )
    
    Fields = Form.of(
        Boolean.named('measure_od').using(default=False, optional=True),
        Float.named('od_threshold').using(default=0, optional=True,
            validators=[ValueAtLeast(minimum=0), ],
            properties={'mappers': [PropertyMapper('sensitive',
                    attr='measure_od'), PropertyMapper('editable',
                            attr='measure_od'), ],
            }),
    )

    implements(IPlugin)
    version = get_plugin_info(path(__file__).parent.parent).version

    def __init__(self):
        self.name = get_plugin_info(path(__file__).parent.parent).plugin_name
        self.url = ""
        self.control_board = None
        self.current_options = self.get_default_options()
        self.initialized = False

    def on_plugin_enable(self):
        """
        Handler called once the plugin instance has been enabled.
        """
        app = get_app()

        if not self.initialized:
            # get a reference to the control board 
            if self.control_board==None:
                observers = ExtensionPoint(IPlugin)
                if len(observers('wheelerlab.dmf_control_board')):
                    self.control_board = \
                        observers('wheelerlab.dmf_control_board')[0]. \
                        control_board
            self.od_sensor_menu_item = gtk.MenuItem('OD threshold events')
            app.main_window_controller.menu_tools.append(
                self.od_sensor_menu_item)
            self.od_sensor_menu_item.connect("activate",
                self.on_edit_od_threshold_events)
            self.initialized = True
        self.od_sensor_menu_item.show()
        super(ODSensorPlugin, self).on_plugin_enable()

    def on_plugin_disable(self):
        """
        Handler called once the plugin instance has been disabled.
        """
        self.od_sensor_menu_item.hide()

    def on_edit_od_threshold_events(self, widget, data=None):
        """
        Handler called when the user clicks on "Edit OD threshold events" in
        the "Tools" menu.
        """
        app = get_app()
        options = self.get_step_options()
        form = Form.of(
            Filepath.named('under_threshold_subprotocol').using(
                default=options.under_threshold_subprotocol,
                optional=True),
            Filepath.named('over_threshold_subprotocol').using(
                default=options.over_threshold_subprotocol,
                optional=True),
        )
        dialog = FormViewDialog()
        valid, response =  dialog.run(form)

        step_options_changed = False
        if valid:
            if response['under_threshold_subprotocol'] and \
                response['under_threshold_subprotocol'] != \
                options.under_threshold_subprotocol:
                options.under_threshold_subprotocol = \
                    response['under_threshold_subprotocol']
                step_options_changed = True
            if response['over_threshold_subprotocol'] and \
                response['over_threshold_subprotocol'] != \
                options.over_threshold_subprotocol:
                options.over_threshold_subprotocol = \
                    response['over_threshold_subprotocol']
                step_options_changed = True
        if step_options_changed:
            emit_signal('on_step_options_changed',
                        [self.name, app.protocol.current_step_number],
                        interface=IPlugin)

    def on_step_run(self):
        """
        Handler called whenever a step is executed.

        Plugins that handle this signal must emit the on_step_complete
        signal once they have completed the step. The protocol controller
        will wait until all plugins have completed the current step before
        proceeding.
        """
        options = self.get_step_options()
        app_values = self.get_app_values()
        app = get_app()
        if (app.realtime_mode or app.running):
            if options.measure_od and self.control_board.connected():
                logger.info('[ODSensorPlugin] measure od from ' \
                            'i2c_address=%d' % (app_values['i2c_address']))
                # get the od (returns a 4 byte float)
                data = self.control_board.i2c_read(app_values['i2c_address'],
                                                   [], 4)
                freq = struct.unpack('f', struct.pack('bbbb', data[0], data[1],
                                                      data[2], data[3]))[0]
                logger.info('[ODSensorPlugin] freq=%.1f, threshold=%.1f' % \
                            (freq, options.od_threshold))
                app.experiment_log.add_data({'freq':freq}, self.name)
                
                sub_protocol = []
                if freq >= options.od_threshold:
                    if options.over_threshold_subprotocol:
                        logger.info('[ODSensorPlugin] freq >= threshold, run '
                                    'subprotocol %s' % \
                                    options.over_threshold_subprotocol)
                        sub_protocol = protocol.Protocol.load(
                            options.over_threshold_subprotocol)
                else:
                    if options.under_threshold_subprotocol:
                        logger.info('[ODSensorPlugin] freq < threshold, run '
                                    'subprotocol %s' % \
                                    options.under_threshold_subprotocol)
                        sub_protocol = protocol.Protocol.load(
                            options.under_threshold_subprotocol)

                # execute all steps in subprotocol
                for i, step in enumerate(sub_protocol):
                    logger.info('[ODSensorPlugin] step %d' % i)
                    dmf_options = step.get_data('microdrop.gui.dmf_device_controller')                    
                    options = step.get_data('wheelerlab.dmf_control_board')
                    
                    state = dmf_options.state_of_channels
                    max_channels = self.control_board.number_of_channels()
                    if len(state) >  max_channels:
                        state = state[0:max_channels]
                    elif len(state) < max_channels:
                        state = np.concatenate([state,
                                np.zeros(max_channels - len(state), int)])
                    else:
                        assert(len(state) == max_channels)

                    emit_signal("set_voltage", options.voltage,
                                interface=IWaveformGenerator)
                    emit_signal("set_frequency", options.frequency,
                                interface=IWaveformGenerator)
                    self.control_board.set_state_of_all_channels(state)
                    time.sleep(options.duration/1000)

            emit_signal('on_step_complete', [self.name, False])

    def on_app_exit(self):
        """
        Handler called just before the Microdrop application exists. 
        """
        pass

    def on_protocol_run(self):
        """
        Handler called when a protocol starts running.
        """
        pass
        #TODO
    
    def on_protocol_pause(self):
        """
        Handler called when a protocol is paused.
        """
        pass
        #TODO

    def set_app_values(self, values_dict):
        logger.debug('[ODSensorPlugin] set_app_values(): '\
                    'values_dict=%s' % (values_dict,))
        super(ODSensorPlugin, self).set_app_values(values_dict)
        
    def get_default_options(self):
        return ODSensorOptions()

    def get_step(self, default):
        if default is None:
            return get_app().protocol.current_step_number
        return default

    def get_step_options(self, step_number=None):
        """
        Return a ODSensorOptions object for the current step in the protocol.
        If none exists yet, create a new one.
        """
        step_number = self.get_step(step_number)
        app = get_app()
        step = app.protocol.steps[step_number]
        options = step.get_data(self.name)
        if options is None:
            # No data is registered for this plugin (for this step).
            options = self.get_default_options()
            step.set_data(self.name, options)
        return options

    def get_step_form_class(self):
        return self.Fields

    def get_step_fields(self):
        return self.Fields.field_schema_mapping.keys()

    def set_step_values(self, values_dict, step_number=None):
        step_number = self.get_step(step_number)
        logger.debug('[ODSensorPlugin] set_step[%d]_values(): '\
                    'values_dict=%s' % (step_number, values_dict,))
        el = self.Fields(value=values_dict)
        try:
            if not el.validate():
                raise ValueError()
            options = self.get_step_options(step_number=step_number)
            for name, field in el.iteritems():
                if field.value is None:
                    continue
                else:
                    setattr(options, name, field.value)
        finally:
            emit_signal('on_step_options_changed', [self.name, step_number],
                    interface=IPlugin)

    def get_step_values(self, step_number=None):
        step_number = self.get_step(step_number)
        options = self.get_step_options(step_number)
        values = {}
        for name in self.Fields.field_schema_mapping:
            value = getattr(options, name)
            values[name] = value
        return values

    def get_step_value(self, name, step_number=None):
        app = get_app()
        if not name in self.Fields.field_schema_mapping:
            raise KeyError('No field with name %s for plugin %s' % (name, self.name))
        if step_number is None:
            step_number = app.protocol.current_step_number
        step = app.protocol.steps[step_number]

        options = step.get_data(self.name)
        if options is None:
            return None
        return getattr(options, name)

    def on_step_options_changed(self, plugin, step_number):
        app = get_app()
        logger.debug('[ODSensorPlugin] on_step_options_changed():'\
                    '%s step #%d' % (plugin, step_number))
        if not app.running and plugin==self.name and \
        app.protocol.current_step_number==step_number:
            self.on_step_run()

    def get_schedule_requests(self, function_name):
        """
        Returns a list of scheduling requests (i.e., ScheduleRequest
        instances) for the function specified by function_name.
        """
        if function_name == 'on_plugin_enable':
            return [ScheduleRequest('wheelerlab.dmf_control_board', self.name)]
        if function_name == 'on_step_run':
            return [ScheduleRequest(self.name, 'wheelerlab.dmf_control_board')]
        return []

PluginGlobals.pop_env()
